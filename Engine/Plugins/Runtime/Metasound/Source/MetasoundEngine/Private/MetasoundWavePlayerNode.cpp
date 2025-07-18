// Copyright Epic Games, Inc. All Rights Reserved.

#include "DSP/BufferVectorOperations.h"
#include "DSP/ConvertDeinterleave.h"
#include "DSP/MultichannelBuffer.h"
#include "DSP/MultichannelLinearResampler.h"
#include "MetasoundBuildError.h"
#include "MetasoundBuilderInterface.h"
#include "MetasoundEngineNodesNames.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundLog.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundParamHelper.h"
#include "MetasoundPrimitives.h"
#include "MetasoundRenderCost.h"
#include "MetasoundTrace.h"
#include "MetasoundTrigger.h"
#include "MetasoundVertex.h"
#include "MetasoundWave.h"
#include "Sound/SoundWaveProxyReader.h"

#define LOCTEXT_NAMESPACE "MetasoundWaveNode"

namespace Metasound
{
	namespace WavePlayerVertexNames
	{
		METASOUND_PARAM(InputTriggerPlay, "Play", "Play the wave player.")
		METASOUND_PARAM(InputTriggerStop, "Stop", "Stop the wave player.")
		METASOUND_PARAM(InputWaveAsset, "Wave Asset", "The wave asset to be real-time decoded.")
		METASOUND_PARAM(InputStartTime, "Start Time", "Time into the wave asset to start (seek) the wave asset.")
		METASOUND_PARAM(InputPitchShift, "Pitch Shift", "The pitch shift to use for the wave asset in semitones.")
		METASOUND_PARAM(InputLoop, "Loop", "Whether or not to loop between the start and specified end times.")
		METASOUND_PARAM(InputLoopStart, "Loop Start", "When to start the loop.")
		METASOUND_PARAM(InputLoopDuration, "Loop Duration", "The duration of the loop when wave player is enabled for looping. A negative value will loop the whole wave asset.")
		
		METASOUND_PARAM(OutputTriggerOnPlay, "On Play", "Triggers when Play is triggered.")
		METASOUND_PARAM(OutputTriggerOnDone, "On Finished", "Triggers when the wave played has finished playing or Stop is triggered.")
		METASOUND_PARAM(OutputTriggerOnNearlyDone, "On Nearly Finished", "Triggers when the wave played has almost finished playing (the block before it finishes). Allows time for logic to trigger different variations to play seamlessly.")
		METASOUND_PARAM(OutputTriggerOnLooped, "On Looped", "Triggers when the wave player has looped.")
		METASOUND_PARAM(OutputTriggerOnCuePoint, "On Cue Point", "Triggers when a wave cue point was hit during playback.")
		METASOUND_PARAM(OutputCuePointID, "Cue Point ID", "The cue point ID that was triggered.")
		METASOUND_PARAM(OutputCuePointLabel, "Cue Point Label", "The cue point label that was triggered (if there was a label parsed in the imported .wav file).")
		METASOUND_PARAM(OutputLoopRatio, "Loop Percent", "Returns the current playback location as a ratio of the loop (0-1) if looping is enabled.")
		METASOUND_PARAM(OutputPlaybackLocation, "Playback Location", "Returns the absolute position of the wave playback as a ratio of wave duration (0-1).")
		METASOUND_PARAM(OutputPlaybackTime, "Playback Time", "Returns the current absolute playback time of the wave.")
		METASOUND_PARAM(OutputAudioMono, "Out Mono", "The mono channel audio output.")
		METASOUND_PARAM(OutputAudioLeft, "Out Left", "The left channel audio output.")
		METASOUND_PARAM(OutputAudioRight, "Out Right", "The right channel audio output.")
		METASOUND_PARAM(OutputAudioFrontRight, "Out Front Right", "The front right channel audio output.")
		METASOUND_PARAM(OutputAudioFrontLeft, "Out Front Left", "The front left channel audio output.")
		METASOUND_PARAM(OutputAudioFrontCenter, "Out Front Center", "The front center channel audio output.")
		METASOUND_PARAM(OutputAudioLowFrequency, "Out Low Frequency", "The low frequency channel audio output.")
		METASOUND_PARAM(OutputAudioSideRight, "Out Side Right", "The side right channel audio output.")
		METASOUND_PARAM(OutputAudioSideLeft, "Out Side Left", "The side left channel audio output.")
		METASOUND_PARAM(OutputAudioBackRight, "Out Back Right", "The back right channel audio output.")
		METASOUND_PARAM(OutputAudioBackLeft, "Out Back Left", "The back left channel audio output.")
	}

	namespace WavePlayerNodePrivate
	{
		constexpr int32 MaxNumFramesToInterpolateFrameRatio = 128;

		int32 GetCuePointFrame(const FSoundWaveCuePoint& InPoint)
		{
			return InPoint.FramePosition;
		}


		/** FSourceBufferState tracks the current frame and loop indices held in 
		 * a circular buffer. It describes how the content of a circular buffer
		 * relates to the frame indices of an FSoundWaveProxyReader 
		 *
		 * FSourceBufferState is tied to the implementation of the FSoundWaveProxyReader
		 * and TCircularAudioBuffer<>, and thus does not serve much purpose outside
		 * of this wave player node.
		 *
		 * However, it does provide a convenient place to perform frame counting
		 * arithmetic that would otherwise make code more difficult to read.
		 */
		struct FSourceBufferState
		{
			FSourceBufferState() = default;

			/** Construct a FSourceBufferState
			 *
			 * @param InStartFrameIndex - The frame index in the wave corresponding to the first frame in the circular buffer.
			 * @param InNumFrames - The number of frames in the circular buffer.
			 * @param bIsLooping - True if the wave player is looping, false if not.
			 * @param InLoopStartFrameIndexInWave - Frame index in the wave corresponding to a loop start.
			 * @param InLoopEndFrameIndexInWave - Frame index in the wave corresponding to a loop end.
			 * @param InEOFFrameIndexInWave - Frame index in the wave corresponding to the end of the file.
			 */
			FSourceBufferState(int32 InStartFrameIndex, int32 InNumFrames, bool bIsLooping, int32 InLoopStartFrameIndexInWave, int32 InLoopEndFrameIndexInWave, int32 InEOFFrameIndexInWave)
			{
				check(InStartFrameIndex >= 0);
				check(InNumFrames >= 0);
				check(InLoopStartFrameIndexInWave >= 0);
				check(InLoopEndFrameIndexInWave >= 0);
				check(InEOFFrameIndexInWave >= 0);

				StartFrameIndex = InStartFrameIndex;
				EndFrameIndex = InStartFrameIndex; // Initialize to starting frame index. Will be adjusted during call to Append()
				EOFFrameIndexInBuffer = InEOFFrameIndexInWave - InStartFrameIndex;
				LoopEndFrameIndexInBuffer = InLoopEndFrameIndexInWave - InStartFrameIndex;
				LoopStartFrameIndexInWave = InLoopStartFrameIndexInWave;
				LoopEndFrameIndexInWave = InLoopEndFrameIndexInWave;
				EOFFrameIndexInWave = InEOFFrameIndexInWave;

				Append(InNumFrames, bIsLooping);
			}

			/** Construct an FSourceBufferState
			 *
			 * @param ProxyReader - The wave proxy reader producing the audio.
			 * @parma InSourceBuffer - The audio buffer holding a range of samples popped from the reader.
			 */
			FSourceBufferState(const FSoundWaveProxyReader& ProxyReader, const Audio::FMultichannelCircularBuffer& InSourceBuffer)
			: FSourceBufferState(ProxyReader.GetFrameIndex(), Audio::GetMultichannelBufferNumFrames(InSourceBuffer), ProxyReader.IsLooping(), ProxyReader.GetLoopStartFrameIndex(), ProxyReader.GetLoopEndFrameIndex(), ProxyReader.GetNumFramesInWave())
			{
			}

			/** Track frames removed from the circular buffer. This generally coincides
			 * with a Pop(...) call to the circular buffer. */
			void Advance(int32 InNumFrames, bool bIsLooping)
			{
				check(InNumFrames >= 0);

				StartFrameIndex += InNumFrames;
				if (bIsLooping)
				{
					StartFrameIndex = WrapLoop(StartFrameIndex);
				}

				EOFFrameIndexInBuffer = EOFFrameIndexInWave - StartFrameIndex;
				LoopEndFrameIndexInBuffer = LoopEndFrameIndexInWave - StartFrameIndex;
			}


			/** Track frames appended to the source buffer. This generally coincides
			 * with a Push(...) call to the circular buffer. */
			void Append(int32 InNumFrames, bool bIsLooping)
			{
				check(InNumFrames >= 0);
				EndFrameIndex += InNumFrames;

				if (bIsLooping)
				{
					EndFrameIndex = WrapLoop(EndFrameIndex);
				}
			}

			/** Update loop frame indices. */
			void SetLoopFrameIndices(int32 InLoopStartFrameIndexInWave, int32 InLoopEndFrameIndexInWave)
			{
				LoopStartFrameIndexInWave = InLoopStartFrameIndexInWave;
				LoopEndFrameIndexInWave = InLoopEndFrameIndexInWave;
				LoopEndFrameIndexInBuffer = LoopEndFrameIndexInWave - StartFrameIndex;
			}

			/** Update loop frame indices. */
			void SetLoopFrameIndices(const FSoundWaveProxyReader& InProxyReader)
			{
				SetLoopFrameIndices(InProxyReader.GetLoopStartFrameIndex(), InProxyReader.GetLoopEndFrameIndex());
			}

			/** Returns the corresponding frame index in the wave which corresponds
			 * to the first frame in the circular buffer. 
			 */
			FORCEINLINE int32 GetStartFrameIndex() const
			{
				return StartFrameIndex;
			}

			/** Returns the corresponding frame index in the wave which corresponds
			 * to the end frame in the circular buffer (non-inclusive).
			 */
			FORCEINLINE int32 GetEndFrameIndex() const
			{
				return EndFrameIndex;
			}

			/** Returns the frame index in the wave where the loop starts */
			FORCEINLINE int32 GetLoopStartFrameIndexInWave() const
			{
				return LoopStartFrameIndexInWave;
			}

			/** Returns the frame index in the wave where the loop end*/
			FORCEINLINE int32 GetLoopEndFrameIndexInWave() const
			{
				return LoopEndFrameIndexInWave;
			}

			/** Returns the end-of-file frame index in the wave. */
			FORCEINLINE int32 GetEOFFrameIndexInWave() const
			{
				return EOFFrameIndexInWave;
			}

			/** Returns the frame index in the circular buffer which represents
			 * the end of file in the wave. */
			FORCEINLINE int32 GetEOFFrameIndexInBuffer() const
			{
				return EOFFrameIndexInBuffer;
			}

			/** Returns the frame index in the circular buffer which represents
			 * the ending loop frame index in the wave. */
			FORCEINLINE int32 GetLoopEndFrameIndexInBuffer() const
			{
				return LoopEndFrameIndexInBuffer;
			}

			/** Returns the ratio of the current frame index divided by the total
			 * number of frames in the wave. */
			FORCEINLINE float GetPlaybackFraction() const
			{
				const float PlaybackFraction = static_cast<float>(StartFrameIndex) / FMath::Max(static_cast<float>(EOFFrameIndexInWave), 1.f);
				return FMath::Max(0.f, PlaybackFraction);
			}

			/** Returns the ratio of the relative position of the current frame 
			 * index to the start loop frame index, divided by the number of frames
			 * in the loop.
			 * This value can be negative if the current frame index is less
			 * than the first loop frame index. 
			 */
			FORCEINLINE float GetLoopFraction() const
			{
				const float LoopNumFrames = static_cast<float>(FMath::Max(1, LoopEndFrameIndexInWave - LoopStartFrameIndexInWave));
				const float LoopRelativeLocation = static_cast<float>(StartFrameIndex - LoopStartFrameIndexInWave);

				return LoopRelativeLocation / LoopNumFrames;
			}

			/** Map a index representing a frame in a wave file to an index representing
			 * a frame in the associated circular buffer. */
			FORCEINLINE int32 MapFrameInWaveToFrameInBuffer(int32 InFrameIndexInWave, bool bIsLooping) const
			{
				if (!bIsLooping || (InFrameIndexInWave >= StartFrameIndex))
				{
					return InFrameIndexInWave - StartFrameIndex;
				}
				else
				{
					const int32 NumFramesFromStartToLoopEnd = LoopEndFrameIndexInWave - StartFrameIndex;
					const int32 NumFramesFromLoopStartToFrameIndex = InFrameIndexInWave - LoopStartFrameIndexInWave;
					return NumFramesFromStartToLoopEnd + NumFramesFromLoopStartToFrameIndex;
				}
			}

		private:

			int32 WrapLoop(int32 InSourceFrameIndex)
			{
				int32 Overshot = InSourceFrameIndex - LoopEndFrameIndexInWave;
				if (Overshot > 0)
				{
					InSourceFrameIndex = LoopStartFrameIndexInWave + Overshot;
				}
				return InSourceFrameIndex;
			}

			int32 StartFrameIndex = INDEX_NONE;
			int32 EndFrameIndex = INDEX_NONE;
			int32 EOFFrameIndexInBuffer = INDEX_NONE;
			int32 LoopEndFrameIndexInBuffer = INDEX_NONE;
			int32 EOFFrameIndexInWave = INDEX_NONE;
			int32 LoopStartFrameIndexInWave = INDEX_NONE;
			int32 LoopEndFrameIndexInWave = INDEX_NONE;
		};

		/** FSourceEvents contains the frame indices of wave events. 
		 * Indices are INDEX_NONE if they are unset. 
		 */
		struct FSourceEvents
		{
			/** Frame index of a loop end. */
			int32 OnLoopFrameIndex = INDEX_NONE;
			/** Frame index of an end of file. */
			int32 OnEOFFrameIndex = INDEX_NONE;
			/** Frame index of a cue points. */
			int32 OnCuePointFrameIndex = INDEX_NONE;
			/** Cue point associated with OnCuePointFrameIndex. */
			const FSoundWaveCuePoint* CuePoint = nullptr;

			/** Clear all frame indices and associated data. */
			void Reset()
			{
				OnLoopFrameIndex = INDEX_NONE;
				OnEOFFrameIndex = INDEX_NONE;
				OnCuePointFrameIndex = INDEX_NONE;
				CuePoint = nullptr;
			}
		};
	}

	struct FWavePlayerOpArgs
	{
		FOperatorSettings Settings;
		TArray<FOutputDataVertex> OutputAudioVertices;
		FTriggerReadRef PlayTrigger;
		FTriggerReadRef StopTrigger;
		FWaveAssetReadRef WaveAsset;
		FTimeReadRef StartTime;
		FFloatReadRef PitchShift;
		FBoolReadRef bLoop;
		FTimeReadRef LoopStartTime;
		FTimeReadRef LoopDuration;
		FNodeRenderCost CostReporter;
	};

	// Maximum decode size in frames. 
	static int32 MaxDecodeSizeInFrames = 1024;
	FAutoConsoleVariableRef CVarMetaSoundWavePlayerMaxDecodeSizeInFrames(
		TEXT("au.MetaSound.WavePlayer.MaxDecodeSizeInFrames"),
		MaxDecodeSizeInFrames,
		TEXT("Max size in frames used for decoding audio in the MetaSound wave player node.\n")
		TEXT("Default: 1024"),
		ECVF_Default);

	// Block size for deinterleaving audio. 
	static int32 DeinterleaveBlockSizeInFrames = 512;
	FAutoConsoleVariableRef CVarMetaSoundWavePlayerDeinterleaveBlockSizeInFrames(
		TEXT("au.MetaSound.WavePlayer.DeinterleaveBlockSizeInFrames"),
		DeinterleaveBlockSizeInFrames,
		TEXT("Block size in frames used for deinterleaving audio in the MetaSound wave player node.\n")
		TEXT("Default: 512"),
		ECVF_Default);

	namespace WavePlayerOperatorPrivate
	{
		template<typename AudioChannelConfigurationInfoType>
		FVertexInterface GetVertexInterface()
		{
			using namespace WavePlayerVertexNames;

			//Adds uncommonly used pins to the Advanced View, to reduce the size of the node.
			FDataVertexMetadata InputLoopStartMetaData = METASOUND_GET_PARAM_METADATA(InputLoopStart);
			InputLoopStartMetaData.bIsAdvancedDisplay = true;

			FDataVertexMetadata InputLoopDurationMetaData = METASOUND_GET_PARAM_METADATA(InputLoopDuration);
			InputLoopDurationMetaData.bIsAdvancedDisplay = true;

			FDataVertexMetadata OutputTriggerOnNearlyDoneMetaData = METASOUND_GET_PARAM_METADATA(OutputTriggerOnNearlyDone);
			OutputTriggerOnNearlyDoneMetaData.bIsAdvancedDisplay = true;

			FDataVertexMetadata OutputTriggerOnCuePointMetaData = METASOUND_GET_PARAM_METADATA(OutputTriggerOnCuePoint);
			OutputTriggerOnCuePointMetaData.bIsAdvancedDisplay = true;

			FDataVertexMetadata OutputCuePointIDMetaData = METASOUND_GET_PARAM_METADATA(OutputCuePointID);
			OutputCuePointIDMetaData.bIsAdvancedDisplay = true;

			FDataVertexMetadata OutputCuePointLabelMetaData = METASOUND_GET_PARAM_METADATA(OutputCuePointLabel);
			OutputCuePointLabelMetaData.bIsAdvancedDisplay = true;

			FDataVertexMetadata OutputPlaybackLocationMetaData = METASOUND_GET_PARAM_METADATA(OutputPlaybackLocation);
			OutputPlaybackLocationMetaData.bIsAdvancedDisplay = true;

			// Workaround to override display name of OutputLoopRatio
			FDataVertexMetadata OutputLoopRatioMetadata
			{
				METASOUND_GET_PARAM_TT(OutputLoopRatio), // description 
				METASOUND_LOCTEXT("OutputLoopRatioNotPercentDisplayName", "Loop Ratio"), // display name  
				true // Is Advanced Display
			};

			FVertexInterface VertexInterface(
				FInputVertexInterface(
					TInputDataVertex<FTrigger>(METASOUND_GET_PARAM_NAME_AND_METADATA(InputTriggerPlay)),
					TInputDataVertex<FTrigger>(METASOUND_GET_PARAM_NAME_AND_METADATA(InputTriggerStop)),
					TInputDataVertex<FWaveAsset>(METASOUND_GET_PARAM_NAME_AND_METADATA(InputWaveAsset)),
					TInputDataVertex<FTime>(METASOUND_GET_PARAM_NAME_AND_METADATA(InputStartTime), 0.0f),
					TInputDataVertex<float>(METASOUND_GET_PARAM_NAME_AND_METADATA(InputPitchShift), 0.0f),
					TInputDataVertex<bool>(METASOUND_GET_PARAM_NAME_AND_METADATA(InputLoop), false),
					TInputDataVertex<FTime>(METASOUND_GET_PARAM_NAME(InputLoopStart), InputLoopStartMetaData, 0.0f),
					TInputDataVertex<FTime>(METASOUND_GET_PARAM_NAME(InputLoopDuration), InputLoopDurationMetaData, -1.0f)
				),
				FOutputVertexInterface(
					TOutputDataVertex<FTrigger>(METASOUND_GET_PARAM_NAME_AND_METADATA(OutputTriggerOnPlay)),
					TOutputDataVertex<FTrigger>(METASOUND_GET_PARAM_NAME_AND_METADATA(OutputTriggerOnDone)),
					TOutputDataVertex<FTrigger>(METASOUND_GET_PARAM_NAME(OutputTriggerOnNearlyDone), OutputTriggerOnNearlyDoneMetaData),
					TOutputDataVertex<FTrigger>(METASOUND_GET_PARAM_NAME_AND_METADATA(OutputTriggerOnLooped)),
					TOutputDataVertex<FTrigger>(METASOUND_GET_PARAM_NAME(OutputTriggerOnCuePoint), OutputTriggerOnCuePointMetaData),
					TOutputDataVertex<int32>(METASOUND_GET_PARAM_NAME(OutputCuePointID), OutputCuePointIDMetaData),
					TOutputDataVertex<FString>(METASOUND_GET_PARAM_NAME(OutputCuePointLabel), OutputCuePointLabelMetaData),
					TOutputDataVertex<float>(METASOUND_GET_PARAM_NAME(OutputLoopRatio), OutputLoopRatioMetadata),
					TOutputDataVertex<float>(METASOUND_GET_PARAM_NAME(OutputPlaybackLocation), OutputPlaybackLocationMetaData),
					TOutputDataVertex<FTime>(METASOUND_GET_PARAM_NAME_AND_METADATA(OutputPlaybackTime))
				)
			);

			// Add audio outputs dependent upon source info.
			for (const FOutputDataVertex& OutputDataVertex : AudioChannelConfigurationInfoType::GetAudioOutputs())
			{
				VertexInterface.GetOutputInterface().Add(OutputDataVertex);
			}

			return VertexInterface;
		}

		template<typename AudioChannelConfigurationInfoType>
		FNodeClassMetadata GetNodeInfo()
		{
			FNodeClassMetadata Info;
			Info.ClassName = { Metasound::EngineNodes::Namespace, TEXT("Wave Player"), AudioChannelConfigurationInfoType::GetVariantName() };
			Info.MajorVersion = 1;
			Info.MinorVersion = 0;
			Info.DisplayName = AudioChannelConfigurationInfoType::GetNodeDisplayName();
			Info.Description = METASOUND_LOCTEXT("Metasound_WavePlayerNodeDescription", "Plays a wave asset. The wave's channel configurations will be up or down mixed to match the wave players audio channel format.");
			Info.Author = PluginAuthor;
			Info.PromptIfMissing = PluginNodeMissingPrompt;
			Info.DefaultInterface = GetVertexInterface<AudioChannelConfigurationInfoType>();
			Info.Keywords = { METASOUND_LOCTEXT("WavePlayerSoundKeyword", "Sound"),
				                METASOUND_LOCTEXT("WavePlayerCueKeyword", "Cue")
			};

			return Info;
		}
	}

	/** MetaSound operator for the wave player node. */
	class FWavePlayerOperator : public TExecutableOperator<FWavePlayerOperator>
	{	
	public:

		// Maximum absolute pitch shift in octaves. 
		static constexpr float MaxAbsPitchShiftInOctaves = 6.0f;

		FWavePlayerOperator(const FWavePlayerOpArgs& InArgs)
			: OperatorSettings(InArgs.Settings)
			, PlayTrigger(InArgs.PlayTrigger)
			, StopTrigger(InArgs.StopTrigger)
			, WaveAsset(InArgs.WaveAsset)
			, StartTime(InArgs.StartTime)
			, PitchShift(InArgs.PitchShift)
			, bLoop(InArgs.bLoop)
			, LoopStartTime(InArgs.LoopStartTime)
			, LoopDuration(InArgs.LoopDuration)
			, TriggerOnDone(FTriggerWriteRef::CreateNew(InArgs.Settings))
			, TriggerOnNearlyDone(FTriggerWriteRef::CreateNew(InArgs.Settings))
			, TriggerOnLooped(FTriggerWriteRef::CreateNew(InArgs.Settings))
			, TriggerOnCuePoint(FTriggerWriteRef::CreateNew(InArgs.Settings))
			, CuePointID(FInt32WriteRef::CreateNew(0))
			, CuePointLabel(FStringWriteRef::CreateNew(TEXT("")))
			, LoopPercent(FFloatWriteRef::CreateNew(0.0f))
			, PlaybackLocation(FFloatWriteRef::CreateNew(0.0f))
			, PlaybackTime(FTimeWriteRef::CreateNew(0.0))
			, CostReporter(InArgs.CostReporter)
		{
			NumOutputChannels = InArgs.OutputAudioVertices.Num();

			for (const FOutputDataVertex& OutputAudioVertex : InArgs.OutputAudioVertices)
			{
				OutputAudioBufferVertexNames.Add(OutputAudioVertex.VertexName);

				FAudioBufferWriteRef AudioBuffer = FAudioBufferWriteRef::CreateNew(InArgs.Settings);
				OutputAudioBuffers.Add(AudioBuffer);

				// Hold on to a view of the output audio. Audio buffers are only writable
				// by this object and will not be reallocated. 
				OutputAudioView.Emplace(AudioBuffer->GetData(), AudioBuffer->Num());
			}
		}

		virtual void BindInputs(FInputVertexInterfaceData& InOutVertexData) override
		{
			using namespace WavePlayerVertexNames;
			
			InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(InputTriggerPlay), PlayTrigger);
			InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(InputTriggerStop), StopTrigger);
			InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(InputWaveAsset), WaveAsset);
			InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(InputStartTime), StartTime);
			InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(InputPitchShift), PitchShift);
			InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(InputLoop), bLoop);
			InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(InputLoopStart), LoopStartTime);
			InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(InputLoopDuration), LoopDuration);
		}

		virtual void BindOutputs(FOutputVertexInterfaceData& InOutVertexData) override
		{
			using namespace WavePlayerVertexNames;

			InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(OutputTriggerOnPlay), PlayTrigger);
			InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(OutputTriggerOnDone), TriggerOnDone);
			InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(OutputTriggerOnNearlyDone), TriggerOnNearlyDone);
			InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(OutputTriggerOnLooped), TriggerOnLooped);
			InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(OutputTriggerOnCuePoint), TriggerOnCuePoint);
			InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(OutputCuePointID), CuePointID);
			InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(OutputCuePointLabel), CuePointLabel);
			InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(OutputLoopRatio), LoopPercent);
			InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(OutputPlaybackLocation), PlaybackLocation);
			InOutVertexData.BindReadVertex(METASOUND_GET_PARAM_NAME(OutputPlaybackTime), PlaybackTime);

			check(OutputAudioBuffers.Num() == OutputAudioBufferVertexNames.Num());

			for (int32 i = 0; i < OutputAudioBuffers.Num(); i++)
			{
				InOutVertexData.BindReadVertex(OutputAudioBufferVertexNames[i], OutputAudioBuffers[i]);
			}
		}

		void Execute()
		{
			using namespace WavePlayerNodePrivate;

			METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(Metasound::FWavePlayerOperator::Execute);

			// Advance all triggers owned by this operator. 
			TriggerOnDone->AdvanceBlock();
			TriggerOnNearlyDone->AdvanceBlock();
			TriggerOnCuePoint->AdvanceBlock();
			TriggerOnLooped->AdvanceBlock();

			// Reset flag to track render cost.
			bDidWaveRenderThisBlock = false;

			// Update wave proxy reader with any new looping bounds. 
			if (WaveProxyReader.IsValid())
			{
				WaveProxyReader->SetIsLooping(*bLoop);
				WaveProxyReader->SetLoopStartTime(LoopStartTime->GetSeconds());
				WaveProxyReader->SetLoopDuration(LoopDuration->GetSeconds());
				SourceState.SetLoopFrameIndices(*WaveProxyReader);
			}

			// Update resampler with new frame ratio. 
			if (Resampler.IsValid())
			{
				Resampler->SetFrameRatio(GetFrameRatio(), FMath::Min(OperatorSettings.GetNumFramesPerBlock(), MaxNumFramesToInterpolateFrameRatio));
			}

			// zero output buffers
			for (const FAudioBufferWriteRef& OutputBuffer : OutputAudioBuffers)
			{
				FMemory::Memzero(OutputBuffer->GetData(), OperatorSettings.GetNumFramesPerBlock() * sizeof(float));
			}

			// Performs execution per sub block based on triggers.
			ExecuteSubblocks();

			// Updates output playhead information
			UpdatePlaybackLocation();

			if (bDidWaveRenderThisBlock)
			{
				CostReporter.SetRenderCost(1.f);
			}
		}

		void Reset(const IOperator::FResetParams& InParams)
		{
			TriggerOnDone->Reset();
			TriggerOnNearlyDone->Reset();
			TriggerOnLooped->Reset();
			TriggerOnCuePoint->Reset();

			*CuePointID = 0;
			*CuePointLabel = TEXT("");
			*LoopPercent = 0;
			*PlaybackLocation = 0;
			*PlaybackTime = FTime(0.0);
			for (const FAudioBufferWriteRef& BufferRef : OutputAudioBuffers)
			{
				BufferRef->Zero();
			}

			WaveProxyReader.Reset();
			ConvertDeinterleave.Reset();
			Resampler.Reset();

			SortedCuePoints.Reset();
			for (Audio::TCircularAudioBuffer<float>& Buffer : SourceCircularBuffer)
			{
				Buffer.Empty();
			}

			SourceState = WavePlayerNodePrivate::FSourceBufferState();
			SampleRateFrameRatio = 1.f;
			bOnNearlyDoneTriggeredForWave = false;
			bIsPlaying = false;
		}

	private:

		void ExecuteSubblocks()
		{
			// Parse triggers and render audio
			int32 PlayTrigIndex = 0;
			int32 NextPlayFrame = 0;
			const int32 NumPlayTrigs = PlayTrigger->NumTriggeredInBlock();

			int32 StopTrigIndex = 0;
			int32 NextStopFrame = 0;
			const int32 NumStopTrigs = StopTrigger->NumTriggeredInBlock();

			int32 CurrAudioFrame = 0;
			int32 NextAudioFrame = 0;
			const int32 LastAudioFrame = OperatorSettings.GetNumFramesPerBlock() - 1;
			const int32 NoTrigger = OperatorSettings.GetNumFramesPerBlock() << 1;

			while (NextAudioFrame < LastAudioFrame)
			{
				// get the next Play and Stop indices
				// (play)
				if (PlayTrigIndex < NumPlayTrigs)
				{
					NextPlayFrame = (*PlayTrigger)[PlayTrigIndex];
				}
				else
				{
					NextPlayFrame = NoTrigger;
				}

				// (stop)
				if (StopTrigIndex < NumStopTrigs)
				{
					NextStopFrame = (*StopTrigger)[StopTrigIndex];
				}
				else
				{
					NextStopFrame = NoTrigger;
				}

				// determine the next audio frame we are going to render up to
				NextAudioFrame = FMath::Min(NextPlayFrame, NextStopFrame);

				// no more triggers, rendering to the end of the block
				if (NextAudioFrame == NoTrigger)
				{
					NextAudioFrame = OperatorSettings.GetNumFramesPerBlock();
				}

				// render audio (while loop handles looping audio)
				while (CurrAudioFrame != NextAudioFrame)
				{
					if (bIsPlaying)
					{
						RenderFrameRange(CurrAudioFrame, NextAudioFrame);
					}
					CurrAudioFrame = NextAudioFrame;
				}

				// execute the next trigger
				if (CurrAudioFrame == NextPlayFrame)
				{
					if (!StartPlaying())
					{
						TriggerOnDone->TriggerFrame(CurrAudioFrame);
					}

					++PlayTrigIndex;
				}

				if (CurrAudioFrame == NextStopFrame)
				{
					bIsPlaying = false;
					TriggerOnDone->TriggerFrame(CurrAudioFrame);
					++StopTrigIndex;
				}
			}
		}

		void RenderFrameRange(int32 StartFrame, int32 EndFrame)
		{
			using namespace Audio;

			METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(Metasound::FWavePlayerOperator::RenderFrameRange);

			// Set flag that this node rendered wave audio this block.
			bDidWaveRenderThisBlock = true;

			// Assume this is set to true and checked by outside callers
			check(bIsPlaying);

			const int32 NumFramesToGenerate = EndFrame - StartFrame;
			if (NumFramesToGenerate > 0)
			{
				// Trigger any events that occur within this frame range
				TriggerUpcomingEvents(StartFrame, NumFramesToGenerate, SourceState);

				// Generate audio
				FMultichannelBufferView BufferToGenerate = SliceMultichannelBufferView(OutputAudioView, StartFrame, NumFramesToGenerate);
				GeneratePitchedAudio(BufferToGenerate);

				// Check if the source is empty.
				if (!(*bLoop))
				{
					bIsPlaying = (SourceState.GetStartFrameIndex() <= SourceState.GetEOFFrameIndexInWave());
				}
			}
		}

		void UpdatePlaybackLocation()
		{
			*PlaybackLocation = SourceState.GetPlaybackFraction();

			if (WaveProxyReader.IsValid())
			{
				*PlaybackTime = FTime::FromSeconds(static_cast<double>(SourceState.GetStartFrameIndex()) / static_cast<double>(FMath::Max(UE_SMALL_NUMBER, WaveProxyReader->GetSampleRate())));
			}
			else
			{
				*PlaybackTime = FTime(0.0);
			}

			if (*bLoop)
			{
				*LoopPercent = SourceState.GetLoopFraction();
			}
			else
			{
				*LoopPercent = 0.f;
			}
		}

		float GetPitchShiftClamped() const
		{
			return FMath::Clamp(*PitchShift, -12.0f * MaxAbsPitchShiftInOctaves, 12.0f * MaxAbsPitchShiftInOctaves);
		}

		float GetPitchShiftFrameRatio() const
		{
			return FMath::Pow(2.0f, GetPitchShiftClamped() / 12.0f);
		}

		// Updates the sample rate frame ratio. Used when a new wave proxy reader
		// is created. 
		void UpdateSampleRateFrameRatio() 
		{
			SampleRateFrameRatio = 1.f;

			if (WaveProxyReader.IsValid())
			{
				float SourceSampleRate = WaveProxyReader->GetSampleRate();
				if (SourceSampleRate > 0.f)
				{
					float TargetSampleRate = OperatorSettings.GetSampleRate();
					if (TargetSampleRate > 0.f)
					{
						SampleRateFrameRatio = SourceSampleRate / OperatorSettings.GetSampleRate();
					}
				}
			}
		}

		float GetSampleRateFrameRatio() const
		{
			return SampleRateFrameRatio;
		}

		float GetFrameRatio() const
		{
			using namespace Audio;
			return FMath::Clamp(GetSampleRateFrameRatio() * GetPitchShiftFrameRatio(), FMultichannelLinearResampler::MinFrameRatio, FMultichannelLinearResampler::MaxFrameRatio);
		}

		float GetMaxPitchShiftFrameRatio() const
		{
			return FMath::Pow(2.0f, MaxAbsPitchShiftInOctaves);
		}

		float GetMaxFrameRatio() const
		{
			return GetSampleRateFrameRatio() * GetMaxPitchShiftFrameRatio();
		}

		// Start playing the current wave by creating a wave proxy reader and
		// recreating the DSP stack.
		bool StartPlaying()
		{
			using namespace WavePlayerNodePrivate;
			METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(Metasound::FWavePlayerOperator::StartPlaying);

			// MetasoundWavePlayerNode DSP Stack
			//
			// Legend:
			// 	[ObjectName] - An Object which generates or process audio.
			// 	(BufferName) - A buffer which holds audio.
			//
			// [WaveProxyReader]->(InterleavedBuffer)->[ConvertDeinterleave]->(DeinterleavedBuffer)->(SourceCircularBuffer)->[Resampler]->(AudioOutputView)
			//

			// Copy the wave asset off on init in case the user changes it while we're playing it.
			// We'll only check for new wave assets when the current one finishes for sample accurate concantenation
			CurrentWaveAsset = *WaveAsset;
			FSoundWaveProxyPtr WaveProxy = CurrentWaveAsset.GetSoundWaveProxy();

			bOnNearlyDoneTriggeredForWave = false;
			bIsPlaying = false;

			// Reset dsp stack.
			ResetSourceBufferAndState();
			WaveProxyReader.Reset();
			ConvertDeinterleave.Reset();
			Resampler.Reset();
			SortedCuePoints.Reset();

			if (WaveProxy.IsValid())
			{
				UE_LOG(LogMetaSound, Verbose, TEXT("Starting Sound: '%s'"), *CurrentWaveAsset->GetFName().ToString());

				// Create local sorted copy of cue points and loop regions.
				SortedCuePoints = WaveProxy->GetCuePointsAndLoopRegions();
				Algo::SortBy(SortedCuePoints, WavePlayerNodePrivate::GetCuePointFrame);
				
				// Create the wave proxy reader.
				FSoundWaveProxyReader::FSettings WaveReaderSettings;
				WaveReaderSettings.MaxDecodeSizeInFrames = FMath::IsPowerOfTwo(MaxDecodeSizeInFrames) ? 
					MaxDecodeSizeInFrames : FMath::RoundUpToPowerOfTwo(MaxDecodeSizeInFrames);

				WaveReaderSettings.StartTimeInSeconds = StartTime->GetSeconds();
				WaveReaderSettings.LoopStartTimeInSeconds = LoopStartTime->GetSeconds();
				WaveReaderSettings.LoopDurationInSeconds = LoopDuration->GetSeconds(); 
				WaveReaderSettings.bIsLooping = *bLoop;

				WaveProxyReader = FSoundWaveProxyReader::Create(WaveProxy.ToSharedRef(), WaveReaderSettings);

				DeinterleaveBufferBlockSizeInFrames = FMath::IsPowerOfTwo(DeinterleaveBlockSizeInFrames) ? 
					DeinterleaveBlockSizeInFrames : FMath::RoundUpToPowerOfTwo(DeinterleaveBlockSizeInFrames);

				if (WaveProxyReader.IsValid())
				{
					UpdateSampleRateFrameRatio();
					int32 WaveProxyNumChannels = WaveProxyReader->GetNumChannels();

					if (WaveProxyNumChannels > 0)
					{
						// Create buffer for interleaved audio
						int32 InterleavedBufferNumSamples = WaveProxyNumChannels * DeinterleaveBufferBlockSizeInFrames;
						InterleavedBuffer.Reset(InterleavedBufferNumSamples);
						InterleavedBuffer.AddUninitialized(InterleavedBufferNumSamples);

						NumDeinterleaveChannels = NumOutputChannels;

						// Create algorithm for channel conversion and deinterleave 
						Audio::FConvertDeinterleaveParams ConvertDeinterleaveParams;
						ConvertDeinterleaveParams.NumInputChannels = WaveProxyReader->GetNumChannels();
						ConvertDeinterleaveParams.NumOutputChannels = NumDeinterleaveChannels;
						// Original implementation of MetaSound WavePlayer upmixed 
						// mono using FullVolume. In the future, the mono upmix 
						// method may be exposed as a node input to facilitate 
						// better control.
						ConvertDeinterleaveParams.MonoUpmixMethod = Audio::EChannelMapMonoUpmixMethod::FullVolume;
						ConvertDeinterleave = Audio::IConvertDeinterleave::Create(ConvertDeinterleaveParams);
						Audio::SetMultichannelBufferSize(NumDeinterleaveChannels, DeinterleaveBufferBlockSizeInFrames, DeinterleavedBuffer);

						// Initialize source buffer
						int32 FrameCapacity = DeinterleaveBufferBlockSizeInFrames + FMath::CeilToInt(GetMaxFrameRatio() * OperatorSettings.GetNumFramesPerBlock());
						Audio::SetMultichannelCircularBufferCapacity(NumOutputChannels, FrameCapacity, SourceCircularBuffer);
						SourceState = FSourceBufferState(*WaveProxyReader, SourceCircularBuffer);

						// Create a resampler.
						Resampler = MakeUnique<Audio::FMultichannelLinearResampler>(NumDeinterleaveChannels);
						Resampler->SetFrameRatio(GetFrameRatio(), 0 /* NumFramesToInperolate */);

						// Need to add upmixing if this is not true
						check(NumDeinterleaveChannels == NumOutputChannels);
					}
				}
			}

			// If everything was created successfully, start playing.
			bIsPlaying = WaveProxyReader.IsValid() && ConvertDeinterleave.IsValid() && Resampler.IsValid();
			return bIsPlaying;
		}

		/** Removes all samples from the source buffer and resets SourceState. */
		void ResetSourceBufferAndState()
		{
			using namespace WavePlayerNodePrivate;
			using namespace Audio;

			SourceState = FSourceBufferState();
			for (TCircularAudioBuffer<float>& ChannelCircularBuffer : SourceCircularBuffer)
			{
				ChannelCircularBuffer.SetNum(0);
			}
		}

		/** Generates audio from the wave proxy reader.
		 *
		 * @param OutBuffer - Buffer to place generated audio.
		 * @param OutSourceState - Source state for tracking state of OutBuffer.
		 * Returns true if succesfully generated audio. 
		 */
		void GenerateSourceAudio(Audio::FMultichannelCircularBuffer& OutBuffer, WavePlayerNodePrivate::FSourceBufferState& OutSourceState)
		{
			using namespace WavePlayerNodePrivate;

			if (bIsPlaying)
			{
				const int32 NumExistingFrames = Audio::GetMultichannelBufferNumFrames(OutBuffer);
				const int32 NumSamplesToGenerate = DeinterleaveBufferBlockSizeInFrames * WaveProxyReader->GetNumChannels();
 				check(NumSamplesToGenerate == InterleavedBuffer.Num())

				// if the wave proxy reader has failed, write out silence. 
				if (WaveProxyReader->HasFailed())
				{
					FMemory::Memset(InterleavedBuffer.GetData(), 0, sizeof(float) * InterleavedBuffer.Num());
				}
				else
				{
					WaveProxyReader->PopAudio(InterleavedBuffer);
				}

				ConvertDeinterleave->ProcessAudio(InterleavedBuffer, DeinterleavedBuffer);

				for (int32 ChannelIndex = 0; ChannelIndex < NumDeinterleaveChannels; ChannelIndex++)
				{
					OutBuffer[ChannelIndex].Push(DeinterleavedBuffer[ChannelIndex]);
				}
				OutSourceState.Append(DeinterleaveBufferBlockSizeInFrames, *bLoop);
			}
			else
			{
				OutSourceState = FSourceBufferState();
			}
		}


		/** Updates frame indices of events if the event occurs in the source within
		 * the frame range. The frame range begins with the start frame in InSourceState
		 * and continues for InNumSourceFrames in the source buffer. 
		 *
		 * @param InSourceState - Description of the current state of the source buffer.
		 * @param InNumSourceFrames - Number of frames to inspect.
		 * @param OutEvents - Event structure to fill out. 
		 */
		void MapSourceEventsIfInRange(const WavePlayerNodePrivate::FSourceBufferState& InSourceState, int32 InNumSourceFrames, WavePlayerNodePrivate::FSourceEvents& OutEvents)
		{
			OutEvents.Reset();

			// Loop end
			if (*bLoop && FMath::IsWithin(SourceState.GetLoopEndFrameIndexInBuffer(), 0, InNumSourceFrames))
			{
				OutEvents.OnLoopFrameIndex = FMath::RoundToInt(Resampler->MapInputFrameToOutputFrame(SourceState.GetLoopEndFrameIndexInBuffer()));
			}

			// End of file
			if (FMath::IsWithin(SourceState.GetEOFFrameIndexInBuffer(), 0, InNumSourceFrames))
			{
				OutEvents.OnEOFFrameIndex = FMath::RoundToInt(Resampler->MapInputFrameToOutputFrame(SourceState.GetEOFFrameIndexInBuffer()));
			}

			// Map Cue point. Since only one can be mapped, map the first one found.
			// The first cue point found has the best chance of being rendered.
			int32 SearchStartFrameIndexInWave = InSourceState.GetStartFrameIndex();
			int32 SearchEndFrameIndexInWave = SearchStartFrameIndexInWave + InNumSourceFrames;

			const bool bFramesCrossLoopBoundary = *bLoop && (OutEvents.OnLoopFrameIndex != INDEX_NONE);
			if (bFramesCrossLoopBoundary)
			{
				SearchEndFrameIndexInWave = SearchStartFrameIndexInWave + SourceState.GetLoopEndFrameIndexInBuffer();
			}

			OutEvents.CuePoint = FindCuePoint(SearchStartFrameIndexInWave, SearchEndFrameIndexInWave);

			if (bFramesCrossLoopBoundary)
			{
				SearchStartFrameIndexInWave = SourceState.GetLoopStartFrameIndexInWave();
				int32 RemainingFrames = InNumSourceFrames - SourceState.GetLoopEndFrameIndexInBuffer();
				SearchEndFrameIndexInWave = RemainingFrames;

				// Only override OutEvents.CuePoint if one exists in this subsection
				// of the buffer.
				if (const FSoundWaveCuePoint* CuePoint = FindCuePoint(SearchStartFrameIndexInWave, SearchEndFrameIndexInWave))
				{
					if (nullptr == OutEvents.CuePoint)
					{
						OutEvents.CuePoint = CuePoint;
					}
					else 
					{
						UE_LOG(LogMetaSound, Verbose, TEXT("Skipping cue point \"%s\" at frame %d due to multiple cue points in same render block"), *CuePoint->Label, CuePoint->FramePosition);
					}
				}
			}

			if (nullptr != OutEvents.CuePoint)
			{
				int32 CuePointFrameInBuffer = SourceState.MapFrameInWaveToFrameInBuffer(OutEvents.CuePoint->FramePosition, *bLoop);
				OutEvents.OnCuePointFrameIndex = FMath::RoundToInt(Resampler->MapInputFrameToOutputFrame(CuePointFrameInBuffer));
			}
		}

		// Search for cue points in frame range. Return the first cue point in the frame range.
		const FSoundWaveCuePoint* FindCuePoint(int32 InStartFrameInWave, int32 InEndFrameInWave)
		{
			int32 LowerBoundIndex = Algo::LowerBoundBy(SortedCuePoints, InStartFrameInWave, WavePlayerNodePrivate::GetCuePointFrame);
			int32 UpperBoundIndex = Algo::LowerBoundBy(SortedCuePoints, InEndFrameInWave, WavePlayerNodePrivate::GetCuePointFrame);

			if (LowerBoundIndex < UpperBoundIndex)
			{
				// Inform about skipped cue points. 
				for (int32 i = LowerBoundIndex + 1; i < UpperBoundIndex; i++)
				{
					const FSoundWaveCuePoint& CuePoint = SortedCuePoints[i];

					UE_LOG(LogMetaSound, Verbose, TEXT("Skipping cue point \"%s\" at frame %d due to multiple cue points in same render block"), *CuePoint.Label, CuePoint.FramePosition);
				}
				return &SortedCuePoints[LowerBoundIndex];
			}

			return nullptr;
		}

		// Check the expected output positions for various sample accurate events
		// before resampling. 
		//
		// Note: The resampler can only accurately map samples *before* processing 
		// audio because processing audio modifies the internal state of the resampler.
		void TriggerUpcomingEvents(int32 InOperatorStartFrame, int32 InNumFrames, const WavePlayerNodePrivate::FSourceBufferState& InState)
		{
			WavePlayerNodePrivate::FSourceEvents Events;

			// Check extra frames to hit the 
			const int32 NumOutputFramesToCheck = (2 * OperatorSettings.GetNumFramesPerBlock() + 1) - InOperatorStartFrame;
			const int32 NumSourceFramesToCheck = FMath::CeilToInt(Resampler->MapOutputFrameToInputFrame(NumOutputFramesToCheck));

			// Selectively map events in the source buffer to frame indices in the
			// resampled output buffer. 
			MapSourceEventsIfInRange(SourceState, NumSourceFramesToCheck, Events);

			// Check whether to trigger loops based on actual number of output frames 
			if (*bLoop)
			{
				if (FMath::IsWithin(Events.OnLoopFrameIndex, 0, InNumFrames))
				{
					TriggerOnLooped->TriggerFrame(InOperatorStartFrame + Events.OnLoopFrameIndex);
				}
			}
			else
			{
				const int32 IsNearlyDoneStartFrameIndex = OperatorSettings.GetNumFramesPerBlock() - InOperatorStartFrame;
				const int32 IsNearlyDoneEndFrameIndex = IsNearlyDoneStartFrameIndex + OperatorSettings.GetNumFramesPerBlock();

				if (FMath::IsWithin(Events.OnEOFFrameIndex, 0, InNumFrames))
				{
					TriggerOnDone->TriggerFrame(InOperatorStartFrame + Events.OnEOFFrameIndex);
				}
				else if (FMath::IsWithin(Events.OnEOFFrameIndex, IsNearlyDoneStartFrameIndex, IsNearlyDoneEndFrameIndex))
				{
					// Protect against triggering OnNearlyDone multiple times
					// in the scenario where significant pitch shift changes drastically
					// alter the predicted OnDone frame between render blocks.
					if (!bOnNearlyDoneTriggeredForWave)
					{
						TriggerOnNearlyDone->TriggerFrame(InOperatorStartFrame + Events.OnEOFFrameIndex - OperatorSettings.GetNumFramesPerBlock());
						bOnNearlyDoneTriggeredForWave = true;
					}
				}
			}

			if (nullptr != Events.CuePoint)
			{
				if (FMath::IsWithin(Events.OnCuePointFrameIndex, 0, InNumFrames))
				{
					if (!TriggerOnCuePoint->IsTriggeredInBlock())
					{
						*CuePointID = Events.CuePoint->CuePointID;
						*CuePointLabel = Events.CuePoint->Label;
						TriggerOnCuePoint->TriggerFrame(InOperatorStartFrame + Events.OnCuePointFrameIndex);
					}
					else
					{
						UE_LOG(LogMetaSound, Verbose, TEXT("Skipping cue point \"%s\" at frame %d due to multiple cue points in same render block"), *Events.CuePoint->Label, Events.CuePoint->FramePosition);
					}
				}
			}
		}


		void GeneratePitchedAudio(Audio::FMultichannelBufferView& OutBuffer)
		{
			using namespace Audio;

			// Outside callers should ensure that bIsPlaying is true if calling this function.
			check(bIsPlaying);

			int32 NumFramesRequested = GetMultichannelBufferNumFrames(OutBuffer);
			int32 NumSourceFramesAvailable = GetMultichannelBufferNumFrames(SourceCircularBuffer);

			while (NumFramesRequested > 0)
			{
				// Determine how many frames are needed to produce the output.
				int32 NumSourceFramesNeeded = Resampler->GetNumInputFramesNeededToProduceOutputFrames(NumFramesRequested + 1);
				if (NumSourceFramesNeeded > NumSourceFramesAvailable)
				{
					// Generate more source audio, but still may not be enough to produce all requested frames.
					GenerateSourceAudio(SourceCircularBuffer, SourceState);
				}
				NumSourceFramesAvailable = GetMultichannelBufferNumFrames(SourceCircularBuffer);

				// Resample frames. 
				int32 NumFramesProduced = Resampler->ProcessAndConsumeAudio(SourceCircularBuffer, OutBuffer);
				if (NumFramesProduced < 1)
				{
					UE_LOG(LogMetaSound, Error, TEXT("Aborting currently playing metasound wave %s. Failed to produce any resampled audio frames with %d input frames and a frame ratio of %f."), *CurrentWaveAsset->GetFName().ToString(), NumSourceFramesAvailable, GetFrameRatio());
					bIsPlaying = false;
					break;
				}

				// Update sample counters
				int32 NewNumSourceFramesAvailable = GetMultichannelBufferNumFrames(SourceCircularBuffer);
				int32 NumSourceFramesConsumed = NumSourceFramesAvailable - NewNumSourceFramesAvailable;
				NumSourceFramesAvailable = NewNumSourceFramesAvailable;
				NumFramesRequested -= NumFramesProduced;

				SourceState.Advance(NumSourceFramesConsumed, *bLoop);

				// Shift buffer if there are more samples to create
				if (NumFramesRequested > 0)
				{
					ShiftMultichannelBufferView(NumFramesProduced, OutBuffer);
				}
			}
		}

		const FOperatorSettings OperatorSettings;

		// i/o
		FTriggerReadRef PlayTrigger;
		FTriggerReadRef StopTrigger;
		FWaveAssetReadRef WaveAsset;
		FTimeReadRef StartTime;
		FFloatReadRef PitchShift;
		FBoolReadRef bLoop;
		FTimeReadRef LoopStartTime;
		FTimeReadRef LoopDuration;

		FTriggerWriteRef TriggerOnDone;
		FTriggerWriteRef TriggerOnNearlyDone;
		FTriggerWriteRef TriggerOnLooped;
		FTriggerWriteRef TriggerOnCuePoint;
		FInt32WriteRef CuePointID;
		FStringWriteRef CuePointLabel;
		FFloatWriteRef LoopPercent;
		FFloatWriteRef PlaybackLocation;
		FTimeWriteRef PlaybackTime;
		TArray<FAudioBufferWriteRef> OutputAudioBuffers;
		TArray<FName> OutputAudioBufferVertexNames;
		FNodeRenderCost CostReporter;

		TUniquePtr<FSoundWaveProxyReader> WaveProxyReader;
		TUniquePtr<Audio::IConvertDeinterleave> ConvertDeinterleave;
		TUniquePtr<Audio::FMultichannelLinearResampler> Resampler;

		FWaveAsset CurrentWaveAsset;
		TArray<FSoundWaveCuePoint> SortedCuePoints;
		Audio::FAlignedFloatBuffer InterleavedBuffer;
		Audio::FMultichannelBuffer DeinterleavedBuffer;
		Audio::FMultichannelCircularBuffer SourceCircularBuffer;
		Audio::FMultichannelBufferView OutputAudioView;

		WavePlayerNodePrivate::FSourceBufferState SourceState;
		float SampleRateFrameRatio = 1.f;
		int32 NumOutputChannels;
		int32 NumDeinterleaveChannels;
		bool bOnNearlyDoneTriggeredForWave = false;
		bool bIsPlaying = false;
		bool bDidWaveRenderThisBlock = false;
		// Cached from cvar 
		int32 DeinterleaveBufferBlockSizeInFrames;
	};

	class FWavePlayerOperatorFactory : public IOperatorFactory
	{
	public:
		FWavePlayerOperatorFactory(const TArray<FOutputDataVertex>& InOutputAudioVertices)
		: OutputAudioVertices(InOutputAudioVertices)
		{
		}

		virtual TUniquePtr<IOperator> CreateOperator(const FBuildOperatorParams& InParams, FBuildResults& OutResults) override
		{
			using namespace Audio;
			using namespace WavePlayerVertexNames;

			const FInputVertexInterfaceData& Inputs = InParams.InputData;

			FWavePlayerOpArgs Args =
			{
				InParams.OperatorSettings,
				OutputAudioVertices,
				Inputs.GetOrCreateDefaultDataReadReference<FTrigger>(METASOUND_GET_PARAM_NAME(InputTriggerPlay), InParams.OperatorSettings),
				Inputs.GetOrCreateDefaultDataReadReference<FTrigger>(METASOUND_GET_PARAM_NAME(InputTriggerStop), InParams.OperatorSettings),
				Inputs.GetOrCreateDefaultDataReadReference<FWaveAsset>(METASOUND_GET_PARAM_NAME(InputWaveAsset), InParams.OperatorSettings),
				Inputs.GetOrCreateDefaultDataReadReference<FTime>(METASOUND_GET_PARAM_NAME(InputStartTime), InParams.OperatorSettings),
				Inputs.GetOrCreateDefaultDataReadReference<float>(METASOUND_GET_PARAM_NAME(InputPitchShift), InParams.OperatorSettings),
				Inputs.GetOrCreateDefaultDataReadReference<bool>(METASOUND_GET_PARAM_NAME(InputLoop), InParams.OperatorSettings),
				Inputs.GetOrCreateDefaultDataReadReference<FTime>(METASOUND_GET_PARAM_NAME(InputLoopStart), InParams.OperatorSettings),
				Inputs.GetOrCreateDefaultDataReadReference<FTime>(METASOUND_GET_PARAM_NAME(InputLoopDuration), InParams.OperatorSettings),
				FNodeRenderCost(InParams.GraphRenderCost ? InParams.GraphRenderCost->AddNode(InParams.Node.GetInstanceID(), InParams.Environment) : FNodeRenderCost{})
			};

			return MakeUnique<FWavePlayerOperator>(Args);
		}
	private:
		TArray<FOutputDataVertex> OutputAudioVertices;
	};

	template<typename AudioChannelConfigurationInfoType>
	class TWavePlayerNode : public FBasicNode
	{

	public:
		TWavePlayerNode(const FNodeInitData& InInitData)
			: TWavePlayerNode(FNodeData(InInitData.InstanceName, InInitData.InstanceID, WavePlayerOperatorPrivate::GetVertexInterface<AudioChannelConfigurationInfoType>()), MakeShared<const FNodeClassMetadata>(CreateNodeClassMetadata()))
		{
		}

		TWavePlayerNode(FNodeData InNodeData, TSharedRef<const FNodeClassMetadata> InClassMetadata)
			: FBasicNode(MoveTemp(InNodeData), MoveTemp(InClassMetadata))
			, Factory(MakeOperatorFactoryRef<FWavePlayerOperatorFactory>(AudioChannelConfigurationInfoType::GetAudioOutputs()))
		{
		}


		virtual ~TWavePlayerNode() = default;

		static FNodeClassMetadata CreateNodeClassMetadata()
		{
			return WavePlayerOperatorPrivate::GetNodeInfo<AudioChannelConfigurationInfoType>();
		}

		virtual FOperatorFactorySharedRef GetDefaultOperatorFactory() const override
		{
			return Factory;
		}

	private:
		FOperatorFactorySharedRef Factory;
	};

	struct FMonoAudioChannelConfigurationInfo
	{
		static FText GetNodeDisplayName() { return METASOUND_LOCTEXT("Metasound_WavePlayerMonoNodeDisplayName", "Wave Player (1.0, Mono)"); }
		static FName GetVariantName() { return Metasound::EngineNodes::MonoVariant; }

		static TArray<FOutputDataVertex> GetAudioOutputs()
		{
			using namespace WavePlayerVertexNames;
			return {
				TOutputDataVertex<FAudioBuffer>(METASOUND_GET_PARAM_NAME_AND_METADATA(OutputAudioMono))
			};
		}
	};
	using FMonoWavePlayerNode = TWavePlayerNode<FMonoAudioChannelConfigurationInfo>;
	METASOUND_REGISTER_NODE(FMonoWavePlayerNode);

	struct FStereoAudioChannelConfigurationInfo
	{
		static FText GetNodeDisplayName() { return METASOUND_LOCTEXT("Metasound_WavePlayerStereoNodeDisplayName", "Wave Player (2.0, Stereo)"); }
		static FName GetVariantName() { return Metasound::EngineNodes::StereoVariant; }

		static TArray<FOutputDataVertex> GetAudioOutputs()
		{
			using namespace WavePlayerVertexNames;
			return {
				TOutputDataVertex<FAudioBuffer>(METASOUND_GET_PARAM_NAME_AND_METADATA(OutputAudioLeft)),
				TOutputDataVertex<FAudioBuffer>(METASOUND_GET_PARAM_NAME_AND_METADATA(OutputAudioRight))
			};
		}
	};
	using FStereoWavePlayerNode = TWavePlayerNode<FStereoAudioChannelConfigurationInfo>;
	METASOUND_REGISTER_NODE(FStereoWavePlayerNode);

	struct FQuadAudioChannelConfigurationInfo
	{
		static FText GetNodeDisplayName() { return METASOUND_LOCTEXT("Metasound_WavePlayerQuadNodeDisplayName", "Wave Player (4.0, Quad)"); }
		static FName GetVariantName() { return Metasound::EngineNodes::QuadVariant; }

		static TArray<FOutputDataVertex> GetAudioOutputs()
		{
			using namespace WavePlayerVertexNames;
			return {
				TOutputDataVertex<FAudioBuffer>(METASOUND_GET_PARAM_NAME_AND_METADATA(OutputAudioFrontLeft)),
				TOutputDataVertex<FAudioBuffer>(METASOUND_GET_PARAM_NAME_AND_METADATA(OutputAudioFrontRight)),
				TOutputDataVertex<FAudioBuffer>(METASOUND_GET_PARAM_NAME_AND_METADATA(OutputAudioSideLeft)),
				TOutputDataVertex<FAudioBuffer>(METASOUND_GET_PARAM_NAME_AND_METADATA(OutputAudioSideRight))
			};
		}
	};
	using FQuadWavePlayerNode = TWavePlayerNode<FQuadAudioChannelConfigurationInfo>;
	METASOUND_REGISTER_NODE(FQuadWavePlayerNode);

	struct FFiveDotOneAudioChannelConfigurationInfo
	{
		static FText GetNodeDisplayName() { return METASOUND_LOCTEXT("Metasound_WavePlayerFiveDotOneNodeDisplayName", "Wave Player (5.1, Surround)"); }
		static FName GetVariantName() { return Metasound::EngineNodes::FiveDotOneVariant; }

		static TArray<FOutputDataVertex> GetAudioOutputs()
		{
			using namespace WavePlayerVertexNames;
			return {
				TOutputDataVertex<FAudioBuffer>(METASOUND_GET_PARAM_NAME_AND_METADATA(OutputAudioFrontLeft)),
				TOutputDataVertex<FAudioBuffer>(METASOUND_GET_PARAM_NAME_AND_METADATA(OutputAudioFrontRight)),
				TOutputDataVertex<FAudioBuffer>(METASOUND_GET_PARAM_NAME_AND_METADATA(OutputAudioFrontCenter)),
				TOutputDataVertex<FAudioBuffer>(METASOUND_GET_PARAM_NAME_AND_METADATA(OutputAudioLowFrequency)),
				TOutputDataVertex<FAudioBuffer>(METASOUND_GET_PARAM_NAME_AND_METADATA(OutputAudioSideLeft)),
				TOutputDataVertex<FAudioBuffer>(METASOUND_GET_PARAM_NAME_AND_METADATA(OutputAudioSideRight))
			};
		}
	};
	using FFiveDotOneWavePlayerNode = TWavePlayerNode<FFiveDotOneAudioChannelConfigurationInfo>;
	METASOUND_REGISTER_NODE(FFiveDotOneWavePlayerNode);

	struct FSevenDotOneAudioChannelConfigurationInfo
	{
		static FText GetNodeDisplayName() { return METASOUND_LOCTEXT("Metasound_WavePlayerSevenDotOneNodeDisplayName", "Wave Player (7.1, Surround)"); }
		static FName GetVariantName() { return Metasound::EngineNodes::SevenDotOneVariant; }

		static TArray<FOutputDataVertex> GetAudioOutputs()
		{
			using namespace WavePlayerVertexNames;
			return {
				TOutputDataVertex<FAudioBuffer>(METASOUND_GET_PARAM_NAME_AND_METADATA(OutputAudioFrontLeft)),
				TOutputDataVertex<FAudioBuffer>(METASOUND_GET_PARAM_NAME_AND_METADATA(OutputAudioFrontRight)),
				TOutputDataVertex<FAudioBuffer>(METASOUND_GET_PARAM_NAME_AND_METADATA(OutputAudioFrontCenter)),
				TOutputDataVertex<FAudioBuffer>(METASOUND_GET_PARAM_NAME_AND_METADATA(OutputAudioLowFrequency)),
				TOutputDataVertex<FAudioBuffer>(METASOUND_GET_PARAM_NAME_AND_METADATA(OutputAudioSideLeft)),
				TOutputDataVertex<FAudioBuffer>(METASOUND_GET_PARAM_NAME_AND_METADATA(OutputAudioSideRight)),
				TOutputDataVertex<FAudioBuffer>(METASOUND_GET_PARAM_NAME_AND_METADATA(OutputAudioBackLeft)),
				TOutputDataVertex<FAudioBuffer>(METASOUND_GET_PARAM_NAME_AND_METADATA(OutputAudioBackRight))
			};
		}
	};
	using FSevenDotOneWavePlayerNode = TWavePlayerNode<FSevenDotOneAudioChannelConfigurationInfo>;
	METASOUND_REGISTER_NODE(FSevenDotOneWavePlayerNode);
} // namespace Metasound

#undef LOCTEXT_NAMESPACE // MetasoundWaveNode


