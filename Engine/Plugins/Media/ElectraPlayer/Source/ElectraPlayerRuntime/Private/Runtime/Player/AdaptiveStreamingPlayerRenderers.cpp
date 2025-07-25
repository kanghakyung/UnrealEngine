// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlayerCore.h"
#include "Player/AdaptiveStreamingPlayerInternal.h"
#include "Player/AdaptivePlayerOptionKeynames.h"
#include "Utilities/Utilities.h"
#include "ParameterDictionary.h"
#include "Renderer/RendererBase.h"
#include "ElectraPlayerPrivate.h"

#include "HAL/LowLevelMemTracker.h"
#include "Stats/Stats.h"

#include <atomic>

/***************************************************************************************************************************************************/

DECLARE_STATS_GROUP(TEXT("Electra Audio"), STATGROUP_ElectraAudioProcessing, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("Process"), STAT_ElectraAudioProcessing_Process, STATGROUP_ElectraAudioProcessing);

/***************************************************************************************************************************************************/
#ifndef WITH_SOUNDTOUCHZ
#define WITH_SOUNDTOUCHZ 0
#endif
#if WITH_SOUNDTOUCHZ
#include "SoundTouchZ.h"
#endif

/***************************************************************************************************************************************************/
//#define ENABLE_PLAYRATE_OVERRIDE_CVAR

#ifdef ENABLE_PLAYRATE_OVERRIDE_CVAR
#include "HAL/IConsoleManager.h"
static TAutoConsoleVariable<float> CVarElectraPR(TEXT("Electra.PR"), 1.0f, TEXT("Playback rate"), ECVF_Default);
#endif

/***************************************************************************************************************************************************/


namespace Electra
{
	class ISoundTouchFilter
	{
	public:
#if WITH_SOUNDTOUCHZ
		static TUniquePtr<ISoundTouchFilter> Create();
#else
		static TUniquePtr<ISoundTouchFilter> Create()
		{ return MakeUnique<ISoundTouchFilter>(); }
#endif
		ISoundTouchFilter() = default;
		virtual ~ISoundTouchFilter() = default;
		virtual int32 GetNominalOutputSampleNum(int32 InSampleRate, double InMinSlowdown) { return 0; }
		virtual void SetTempo(double InTempo) {}
		virtual void SetMaxOutputSamples(int32 InMaxSamples) {}
		virtual void EnqueueStartSamples(const float* InSourceSamples, int32 InNumSamples, int32 InSampleRate, int32 InNumChannels, const FTimespan& InTimestamp, int64 InSequenceCount) {}
		virtual void EnqueueRunningSamples(const float* InSourceSamples, int32 InNumSamples, int32 InSampleRate, int32 InNumChannels, const FTimespan& InTimestamp, int64 InSequenceCount) {}
		virtual void EnqueueLastSamples(const float* InSourceSamples, int32 InNumSamples, int32 InSampleRate, int32 InNumChannels, const FTimespan& InTimestamp, int64 InSequenceCount) {}
		virtual int32 DequeueSamples(FTimespan& OutTimestamp, int64& OutSequenceIndex, float* OutSamples) { return 0; }
		virtual bool HaveResiduals() { return false; }
		virtual void Reset() {}
	};

	class FAdaptiveStreamingWrappedRenderer : public IAdaptiveStreamingWrappedRenderer
	{
	public:
		virtual ~FAdaptiveStreamingWrappedRenderer() = default;

		FAdaptiveStreamingWrappedRenderer(TSharedPtr<IMediaRenderer, ESPMode::ThreadSafe> RendererToWrap, EStreamType InType)
			: WrappedRenderer(MoveTemp(RendererToWrap))
			, Type(InType)
		{
			// If there is no renderer to wrap there should not be a wrapper created in the first place!
			check(WrappedRenderer.IsValid());
			NumPendingReturnBuffers = 0;
			EnqueuedSamples.Empty();
		}

	private:
		const int32 MaxSampleRate = 48000;
		const int32 NumInterpolationSamplesAt48kHz = 60;
		const double MinPlaybackSpeed = 0.8;
		const double MaxPlaybackSpeed = 1.5;
		const double MinResampleSpeed = 0.98;
		const double MaxResampleSpeed = 1.02;

		struct FPendingReturnBuffer
		{
			IBuffer* Buffer = nullptr;
			bool bRender = false;
			FParamDict Properties;
		};


		FTimeValue GetEnqueuedSampleDuration() override;
		int32 GetNumEnqueuedSamples(TArray<FEnqueuedSampleInfo>* OutOptionalSampleInfos) override;

		void AlwaysEmitSamplesWhenPaused(bool bEmitAlways) override;
		void SetPlaybackRate(double InCurrentPlaybackRate, double InIntendedPlaybackRate, bool bInCurrentlyPaused) override;
		void EnableHoldbackOfFirstRenderableVideoFrame(bool bEnableHoldback) override;

		FTimeRange GetSupportedRenderRateScale() override;
		void SetPlayRateScale(double InNewScale) override;
		double GetPlayRateScale() override;

		void SampleReleasedToPool(IDecoderOutput* InDecoderOutput) override;

		const FParamDict& GetBufferPoolProperties() const override;
		UEMediaError CreateBufferPool(const FParamDict& Parameters) override;
		UEMediaError AcquireBuffer(IBuffer*& OutBuffer, int32 TimeoutInMicroseconds, const FParamDict& InParameters) override;
		UEMediaError ReturnBuffer(IBuffer* Buffer, bool bRender, FParamDict& InOutSampleProperties) override;
		UEMediaError ReleaseBufferPool() override;
		bool CanReceiveOutputFrames(uint64 NumFrames) const override;
		bool GetEnqueuedFrameInfo(int32& OutNumberOfEnqueuedFrames, FTimeValue& OutDurationOfEnqueuedFrames) const override;
		void SetRenderClock(TSharedPtr<IMediaRenderClock, ESPMode::ThreadSafe> InRenderClock) override;
		void SetParentRenderer(TWeakPtr<IMediaRenderer, ESPMode::ThreadSafe> ParentRenderer) override;
		void SetNextApproximatePresentationTime(const FTimeValue& NextApproxPTS) override;
		UEMediaError Flush(const FParamDict& InOptions) override;
		void StartRendering(const FParamDict& InOptions) override;
		void StopRendering(const FParamDict& InOptions) override;

		UEMediaError ReturnVideoBuffer(IBuffer* Buffer, bool bRender, FParamDict& InSampleProperties);
		UEMediaError ReturnAudioBuffer(IBuffer* Buffer, bool bRender, FParamDict& InSampleProperties);
		UEMediaError ReturnBufferCommon(IBuffer* Buffer, bool bRender, FParamDict& InSampleProperties);
		void ReturnAllPendingBuffers(bool bForFlush);

		bool ProcessAudio(bool& bOutNeed2ndBuffer, IBuffer* Buffer, FParamDict& InSampleProperties, double InRate);

		mutable FCriticalSection Lock;
		TSharedPtr<IMediaRenderer, ESPMode::ThreadSafe> WrappedRenderer;
		TSharedPtr<Electra::IMediaRenderClock, ESPMode::ThreadSafe>	RenderClock;
		int64 CurrentValidityValue = 0;
		EStreamType Type = EStreamType::Unsupported;
		bool bIsRunning = false;
		double CurrentPlaybackRate = 0.0;
		double IntendedPlaybackRate = 0.0;
		bool bAlwaysEmitSamplesWhenPaused = false;
		bool bHoldBackFirstVideoFrame = false;
		uint32 NumBuffersNotHeldBack = 0;

		TQueue<FPendingReturnBuffer> PendingReturnBuffers;
		std::atomic<int32> NumPendingReturnBuffers;

		std::atomic<int32> NumBuffersInCirculation;

		// Audio manipulation
		struct FAudioVars
		{
			struct FConfig
			{
				int32 SampleRate = 0;
				int32 NumChannels = 0;
				bool DiffersFrom(int32 InSampleRate, int32 InNumChannels)
				{ return InSampleRate != SampleRate || InNumChannels != NumChannels; }
				void Update(int32 InSampleRate, int32 InNumChannels)
				{
					SampleRate = InSampleRate;
					NumChannels = InNumChannels;
				}
				void Reset()
				{
					SampleRate = 0;
					NumChannels = 0;
				}
			};

			enum class EState
			{
				Disengaged,
				Engaged
			};

			FConfig CurrentConfig;
			TUniquePtr<ISoundTouchFilter> TempoChanger;
			EState CurrentState = EState::Disengaged;
			float LastSampleValuePerChannel[256];
			bool bNextBlockNeedsInterpolation = false;

			int64 OriginalAudioBufferNum = 0;
			int64 OriginalAudioBufferSize = 0;
			int64 AudioBufferSize = 0;
			int32 NumAudioBuffersInUse = 0;
			double RateScale = 1.0;
			float* AudioTempSourceBuffer = nullptr;
			int32 MaxOutputSampleBlockSize = 0;

			FAudioVars()
			{
				Reset();
				CurrentConfig.Reset();
			}
			~FAudioVars()
			{
				FMemory::Free(AudioTempSourceBuffer);
			}
			void Reset()
			{
				CurrentState = EState::Disengaged;
				if (TempoChanger.IsValid())
				{
					TempoChanger->Reset();
				}
				FMemory::Memzero(LastSampleValuePerChannel);
				bNextBlockNeedsInterpolation = false;
			}
			void UpdateLastSampleValue(const float* InSamples, int32 NumSamples, int32 NumChannels)
			{
				if (NumSamples)
				{
					InSamples += (NumSamples - 1) * NumChannels;
					check(NumChannels <= UE_ARRAY_COUNT(LastSampleValuePerChannel));
					FMemory::Memcpy(LastSampleValuePerChannel, InSamples, sizeof(float) * NumChannels);
				}
			}
			void InterpolateFromLastSampleValue(float* InSamples, int32 NumSamples, int32 NumChannels, int32 SampleRate, int32 NumInterpolationSamples)
			{
				// Number of samples over which to interpolate depends on sampling rate.
				int32 NumInter = Utils::Min((int32)(NumInterpolationSamples * (SampleRate / 48000.0)), NumSamples);
				if (NumInter)
				{
					const float *LastInterpSample = InSamples + (NumInter-1) * NumChannels;
					const float Step = 1.0f / (NumInter-1);
					for(int32 i=1; i<NumInter; ++i)
					{
						for(int32 j=0; j<NumChannels; ++j)
						{
							InSamples[j] = LastSampleValuePerChannel[j] + ((LastInterpSample[j] - LastSampleValuePerChannel[j]) * (i*Step));
						}
						InSamples += NumChannels;
					}
				}
			}
		};
		FAudioVars AudioVars;

		// Stats
		TArray<FEnqueuedSampleInfo> EnqueuedSamples;
	};



TSharedPtr<IAdaptiveStreamingWrappedRenderer, ESPMode::ThreadSafe>	FAdaptiveStreamingPlayer::CreateWrappedRenderer(TSharedPtr<IMediaRenderer, ESPMode::ThreadSafe> RendererToWrap, EStreamType InType)
{
	TSharedPtr<IAdaptiveStreamingWrappedRenderer, ESPMode::ThreadSafe> WrappedRenderer;
	if (RendererToWrap.IsValid())
	{
		WrappedRenderer = MakeShared<FAdaptiveStreamingWrappedRenderer, ESPMode::ThreadSafe>(RendererToWrap, InType);
		RendererToWrap->SetParentRenderer(WrappedRenderer);
	}
	return WrappedRenderer;
}


void FAdaptiveStreamingWrappedRenderer::SampleReleasedToPool(IDecoderOutput* InDecoderOutput)
{
	--NumBuffersInCirculation;

	check(InDecoderOutput);
	if (InDecoderOutput && RenderClock.IsValid())
	{
		int64 ValidityValue = InDecoderOutput->GetMutablePropertyDictionary().GetValue(RenderOptionKeys::ValidityValue).SafeGetInt64(-1);
		if (ValidityValue == CurrentValidityValue)
		{
			FTimeValue RenderTime = InDecoderOutput->GetMutablePropertyDictionary().GetValue(RenderOptionKeys::PTS).SafeGetTimeValue(FTimeValue::GetInvalid());
			FTimeValue Duration = InDecoderOutput->GetMutablePropertyDictionary().GetValue(RenderOptionKeys::Duration).SafeGetTimeValue(FTimeValue::GetZero());
			switch(Type)
			{
				case EStreamType::Video:
					RenderClock->SetCurrentTime(Electra::IMediaRenderClock::ERendererType::Video, RenderTime);
					break;
				case EStreamType::Audio:
					RenderClock->SetCurrentTime(Electra::IMediaRenderClock::ERendererType::Audio, RenderTime);
					break;
				default:
					break;
			}

			{
				FScopeLock lock(&Lock);
				for(int32 i=0; i<EnqueuedSamples.Num(); ++i)
				{
					if (EnqueuedSamples[i].PTS == RenderTime)
					{
						EnqueuedSamples.RemoveAt(i);
						break;
					}
				}
			}
		}
	}
	// Note that this is called *by* the wrapped renderer so we do *not* forward this call to there!
}

const FParamDict& FAdaptiveStreamingWrappedRenderer::GetBufferPoolProperties() const
{
	return WrappedRenderer->GetBufferPoolProperties();
}

UEMediaError FAdaptiveStreamingWrappedRenderer::CreateBufferPool(const FParamDict& InParameters)
{
	LLM_SCOPE(ELLMTag::ElectraPlayer);

	FParamDict Parameters(InParameters);

	// Ask for larger buffers in case of audio. For playback speed changes we may need to create artificial
	// samples to slow down audio playback and need larger buffers for that.
	if (Type == EStreamType::Audio)
	{
		AudioVars.TempoChanger = ISoundTouchFilter::Create();
		FMemory::Free(AudioVars.AudioTempSourceBuffer);
		AudioVars.AudioTempSourceBuffer = nullptr;
		AudioVars.NumAudioBuffersInUse = 0;
		AudioVars.OriginalAudioBufferNum = Parameters.GetValue(RenderOptionKeys::NumBuffers).SafeGetInt64(0);
		AudioVars.OriginalAudioBufferSize = Parameters.GetValue(RenderOptionKeys::MaxBufferSize).SafeGetInt64(0);
		if (AudioVars.OriginalAudioBufferSize)
		{
			// We need an occasional extra buffer when the input sample sequence counter changes. Double the number of buffers to accommodate.
			Parameters.Set(RenderOptionKeys::NumBuffers, FVariantValue(AudioVars.OriginalAudioBufferNum * 2));
			AudioVars.AudioTempSourceBuffer = (float*)FMemory::Malloc(AudioVars.OriginalAudioBufferSize);

			int32 SamplesPerBlock = (int32) Parameters.GetValue(RenderOptionKeys::SamplesPerBlock).SafeGetInt64(2048);
			int32 MaxChannels = (int32) Parameters.GetValue(RenderOptionKeys::MaxChannels).SafeGetInt64(8);

			// Get maximum number of samples we may produce when slowing down the most.
			int32 NumTempoSamples = AudioVars.TempoChanger->GetNominalOutputSampleNum(MaxSampleRate, MinPlaybackSpeed);
			int32 NumResampleSamples = (int32)(SamplesPerBlock / MinPlaybackSpeed + 0.5);

			AudioVars.MaxOutputSampleBlockSize = Utils::Max(Utils::Max(NumTempoSamples, NumResampleSamples), SamplesPerBlock);
			AudioVars.AudioBufferSize = AudioVars.MaxOutputSampleBlockSize * MaxChannels * sizeof(float);
			Parameters.Set(RenderOptionKeys::MaxBufferSize, FVariantValue(AudioVars.AudioBufferSize));
			AudioVars.TempoChanger->SetMaxOutputSamples(AudioVars.MaxOutputSampleBlockSize);
		}
	}
	UEMediaError Error = WrappedRenderer->CreateBufferPool(Parameters);
	// Clear the buffer bookkeeping values as creating a buffer _may_ call `SampleReleasedToPool()` to populate
	// its internal structures without us having requested a buffer yet.
	NumBuffersInCirculation = 0;
	EnqueuedSamples.Empty();
	return Error;
}

UEMediaError FAdaptiveStreamingWrappedRenderer::AcquireBuffer(IBuffer*& OutBuffer, int32 TimeoutInMicroseconds, const FParamDict& InParameters)
{
	LLM_SCOPE(ELLMTag::ElectraPlayer);

	if (Type == EStreamType::Audio)
	{
		if (NumBuffersInCirculation >= AudioVars.OriginalAudioBufferNum)
		{
			return UEMEDIA_ERROR_INSUFFICIENT_DATA;
		}
		// For audio we want to return the buffer with the originally requested size.
		// This is to prevent users to do unexpected things like putting more samples in there like they originally wanted
		// to because they see that the buffer can accommodate more.
		OutBuffer = nullptr;
		UEMediaError Error = WrappedRenderer->AcquireBuffer(OutBuffer, TimeoutInMicroseconds, InParameters);
		if (Error == UEMEDIA_ERROR_OK && OutBuffer)
		{
			++NumBuffersInCirculation;
			OutBuffer->GetMutableBufferProperties().Set(RenderOptionKeys::AllocatedSize, FVariantValue(AudioVars.OriginalAudioBufferSize));
		}
		return Error;
	}
	else
	{
		UEMediaError Error = WrappedRenderer->AcquireBuffer(OutBuffer, TimeoutInMicroseconds, InParameters);
		if (Error == UEMEDIA_ERROR_OK && OutBuffer)
		{
			++NumBuffersInCirculation;
		}
		return Error;
	}
}

UEMediaError FAdaptiveStreamingWrappedRenderer::ReturnBuffer(IBuffer* Buffer, bool bRender, FParamDict& InOutSampleProperties)
{
	LLM_SCOPE(ELLMTag::ElectraPlayer);

	Lock.Lock();
	InOutSampleProperties.Set(RenderOptionKeys::ValidityValue, FVariantValue(CurrentValidityValue));
	Lock.Unlock();
	if (Type == EStreamType::Video)
	{
		return ReturnVideoBuffer(Buffer, bRender, InOutSampleProperties);
	}
	else if (Type == EStreamType::Audio)
	{
		return ReturnAudioBuffer(Buffer, bRender, InOutSampleProperties);
	}
	else
	{
		check(!"What type is this?");
		return UEMEDIA_ERROR_NOT_SUPPORTED;
	}
}

UEMediaError FAdaptiveStreamingWrappedRenderer::ReturnVideoBuffer(IBuffer* Buffer, bool bRender, FParamDict& InSampleProperties)
{
	return ReturnBufferCommon(Buffer, bRender, InSampleProperties);
}

UEMediaError FAdaptiveStreamingWrappedRenderer::ReturnAudioBuffer(IBuffer* Buffer, bool bRender, FParamDict& InSampleProperties)
{
	bool bNeed2ndBuffer = false;
	// Process audio when the block will be rendered.
	if (bRender)
	{
		Lock.Lock();
		double DesiredOutputPlayrate = AudioVars.RateScale;
		Lock.Unlock();
		#ifdef ENABLE_PLAYRATE_OVERRIDE_CVAR
		//DesiredOutputPlayrate = Utils::Max(Utils::Min((double)CVarElectraPR.GetValueOnAnyThread(), MaxPlaybackSpeed), MinPlaybackSpeed);
		#endif
		if (ProcessAudio(bNeed2ndBuffer, Buffer, InSampleProperties, DesiredOutputPlayrate))
		{
			if (bNeed2ndBuffer)
			{
				FParamDict NoParams;
				// Return the current buffer. This should allow us to get another one for the
				// residuals immediately.
				UEMediaError Error = ReturnBufferCommon(Buffer, bRender, InSampleProperties);
				check(Error == UEMEDIA_ERROR_OK);
				Error = WrappedRenderer->AcquireBuffer(Buffer, 0, NoParams);
				if (!ensure(Error == UEMEDIA_ERROR_OK))
				{
					return Error;
				}
				++NumBuffersInCirculation;
				ProcessAudio(bNeed2ndBuffer, Buffer, InSampleProperties, DesiredOutputPlayrate);
			}
		}
		else
		{
			// Audio processing produced no samples that can be output right now.
			// Mark this for not-to-get-rendered and return it.
			bRender = false;
		}
	}
	return ReturnBufferCommon(Buffer, bRender, InSampleProperties);
}

UEMediaError FAdaptiveStreamingWrappedRenderer::ReturnBufferCommon(IBuffer* Buffer, bool bRender, FParamDict& InSampleProperties)
{
	FTimeValue Duration = InSampleProperties.GetValue(RenderOptionKeys::Duration).SafeGetTimeValue(FTimeValue::GetZero());
	if (!Duration.IsValid())
	{
		Duration.SetToZero();
		InSampleProperties.Set(RenderOptionKeys::Duration, FVariantValue(Duration));
	}

	bool bIsUnusedReturnBuffer = bRender == false && InSampleProperties.GetValue(RenderOptionKeys::EOSFlag).SafeGetBool(false) == false;

	FScopeLock lock(&Lock);
	FEnqueuedSampleInfo& enqInf = EnqueuedSamples.Emplace_GetRef();
	enqInf.Duration = Duration;
	enqInf.PTS = InSampleProperties.GetValue(RenderOptionKeys::PTS).SafeGetTimeValue(FTimeValue::GetInvalid());

	if (!bIsUnusedReturnBuffer)
	{
		bool bHoldback = !bIsRunning;

		// Never hold back when the player is paused?
		// The main player state as set by the user through API calls, not the current actual rate which
		// may be different during prerolling and buffering!
		if (bAlwaysEmitSamplesWhenPaused && IntendedPlaybackRate == 0.0)
		{
			bHoldback = false;
		}
		// If the video renderer shall not hold back the first frame (used for scrubbing video)
		// then we pass it out. The count is reset in Flush().
		else if (Type == EStreamType::Video && !bHoldBackFirstVideoFrame)
		{
			if (NumBuffersNotHeldBack == 0)
			{
				bHoldback = false;
			}
			if (!bHoldback)
			{
				++NumBuffersNotHeldBack;
			}
		}
		if (bHoldback)
		{
			FPendingReturnBuffer pb;
			pb.Buffer = Buffer;
			pb.bRender = bRender;
			pb.Properties = InSampleProperties;
			PendingReturnBuffers.Enqueue(MoveTemp(pb));
			++NumPendingReturnBuffers;
			return UEMEDIA_ERROR_OK;
		}
	}
	lock.Unlock();
	return WrappedRenderer->ReturnBuffer(Buffer, bRender, InSampleProperties);
}



UEMediaError FAdaptiveStreamingWrappedRenderer::ReleaseBufferPool()
{
	LLM_SCOPE(ELLMTag::ElectraPlayer);

	ReturnAllPendingBuffers(false);
	EnqueuedSamples.Empty();
	AudioVars.Reset();
	Lock.Unlock();

	return WrappedRenderer->ReleaseBufferPool();
}

bool FAdaptiveStreamingWrappedRenderer::CanReceiveOutputFrames(uint64 NumFrames) const
{
	int32 NumPending = NumPendingReturnBuffers;
	return WrappedRenderer->CanReceiveOutputFrames(NumFrames + NumPending);
}

void FAdaptiveStreamingWrappedRenderer::SetRenderClock(TSharedPtr<IMediaRenderClock, ESPMode::ThreadSafe> InRenderClock)
{
	RenderClock = InRenderClock;
	WrappedRenderer->SetRenderClock(InRenderClock);
}

bool FAdaptiveStreamingWrappedRenderer::GetEnqueuedFrameInfo(int32& OutNumberOfEnqueuedFrames, FTimeValue& OutDurationOfEnqueuedFrames) const
{
	return WrappedRenderer->GetEnqueuedFrameInfo(OutNumberOfEnqueuedFrames, OutDurationOfEnqueuedFrames);
}

void FAdaptiveStreamingWrappedRenderer::SetParentRenderer(TWeakPtr<IMediaRenderer, ESPMode::ThreadSafe> ParentRenderer)
{
	check(!"this must not be called");
}

void FAdaptiveStreamingWrappedRenderer::SetNextApproximatePresentationTime(const FTimeValue& NextApproxPTS)
{
	WrappedRenderer->SetNextApproximatePresentationTime(NextApproxPTS);
}

UEMediaError FAdaptiveStreamingWrappedRenderer::Flush(const FParamDict& InOptions)
{
	LLM_SCOPE(ELLMTag::ElectraPlayer);

	ReturnAllPendingBuffers(true);
	++CurrentValidityValue;
	NumBuffersNotHeldBack = 0;
	EnqueuedSamples.Empty();
	AudioVars.Reset();
	Lock.Unlock();

	return WrappedRenderer->Flush(InOptions);
}

void FAdaptiveStreamingWrappedRenderer::StartRendering(const FParamDict& InOptions)
{
	ReturnAllPendingBuffers(false);
	bIsRunning = true;
	Lock.Unlock();

	WrappedRenderer->StartRendering(InOptions);
}

void FAdaptiveStreamingWrappedRenderer::StopRendering(const FParamDict& InOptions)
{
	Lock.Lock();
	bIsRunning = false;
	Lock.Unlock();

	WrappedRenderer->StopRendering(InOptions);
}

void FAdaptiveStreamingWrappedRenderer::ReturnAllPendingBuffers(bool bForFlush)
{
	Lock.Lock();
	while(NumPendingReturnBuffers > 0)
	{
		FPendingReturnBuffer Buffer;
		PendingReturnBuffers.Dequeue(Buffer);
		--NumPendingReturnBuffers;
		Lock.Unlock();
		WrappedRenderer->ReturnBuffer(Buffer.Buffer, bForFlush ? false : Buffer.bRender, Buffer.Properties);
		Lock.Lock();
	}
}


FTimeValue FAdaptiveStreamingWrappedRenderer::GetEnqueuedSampleDuration()
{
	FScopeLock lock(&Lock);
	FTimeValue dur(FTimeValue::GetZero());
	for(auto &it : EnqueuedSamples)
	{
		if (it.PTS.IsValid())
		{
			dur += it.Duration;
		}
	}
	return dur;
}

int32 FAdaptiveStreamingWrappedRenderer::GetNumEnqueuedSamples(TArray<IAdaptiveStreamingWrappedRenderer::FEnqueuedSampleInfo>* OutOptionalSampleInfos)
{
	FScopeLock lock(&Lock);

	int32 NumAvail = 0;
	FTimeValue DurAvail(FTimeValue::GetZero());
	if (!WrappedRenderer->GetEnqueuedFrameInfo(NumAvail, DurAvail))
	{
		NumAvail = 0;
		DurAvail = FTimeValue::GetZero();
	}

	if (OutOptionalSampleInfos)
	{
		for(auto &it : EnqueuedSamples)
		{
			if (it.PTS.IsValid())
			{
				(*OutOptionalSampleInfos).Emplace(it);
			}
		}
	}
	return EnqueuedSamples.Num() + NumAvail;
}

void FAdaptiveStreamingWrappedRenderer::AlwaysEmitSamplesWhenPaused(bool bEmitAlways)
{
	FScopeLock lock(&Lock);
	bAlwaysEmitSamplesWhenPaused = bEmitAlways;
}

void FAdaptiveStreamingWrappedRenderer::SetPlaybackRate(double InCurrentPlaybackRate, double InIntendedPlaybackRate, bool bInCurrentlyPaused)
{
	FScopeLock lock(&Lock);
	CurrentPlaybackRate = bInCurrentlyPaused ? 0.0 : InCurrentPlaybackRate;
	IntendedPlaybackRate = bInCurrentlyPaused ? 0.0 : InIntendedPlaybackRate;
}

void FAdaptiveStreamingWrappedRenderer::EnableHoldbackOfFirstRenderableVideoFrame(bool bEnableHoldback)
{
	FScopeLock lock(&Lock);
	bHoldBackFirstVideoFrame = bEnableHoldback;
}

FTimeRange FAdaptiveStreamingWrappedRenderer::GetSupportedRenderRateScale()
{
	FScopeLock lock(&Lock);
	FTimeRange Range;
	if (Type == EStreamType::Audio)
	{
		Range.Start.SetFromSeconds(MinPlaybackSpeed);
		Range.End.SetFromSeconds(MaxPlaybackSpeed);
	}
	return Range;
}

void FAdaptiveStreamingWrappedRenderer::SetPlayRateScale(double InNewScale)
{
	FScopeLock lock(&Lock);
	// Clamp to within permitted range just in case.
	InNewScale = Utils::Max(Utils::Min(InNewScale, MaxPlaybackSpeed), MinPlaybackSpeed);
	// Quantize to 0.005 multiples.
	AudioVars.RateScale = (double)((int32)(InNewScale * 200.0)) / 200.0;
}

double FAdaptiveStreamingWrappedRenderer::GetPlayRateScale()
{
	return AudioVars.RateScale;
}


bool FAdaptiveStreamingWrappedRenderer::ProcessAudio(bool& bOutNeed2ndBuffer, IBuffer* Buffer, FParamDict& InSampleProperties, double InRate)
{
	FScopeLock lock(&Lock);

	SCOPE_CYCLE_COUNTER(STAT_ElectraAudioProcessing_Process);

	bool bGetResiduals = bOutNeed2ndBuffer;
	bOutNeed2ndBuffer = false;

	int32 SizeInBytes = (int32)InSampleProperties.GetValue(RenderOptionKeys::UsedByteSize).SafeGetInt64();
	FTimeValue Timestamp = InSampleProperties.GetValue(RenderOptionKeys::PTS).SafeGetTimeValue(FTimeValue::GetInvalid());
	FTimeValue Duration = InSampleProperties.GetValue(RenderOptionKeys::Duration).SafeGetTimeValue(FTimeValue::GetZero());
	int32 NumChannels = (int32)InSampleProperties.GetValue(RenderOptionKeys::NumChannels).SafeGetInt64();
	int32 NumSamples = SizeInBytes / NumChannels / sizeof(float);
	int32 SampleRate = (int32)InSampleProperties.GetValue(RenderOptionKeys::SampleRate).SafeGetInt64();
	float* BufferAddress = (float*)Buffer->GetBufferProperties().GetValue(RenderOptionKeys::AllocatedAddress).GetPointer();

	if (AudioVars.CurrentConfig.DiffersFrom(SampleRate, NumChannels))
	{
		AudioVars.CurrentConfig.Update(SampleRate, NumChannels);
		AudioVars.Reset();
	}

	auto DisengageTempoChanger = [&]() -> bool
	{
		if (AudioVars.CurrentState == FAudioVars::EState::Engaged)
		{
			AudioVars.TempoChanger->EnqueueLastSamples(BufferAddress, NumSamples, SampleRate, NumChannels, Timestamp.GetAsTimespan(), Timestamp.GetSequenceIndex());
			AudioVars.CurrentState = FAudioVars::EState::Disengaged;
			AudioVars.bNextBlockNeedsInterpolation = true;
			FTimespan ts;
			int64 si;
			NumSamples = AudioVars.TempoChanger->DequeueSamples(ts, si, BufferAddress);
			Timestamp.SetFromTimespan(ts, si);
			bOutNeed2ndBuffer = AudioVars.TempoChanger->HaveResiduals();
			return true;
		}
		return false;
	};

	bool bUpdateProperties = false;

	if (bGetResiduals)
	{
		FTimespan ts;
		int64 si;
		NumSamples = AudioVars.TempoChanger->DequeueSamples(ts, si, BufferAddress);
		Timestamp.SetFromTimespan(ts, si);
		bUpdateProperties = true;
	}
	else if (InRate != 1.0)
	{
		// Small enough change to use resampler where pitch changes may not be that noticeable?
#if WITH_SOUNDTOUCHZ
		if (InRate >= MinResampleSpeed && InRate <= MaxResampleSpeed)
#else
		if (InRate >= MinPlaybackSpeed && InRate <= MaxPlaybackSpeed)
#endif
		{
			bool bWasEngaged = DisengageTempoChanger();
			if (bWasEngaged)
			{
				bUpdateProperties = true;
			}
			else
			{
				int32 NumOutputSamples = (int32) FMath::RoundToZero(NumSamples / InRate);
				if (NumOutputSamples > 16)
				{
					const int32 MaxOutSamples = AudioVars.MaxOutputSampleBlockSize;
					check(SizeInBytes <= AudioVars.OriginalAudioBufferSize);
					FMemory::Memcpy(AudioVars.AudioTempSourceBuffer, BufferAddress, SizeInBytes);

					double Offset = 0.0;
					int32 o = 0;
					double Step = (double)NumSamples / (double)NumOutputSamples;
					while(o < NumOutputSamples && o < MaxOutSamples)
					{
						int32 I0 = (int32)Offset;
						if (I0+1 >= NumSamples)
						{
							break;
						}
						double F0 = Offset - I0;
						for(int32 nC=0; nC<NumChannels; ++nC)
						{
							double S0 = AudioVars.AudioTempSourceBuffer[I0       * NumChannels + nC];
							double S1 = AudioVars.AudioTempSourceBuffer[(I0 + 1) * NumChannels + nC];
							double S = S0 + (S1-S0) * F0;
							BufferAddress[o * NumChannels + nC] = (float) S;
						}
						++o;
						Offset += Step;
					}
					NumSamples = o;
					bUpdateProperties = true;
				}
			}
		}
#if WITH_SOUNDTOUCHZ
		else
		{
			if (AudioVars.CurrentState == FAudioVars::EState::Disengaged)
			{
				AudioVars.TempoChanger->EnqueueStartSamples(BufferAddress, NumSamples, SampleRate, NumChannels, Timestamp.GetAsTimespan(), Timestamp.GetSequenceIndex());
				AudioVars.CurrentState = FAudioVars::EState::Engaged;
				AudioVars.bNextBlockNeedsInterpolation = true;
			}
			else
			{
				AudioVars.TempoChanger->SetTempo(InRate);
				AudioVars.TempoChanger->EnqueueRunningSamples(BufferAddress, NumSamples, SampleRate, NumChannels, Timestamp.GetAsTimespan(), Timestamp.GetSequenceIndex());
			}
			FTimespan ts;
			int64 si;
			NumSamples = AudioVars.TempoChanger->DequeueSamples(ts, si, BufferAddress);
			bOutNeed2ndBuffer = AudioVars.TempoChanger->HaveResiduals();
			if (NumSamples == 0 && !bOutNeed2ndBuffer)
			{
				return false;
			}
			Timestamp.SetFromTimespan(ts, si);
			bUpdateProperties = true;
		}
#endif
	}
	else
	{
		bUpdateProperties = DisengageTempoChanger();
	}

	if (bUpdateProperties)
	{
		check(NumSamples <= AudioVars.AudioBufferSize / NumChannels / sizeof(float));
		InSampleProperties.Set(RenderOptionKeys::UsedByteSize, FVariantValue((int64)(NumSamples * sizeof(float) * NumChannels)));
		InSampleProperties.Set(RenderOptionKeys::PTS, FVariantValue(Timestamp));
		InSampleProperties.Set(RenderOptionKeys::Duration, FVariantValue(FTimeValue(NumSamples, SampleRate, 0)));
	}

	// Need to interpolate this block's start samples from the last block's last values?
	if (AudioVars.bNextBlockNeedsInterpolation)
	{
		AudioVars.bNextBlockNeedsInterpolation = false;
		AudioVars.InterpolateFromLastSampleValue(BufferAddress, NumSamples, NumChannels, SampleRate, NumInterpolationSamplesAt48kHz);
	}

	// Remember last sample value for interpolation, if necessary.
	AudioVars.UpdateLastSampleValue(BufferAddress, NumSamples, NumChannels);
	return true;
}




#if WITH_SOUNDTOUCHZ

class FSoundTouchFilter : public ISoundTouchFilter
{
public:
	FSoundTouchFilter();
	virtual ~FSoundTouchFilter();
	int32 GetNominalOutputSampleNum(int32 InSampleRate, double InMinSlowdown) override;
	void SetTempo(double InTempo) override;
	void SetMaxOutputSamples(int32 InMaxSamples) override;
	void EnqueueStartSamples(const float* InSourceSamples, int32 InNumSamples, int32 InSampleRate, int32 InNumChannels, const FTimespan& InTimestamp, int64 InSequenceCount) override;
	void EnqueueRunningSamples(const float* InSourceSamples, int32 InNumSamples, int32 InSampleRate, int32 InNumChannels, const FTimespan& InTimestamp, int64 InSequenceCount) override;
	void EnqueueLastSamples(const float* InSourceSamples, int32 InNumSamples, int32 InSampleRate, int32 InNumChannels, const FTimespan& InTimestamp, int64 InSequenceCount) override;
	int32 DequeueSamples(FTimespan& OutTimestamp, int64& OutSequenceIndex, float* OutSamples) override;
	bool HaveResiduals() override;
	void Reset() override;

private:
	template <typename T>
	inline T Min(T a, T b)
	{
		return a < b ? a : b;
	}

	template <typename T>
	inline T Max(T a, T b)
	{
		return a > b ? a : b;
	}

	struct FSampleBlockInfo
	{
		FTimespan Timestamp;
		int64 SequenceCount = 0;
		int32 NumSamples = 0;
		int32 SampleRate = 0;
		int32 NumChannels = 0;
	};

	FSoundTouch ST;
	double Tempo = 1.0;
	float* TempBuffer = nullptr;
	uint32 TempBufferSize = 0;
	uint32 MaxOutputSampleNum = 0;

	TArray<FSampleBlockInfo> EnqueuedSampleBlockInfos;
	FSampleBlockInfo ResidualSampleBlockInfo;
	uint32 NumProcessedThisCall = 0;
	uint32 DiscardFirstNSamples = 0;
	int32 ResidualSampleBlockOffset = 0;
	bool bTerminalBlockAdded = false;

	void ApplyConfiguration(FSoundTouch& InST);
	void PrepareTempBuffer(int32 InNumSamples, int32 InNumChannels, bool bFillZero=false);
	int32 PrepareInputInTempBuffer(const float* InSamples, int32 InNumSamples, int32 InNumChannels, bool bAddEndMarker=false);

	void AppendEnqueuedSampleInfo(int32 InNumSamples, int32 InSampleRate, int32 InNumChannels, const FTimespan& InTimestamp, int64 InSequenceCount);

};

TUniquePtr<ISoundTouchFilter> ISoundTouchFilter::Create()
{
	return MakeUnique<FSoundTouchFilter>();
}

void FSoundTouchFilter::PrepareTempBuffer(int32 InNumSamples, int32 InNumChannels, bool bFillZero)
{
	uint32 nb = InNumSamples * InNumChannels * sizeof(float);
	if (nb > TempBufferSize)
	{
		FMemory::Free(TempBuffer);
		TempBuffer = (float*)FMemory::Malloc(nb);
		TempBufferSize = nb;
	}
	if (TempBuffer && bFillZero)
	{
		FMemory::Memzero(TempBuffer, TempBufferSize);
	}
}

int32 FSoundTouchFilter::PrepareInputInTempBuffer(const float* InSamples, int32 InNumSamples, int32 InNumChannels, bool bAddEndMarker)
{
	const int32 MarkerBlockSize = bAddEndMarker ? ST.GetSetting(FSoundTouch::ESetting::NominalInputSequence) + 512 : 0;
	int32 ns = InNumSamples + MarkerBlockSize;

	PrepareTempBuffer(ns, InNumChannels);
	float* Flt = TempBuffer;
	FMemory::Memcpy(Flt, InSamples, InNumSamples*InNumChannels*sizeof(float));
	if (bAddEndMarker)
	{
		Flt += InNumSamples*InNumChannels;
		for(int32 i=0; i<MarkerBlockSize; ++i)
		{
			for(int32 j=0; j<InNumChannels; ++j)
			{
				*Flt++ = 100.0f;
			}
		}
	}
	return ns;
}

void FSoundTouchFilter::AppendEnqueuedSampleInfo(int32 InNumSamples, int32 InSampleRate, int32 InNumChannels, const FTimespan& InTimestamp, int64 InSequenceCount)
{
	FSampleBlockInfo SampleTime;
	SampleTime.Timestamp = InTimestamp;
	SampleTime.SequenceCount = InSequenceCount;
	SampleTime.NumSamples = InNumSamples;
	SampleTime.SampleRate = InSampleRate;
	SampleTime.NumChannels = InNumChannels;
	EnqueuedSampleBlockInfos.Emplace(MoveTemp(SampleTime));
}


FSoundTouchFilter::FSoundTouchFilter()
{
}

FSoundTouchFilter::~FSoundTouchFilter()
{
	FMemory::Free(TempBuffer);
}


int32 FSoundTouchFilter::GetNominalOutputSampleNum(int32 InSampleRate, double InMinSlowdown)
{
	FSoundTouch TempST;
	TempST.SetSampleRate(InSampleRate);
	TempST.SetChannels(1);
	ApplyConfiguration(TempST);
	TempST.SetTempo(InMinSlowdown);
	int32 NumNominalOut = TempST.GetSetting(FSoundTouch::ESetting::NominalOutputSequence) + 512;		// +512 for safeties sake
	return NumNominalOut;
}


void FSoundTouchFilter::SetTempo(double InTempo)
{
	Tempo = InTempo;
}

void FSoundTouchFilter::SetMaxOutputSamples(int32 InMaxSamples)
{
	MaxOutputSampleNum = (uint32) InMaxSamples;
}


void FSoundTouchFilter::ApplyConfiguration(FSoundTouch& InST)
{
	InST.SetSetting(FSoundTouch::ESetting::UseQuickseek, 1);
	InST.SetSetting(FSoundTouch::ESetting::UseAAFilter, 0);
}

void FSoundTouchFilter::EnqueueStartSamples(const float* InSourceSamples, int32 InNumSamples, int32 InSampleRate, int32 InNumChannels, const FTimespan& InTimestamp, int64 InSequenceCount)
{
	Reset();
	ST.SetSampleRate(InSampleRate);
	ST.SetChannels(InNumChannels);
	ST.SetTempo(1.0);
	ApplyConfiguration(ST);
  	DiscardFirstNSamples = ST.GetSetting(FSoundTouch::ESetting::InitialLatency);

	PrepareTempBuffer(DiscardFirstNSamples, InNumChannels, true);
	uint32 UnprocessedBeforeAdded = ST.NumUnprocessedSamples();
	ST.PutSamples(TempBuffer, DiscardFirstNSamples);

	PrepareInputInTempBuffer(InSourceSamples, InNumSamples, InNumChannels);
	ST.PutSamples(TempBuffer, InNumSamples);
	AppendEnqueuedSampleInfo(InNumSamples, InSampleRate, InNumChannels, InTimestamp, InSequenceCount);
	NumProcessedThisCall = UnprocessedBeforeAdded + InNumSamples + DiscardFirstNSamples - ST.NumUnprocessedSamples();

	// Discard a few additional first samples which could potentially be zeros from the initial fill and the initial latency *estimate* being a bit off.
	DiscardFirstNSamples += 4;
}

void FSoundTouchFilter::EnqueueRunningSamples(const float* InSourceSamples, int32 InNumSamples, int32 InSampleRate, int32 InNumChannels, const FTimespan& InTimestamp, int64 InSequenceCount)
{
	ST.SetTempo(Tempo);
	uint32 UnprocessedBeforeAdded = ST.NumUnprocessedSamples();
	PrepareInputInTempBuffer(InSourceSamples, InNumSamples, InNumChannels);
	ST.PutSamples(TempBuffer, InNumSamples);
	AppendEnqueuedSampleInfo(InNumSamples, InSampleRate, InNumChannels, InTimestamp, InSequenceCount);
	NumProcessedThisCall = UnprocessedBeforeAdded + InNumSamples - ST.NumUnprocessedSamples();
	ResidualSampleBlockOffset = 0;
}

void FSoundTouchFilter::EnqueueLastSamples(const float* InSourceSamples, int32 InNumSamples, int32 InSampleRate, int32 InNumChannels, const FTimespan& InTimestamp, int64 InSequenceCount)
{
	ST.SetTempo(1.0);
	uint32 UnprocessedBeforeAdded = ST.NumUnprocessedSamples();
	InNumSamples = PrepareInputInTempBuffer(InSourceSamples, InNumSamples, InNumChannels, true);
	ST.PutSamples(TempBuffer, InNumSamples);
	AppendEnqueuedSampleInfo(InNumSamples, InSampleRate, InNumChannels, InTimestamp, InSequenceCount);
	NumProcessedThisCall = UnprocessedBeforeAdded + InNumSamples - ST.NumUnprocessedSamples();
	ResidualSampleBlockOffset = 0;
	bTerminalBlockAdded = true;
}

int32 FSoundTouchFilter::DequeueSamples(FTimespan& OutTimestamp, int64& OutSequenceIndex, float* OutSamples)
{
	// If there is output at the beginning from engaging the processor to discard, discard it right now.
	if (DiscardFirstNSamples)
	{
		uint32 NumAvail = ST.NumSamples();
		uint32 NumToPurge = Min(DiscardFirstNSamples, NumAvail);
		uint32 Got = ST.ReceiveSamples(NumToPurge);
		check(Got == NumToPurge);
		check(DiscardFirstNSamples >= Got);
		DiscardFirstNSamples -= Got;
		// Still more to discard?
		if (DiscardFirstNSamples)
		{
			return 0;
		}
		NumProcessedThisCall = Got > NumProcessedThisCall ? 0 : NumProcessedThisCall - Got;
	}

	uint32 numGot = 0;
	uint32 SampleOffset = 0;
	int32 NumChannels = 0;
	if (ResidualSampleBlockOffset == 0)
	{
		// Given the number of input samples consumed in the preceeding process call we need to
		// check if the input had a changing sequence counter.
		int32 NumConsumed = 0;
		int32 Index = 0;
		int32 ToGo = (int32)NumProcessedThisCall;
		for(Index=0; Index<EnqueuedSampleBlockInfos.Num(); )
		{
			if (ToGo >= EnqueuedSampleBlockInfos[Index].NumSamples)
			{
				ToGo -= EnqueuedSampleBlockInfos[Index].NumSamples;
				++Index;
			}
			else
			{
				break;
			}
		}
		// Different sequence count?
		double ResidualBlockPercentage = 0.0;
		if (EnqueuedSampleBlockInfos[Index].SequenceCount != EnqueuedSampleBlockInfos[0].SequenceCount)
		{
			ResidualSampleBlockInfo = EnqueuedSampleBlockInfos[Index];
			ResidualBlockPercentage = (double)ToGo / (double)NumProcessedThisCall;
		}

		OutTimestamp = EnqueuedSampleBlockInfos[0].Timestamp;
		OutSequenceIndex = EnqueuedSampleBlockInfos[0].SequenceCount;

		// Remove outdated sample times.
		for(int32 i=0; i<Index; ++i)
		{
			EnqueuedSampleBlockInfos.RemoveAt(0);
		}
		if (!EnqueuedSampleBlockInfos.Num())
		{
			return 0;
		}
		// Adjust the frontmost sample time.
		EnqueuedSampleBlockInfos[0].NumSamples -= ToGo;
		FTimespan TimeOffset((int64)ToGo * 10000000 / EnqueuedSampleBlockInfos[0].SampleRate);
		EnqueuedSampleBlockInfos[0].Timestamp += TimeOffset;

		NumChannels = EnqueuedSampleBlockInfos[0].NumChannels;
		int32 NumSamples = (int32) ST.NumSamples();
		PrepareTempBuffer(NumSamples, NumChannels, bTerminalBlockAdded);

		numGot = Min(ST.ReceiveSamples(TempBuffer, NumSamples), MaxOutputSampleNum);
		if (numGot && bTerminalBlockAdded)
		{
			// Find the marker
			const float* TempBufEnd = TempBuffer + (numGot - 1) * NumChannels;
			bool bLocked = false;
			for(uint32 i=1; i<numGot; ++i, TempBufEnd-=NumChannels)
			{
				if (!bLocked)
				{
					if (*TempBufEnd > 98.0f)
					{
						bLocked = true;
					}
				}
				else
				{
					if (*TempBufEnd <= 1.2f)
					{
						break;
					}
				}
			}
			numGot = (TempBufEnd - TempBuffer) / NumChannels + 1;
		}
		// Split the output into two parts due to a change in sequence count?
		if (ResidualBlockPercentage)
		{
			ResidualSampleBlockOffset = (int32)(numGot * ResidualBlockPercentage);
			ResidualSampleBlockInfo.NumSamples = numGot - ResidualSampleBlockOffset;
			numGot -= ResidualSampleBlockInfo.NumSamples;
		}
	}
	else
	{
		OutTimestamp = ResidualSampleBlockInfo.Timestamp;
		OutSequenceIndex = ResidualSampleBlockInfo.SequenceCount;
		NumChannels = ResidualSampleBlockInfo.NumChannels;
		SampleOffset = ResidualSampleBlockOffset;
		ResidualSampleBlockOffset = 0;
		numGot = (uint32) ResidualSampleBlockInfo.NumSamples;
	}

	const float* Flt = TempBuffer + SampleOffset * NumChannels;
	numGot = Min(numGot, MaxOutputSampleNum);

	FMemory::Memcpy(OutSamples, Flt, sizeof(float)*numGot*NumChannels);
	return (int32) numGot;
}

bool FSoundTouchFilter::HaveResiduals()
{
	return ResidualSampleBlockOffset != 0;
}

void FSoundTouchFilter::Reset()
{
	ST.Clear();
	EnqueuedSampleBlockInfos.Empty();
	NumProcessedThisCall = 0;
	ResidualSampleBlockOffset = 0;
	bTerminalBlockAdded = false;
}

#endif

} // namespace Electra
