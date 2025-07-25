// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetalProfiler.h"
#include "MetalRHIPrivate.h"
#include "MetalDynamicRHI.h"
#include "EngineGlobals.h"
#include "StaticBoundShaderState.h"
#include "MetalCommandBuffer.h"
#include "MetalRHIContext.h"
#include "HAL/PlatformFramePacer.h"
#include "HAL/FileManager.h"

DEFINE_STAT(STAT_MetalUniformMemAlloc);
DEFINE_STAT(STAT_MetalUniformMemFreed);
DEFINE_STAT(STAT_MetalVertexMemAlloc);
DEFINE_STAT(STAT_MetalVertexMemFreed);
DEFINE_STAT(STAT_MetalIndexMemAlloc);
DEFINE_STAT(STAT_MetalIndexMemFreed);
DEFINE_STAT(STAT_MetalTextureMemUpdate);

DEFINE_STAT(STAT_MetalDrawCallTime);
DEFINE_STAT(STAT_MetalPipelineStateTime);
DEFINE_STAT(STAT_MetalPrepareDrawTime);
DEFINE_STAT(STAT_MetalSwitchToNoneTime);
DEFINE_STAT(STAT_MetalSwitchToRenderTime);
DEFINE_STAT(STAT_MetalSwitchToComputeTime);
DEFINE_STAT(STAT_MetalSwitchToBlitTime);
DEFINE_STAT(STAT_MetalPrepareToRenderTime);
DEFINE_STAT(STAT_MetalPrepareToDispatchTime);
DEFINE_STAT(STAT_MetalCommitRenderResourceTablesTime);
DEFINE_STAT(STAT_MetalSetRenderStateTime);
DEFINE_STAT(STAT_MetalSetRenderPipelineStateTime);

DEFINE_STAT(STAT_MetalMakeDrawableTime);
DEFINE_STAT(STAT_MetalBufferPageOffTime);
DEFINE_STAT(STAT_MetalTexturePageOnTime);
DEFINE_STAT(STAT_MetalTexturePageOffTime);
DEFINE_STAT(STAT_MetalGPUWorkTime);
DEFINE_STAT(STAT_MetalGPUIdleTime);
DEFINE_STAT(STAT_MetalPresentTime);
DEFINE_STAT(STAT_MetalCustomPresentTime);
DEFINE_STAT(STAT_MetalCommandBufferCreatedPerFrame);
DEFINE_STAT(STAT_MetalCommandBufferCommittedPerFrame);
DEFINE_STAT(STAT_MetalBufferMemory);
DEFINE_STAT(STAT_MetalTextureMemory);
DEFINE_STAT(STAT_MetalHeapMemory);
DEFINE_STAT(STAT_MetalBufferUnusedMemory);
DEFINE_STAT(STAT_MetalTextureUnusedMemory);
DEFINE_STAT(STAT_MetalBufferCount);
DEFINE_STAT(STAT_MetalTextureCount);
DEFINE_STAT(STAT_MetalHeapCount);
DEFINE_STAT(STAT_MetalFenceCount);

DEFINE_STAT(STAT_MetalUniformMemoryInFlight);
DEFINE_STAT(STAT_MetalUniformAllocatedMemory);
DEFINE_STAT(STAT_MetalUniformBytesPerFrame);

DEFINE_STAT(STAT_MetalTempAllocatorAllocatedMemory);

int64 volatile GMetalTexturePageOnTime = 0;
int64 volatile GMetalGPUWorkTime = 0;
int64 volatile GMetalGPUIdleTime = 0;
int64 volatile GMetalPresentTime = 0;

#if RHI_NEW_GPU_PROFILER && WITH_RHI_BREADCRUMBS
FMetalBreadcrumbProfiler* FMetalBreadcrumbProfiler::Instance = nullptr;
#endif

void WriteString(FArchive* OutputFile, const char* String)
{
	OutputFile->Serialize((void*)String, sizeof(ANSICHAR)*FCStringAnsi::Strlen(String));
}

#if RHI_NEW_GPU_PROFILER == 0

void FMetalCommandBufferTimer::AddTiming(FMetalCommandBufferTiming Timing)
{
	FScopeLock Lock(&Mutex);
	Counter--;
	Timings.Add(Timing);
	
	if(Counter == 0 && bFrameEnded)
	{
		RecordFrame();
	}
}

void FMetalCommandBufferTimer::FrameEnd()
{
	FScopeLock Lock(&Mutex);
	bFrameEnded = true;
	
	if(Counter == 0)
	{
		RecordFrame();
	}
}

void FMetalCommandBufferTimer::RecordFrame()
{
	Timings.Sort();
	
	FMetalCommandBufferTiming LastBufferTiming;
	const double CyclesPerSecond = 1.0 / FPlatformTime::GetSecondsPerCycle();

	double RunningFrameTimeSeconds = 0.0;
	uint64 FrameStartGPUCycles = 0;
	uint64 FrameEndGPUCycles = 0;

	CFTimeInterval FirstStartTime = 0.0;

	// Add the timings excluding any overlapping time
	for (const FMetalCommandBufferTiming& Timing : Timings)
	{
		if (FirstStartTime == 0.0)
		{
			FirstStartTime = Timing.StartTime;
			LastBufferTiming = Timing;
		}
		
		// Only process if the previous buffer finished before the end of this one
		if (LastBufferTiming.EndTime < Timing.EndTime)
		{
			// Check if the end of the previous buffer finished before the start of this one
			if (LastBufferTiming.EndTime > Timing.StartTime)
			{
				// Segment from end of last buffer to end of current
				RunningFrameTimeSeconds += Timing.EndTime - LastBufferTiming.EndTime;
			}
			else
			{
				// Full timing of this buffer
				RunningFrameTimeSeconds += Timing.EndTime - Timing.StartTime;
			}

			LastBufferTiming = Timing;
		}
	}
	
	FrameStartGPUCycles = FirstStartTime * CyclesPerSecond;
	FrameEndGPUCycles = LastBufferTiming.EndTime * CyclesPerSecond;
	
	uint64 FrameGPUTimeCycles = uint64(CyclesPerSecond * RunningFrameTimeSeconds);
	GRHIGPUFrameTimeHistory.PushFrameCycles(1.0, RunningFrameTimeSeconds);
	
#if STATS
	FPlatformAtomics::AtomicStore_Relaxed(&GMetalGPUWorkTime, FrameGPUTimeCycles);
	int64 FrameIdleTimeCycles = int64(FrameEndGPUCycles - FrameStartGPUCycles - FrameGPUTimeCycles);
	FPlatformAtomics::AtomicStore_Relaxed(&GMetalGPUIdleTime, FrameIdleTimeCycles);
#endif //STATS
	
	FMetalDynamicRHI::Get().DeferredDelete([InTimer=this](){
		delete InTimer;
	});
}

void FMetalCommandBufferTimer::RecordPresent(MTL::CommandBuffer* CommandBuffer)
{
	const CFTimeInterval GpuStartTimeSeconds = CommandBuffer->GPUStartTime();
	const CFTimeInterval GpuEndTimeSeconds = CommandBuffer->GPUStartTime();
	const double CyclesPerSecond = 1.0 / FPlatformTime::GetSecondsPerCycle();
	uint64 StartTimeCycles = uint64(GpuStartTimeSeconds * CyclesPerSecond);
	uint64 EndTimeCycles = uint64(GpuEndTimeSeconds * CyclesPerSecond);
	int64 Time = int64(EndTimeCycles - StartTimeCycles);
	FPlatformAtomics::AtomicStore_Relaxed(&GMetalPresentTime, Time);
}
// END WARNING

void FMetalCommandBufferTimer::ResetFrameBufferTimings()
{
	if(Timer)
	{
		Timer->FrameEnd();
	}
	
	Timer = new FMetalCommandBufferTimer();
}

FMetalCommandBufferTimer& FMetalCommandBufferTimer::GetFrameBufferTimer()
{
	if(!Timer)
	{
		Timer = new FMetalCommandBufferTimer();
	}
	return *Timer;
}

FMetalCommandBufferTimer* FMetalCommandBufferTimer::Timer = nullptr;

void FMetalGPUTiming::PlatformStaticInitialize(void* UserData)
{
	// Are the static variables initialized?
	if ( !GAreGlobalsInitialized )
	{
		GIsSupported = true;
		SetTimingFrequency(1000 * 1000 * 1000);
		GAreGlobalsInitialized = true;
		
		FMetalDevice& Device = ((FMetalRHICommandContext*)UserData)->GetDevice();
		
		MTL::Timestamp CPUTimeStamp, GPUTimestamp;
		Device.GetDevice()->sampleTimestamps(&CPUTimeStamp, &GPUTimestamp);

		FGPUTiming::SetCalibrationTimestamp({ GPUTimestamp, CPUTimeStamp });
	}
}

FMetalEventNode::~FMetalEventNode()
{

}

float FMetalEventNode::GetTiming()
{
	SyncPoint->Wait();
	return FPlatformTime::ToSeconds(EndTime - StartTime);
}

void FMetalEventNode::StartTiming()
{
	StartTime = 0;
	EndTime = 0;

	Context.StartTiming(this);
}

void FMetalEventNode::StopTiming()
{
	Context.EndTiming(this);
}

bool MetalGPUProfilerIsInSafeThread()
{
	return (GIsMetalInitialized && !GIsRHIInitialized) || (IsInRHIThread() || IsInActualRenderingThread());
}
	
/** Start this frame of per tracking */
void FMetalEventNodeFrame::StartFrame()
{
	RootNode->StartTiming();
}

/** End this frame of per tracking, but do not block yet */
void FMetalEventNodeFrame::EndFrame()
{
	RootNode->StopTiming();
}

/** Calculates root timing base frequency (if needed by this RHI) */
float FMetalEventNodeFrame::GetRootTimingResults()
{
	return RootNode->GetTiming();
}

void FMetalEventNodeFrame::LogDisjointQuery()
{
	
}

FGPUProfilerEventNode* FMetalGPUProfiler::CreateEventNode(const TCHAR* InName, FGPUProfilerEventNode* InParent)
{
#if ENABLE_METAL_GPUPROFILE
	FMetalEventNode* EventNode = new FMetalEventNode(Context, InName, InParent, false, false);
	return EventNode;
#else
	return nullptr;
#endif
}

void FMetalGPUProfiler::Cleanup()
{
	
}

void FMetalGPUProfiler::PushEvent(const TCHAR* Name, FColor Color)
{
	if(MetalGPUProfilerIsInSafeThread())
	{
		FGPUProfiler::PushEvent(Name, Color);
	}
}

void FMetalGPUProfiler::PopEvent()
{
	if(MetalGPUProfilerIsInSafeThread())
	{
		FGPUProfiler::PopEvent();
	}
}

//TGlobalResource<FVector4VertexDeclaration> GMetalVector4VertexDeclaration;
TGlobalResource<FTexture> GMetalLongTaskRT;

void FMetalGPUProfiler::BeginFrame()
{
	if(GTriggerGPUProfile)
	{
		bTrackingEvents = true;
		bLatchedGProfilingGPU = true;
		GTriggerGPUProfile = false;
	}
	
	if (bLatchedGProfilingGPU)
	{
		// Start tracking the frame
		CurrentEventNodeFrame = new FMetalEventNodeFrame(Context, GTriggerGPUProfile);
		CurrentEventNodeFrame->StartFrame();
	}
}

void FMetalGPUProfiler::EndFrame()
{
	dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
#if PLATFORM_MAC
	FPlatformMisc::UpdateDriverMonitorStatistics(Context.GetDevice().GetDeviceIndex());
#endif
	});
	
#if STATS
	SET_CYCLE_COUNTER(STAT_MetalTexturePageOnTime, GMetalTexturePageOnTime);
	GMetalTexturePageOnTime = 0;
	
	SET_CYCLE_COUNTER(STAT_MetalGPUIdleTime, GMetalGPUIdleTime);
	SET_CYCLE_COUNTER(STAT_MetalGPUWorkTime, GMetalGPUWorkTime);
	SET_CYCLE_COUNTER(STAT_MetalPresentTime, GMetalPresentTime);
#endif
	
	if(CurrentEventNodeFrame)
	{
		GDynamicRHI->RHIBlockUntilGPUIdle();
		
		CurrentEventNodeFrame->EndFrame();
		
		if(bLatchedGProfilingGPU)
		{
			bTrackingEvents = false;
			bLatchedGProfilingGPU = false;
		
			UE_LOG(LogRHI, Warning, TEXT(""));
			UE_LOG(LogRHI, Warning, TEXT(""));
			CurrentEventNodeFrame->DumpEventTree();
		}
		
		delete CurrentEventNodeFrame;
		CurrentEventNodeFrame = NULL;
	}
}

IMetalStatsScope::~IMetalStatsScope()
{
	for (IMetalStatsScope* Stat : Children)
	{
		delete Stat;
	}
}

FString IMetalStatsScope::GetJSONRepresentation(uint32 Pid)
{
	FString JSONOutput;
	
	{
		if (GPUStartTime && GPUEndTime)
		{
			uint64 ChildStartCallTime = GPUStartTime;
			uint64 ChildDrawCallTime = GPUEndTime - GPUStartTime;
			
			JSONOutput += FString::Printf(TEXT("{\"pid\":%d, \"tid\":%d, \"ph\": \"X\", \"name\": \"%s\", \"ts\": %llu, \"dur\": %llu, \"args\":{\"num_child\":%u}},\n"),
				  Pid,
				  GPUThreadIndex,
				  *Name,
				  ChildStartCallTime,
				  ChildDrawCallTime,
				  Children.Num()
			  );
		}
	}
	
	if (CPUStartTime && CPUEndTime)
	{
		uint64 ChildStartCallTime = CPUStartTime;
		uint64 ChildDrawCallTime = FMath::Max(CPUEndTime - CPUStartTime, 1llu);
		
		JSONOutput += FString::Printf(TEXT("{\"pid\":%d, \"tid\":%d, \"ph\": \"X\", \"name\": \"%s\", \"ts\": %llu, \"dur\": %llu, \"args\":{\"num_child\":%u}},\n"),
			 Pid,
			 CPUThreadIndex,
			 *Name,
			 ChildStartCallTime,
			 ChildDrawCallTime,
			 Children.Num()
		);
	}
	
	return JSONOutput;
}

FMetalCommandBufferStats::FMetalCommandBufferStats(MTL::CommandBuffer* CommandBuffer, uint64 InGPUThreadIndex)
{
	CmdBuffer = CommandBuffer;
	
	Name = FString::Printf(TEXT("CommandBuffer: %p %s"), CmdBuffer, *NSStringToFString(CmdBuffer->label()));
	
	CPUThreadIndex = FPlatformTLS::GetCurrentThreadId();
	GPUThreadIndex = InGPUThreadIndex;
	
	Start(CmdBuffer);
}

FMetalCommandBufferStats::~FMetalCommandBufferStats()
{
}

void FMetalCommandBufferStats::Start(MTL::CommandBuffer*& CommandBuffer)
{
	CPUStartTime = FPlatformTime::ToMilliseconds64(mach_absolute_time()) * 1000.0;
	CPUEndTime = 0;
	
	GPUStartTime = 0;
	GPUEndTime = 0;
}

void FMetalCommandBufferStats::End(MTL::CommandBuffer*& CommandBuffer)
{
	check(CommandBuffer == CmdBuffer);
	
	bool const bTracing = FMetalProfiler::GetProfiler() && FMetalProfiler::GetProfiler()->TracingEnabled();
    
    MTL::HandlerFunction CompletionHandler = [this, bTracing](MTL::CommandBuffer* CommandBuffer) {
        const CFTimeInterval GpuTimeSeconds = CommandBuffer->GPUStartTime();
        GPUStartTime = GpuTimeSeconds * 1000000.0;
        
        const CFTimeInterval GpuEndTimeSeconds = CommandBuffer->GPUEndTime();
        GPUEndTime = GpuEndTimeSeconds * 1000000.0;
    
        if (bTracing)
        {
            FMetalProfiler::GetProfiler()->AddCommandBuffer(this);
        }
        else
        {
            delete this;
        }
    };
    
    CmdBuffer->addCompletedHandler(CompletionHandler);
	
	CPUEndTime = FPlatformTime::ToMilliseconds64(mach_absolute_time()) * 1000.0;
}


#pragma mark -- FMetalProfiler
// ----------------------------------------------------------------


FMetalProfiler* FMetalProfiler::Self = nullptr;
static FMetalViewportPresentHandler PresentHandler = ^(uint32 DisplayID, double OutputSeconds, double OutputDuration){
	FMetalProfiler* Profiler = FMetalProfiler::GetProfiler();
	Profiler->AddDisplayVBlank(DisplayID, OutputSeconds, OutputDuration);
};

FMetalDisplayStats::FMetalDisplayStats(uint32 DisplayID, double OutputSeconds, double Duration)
{
	Name = TEXT("V-Blank");
	
	CPUThreadIndex = FPlatformTLS::GetCurrentThreadId();
	GPUThreadIndex = DisplayID;
	
	CPUStartTime = FPlatformTime::ToMilliseconds64(mach_absolute_time()) * 1000.0;
	CPUEndTime = CPUStartTime+1;
	
	GPUStartTime = OutputSeconds * 1000000.0;
	GPUEndTime = GPUStartTime + (Duration * 1000000.0);
}
FMetalDisplayStats::~FMetalDisplayStats()
{
}

void FMetalDisplayStats::Start(MTL::CommandBuffer*& Buffer)
{
}
void FMetalDisplayStats::End(MTL::CommandBuffer*& Buffer)
{
}

FMetalCPUStats::FMetalCPUStats(FString const& InName)
{
	Name = InName;
	
	CPUThreadIndex = 0;
	GPUThreadIndex = 0;
	
	CPUStartTime = 0;
	CPUEndTime = 0;
	
	GPUStartTime = 0;
	GPUEndTime = 0;
}
FMetalCPUStats::~FMetalCPUStats()
{
	
}

void FMetalCPUStats::Start(void)
{
	CPUThreadIndex = FPlatformTLS::GetCurrentThreadId();
	CPUStartTime = FPlatformTime::ToMilliseconds64(mach_absolute_time()) * 1000.0;
	
}
void FMetalCPUStats::End(void)
{
	CPUEndTime = FPlatformTime::ToMilliseconds64(mach_absolute_time()) * 1000.0;
}

void FMetalCPUStats::Start(MTL::CommandBuffer*& Buffer)
{
	
}
void FMetalCPUStats::End(MTL::CommandBuffer*& Buffer)
{
	
}

void FMetalProfiler::AddDisplayVBlank(uint32 DisplayID, double OutputSeconds, double OutputDuration)
{
	if (GIsRHIInitialized && bEnabled)
	{
		FScopeLock Lock(&Mutex);
		DisplayStats.Add(new FMetalDisplayStats(DisplayID, OutputSeconds, OutputDuration));
	}
}

FMetalProfiler::FMetalProfiler(FMetalRHICommandContext& Context)
: FMetalGPUProfiler(Context)
, bEnabled(false)
{
	NumFramesToCapture = -1;
	CaptureFrameNumber = 0;
	
	bRequestStartCapture = false;
	bRequestStopCapture = false;
	
	if (FPlatformRHIFramePacer::IsEnabled())
	{
		FPlatformRHIFramePacer::AddHandler(PresentHandler);
	}
}

FMetalProfiler::~FMetalProfiler()
{
	check(bEnabled == false);
	if (FPlatformRHIFramePacer::IsEnabled())
	{
		FPlatformRHIFramePacer::RemoveHandler(PresentHandler);
	}
}

FMetalProfiler* FMetalProfiler::CreateProfiler(FMetalRHICommandContext& InContext)
{
	if (!Self)
	{
		Self = new FMetalProfiler(InContext);
		
		int32 CaptureFrames = 0;
		if (FParse::Value(FCommandLine::Get(), TEXT("MetalProfileFrames="), CaptureFrames))
		{
			Self->BeginCapture(CaptureFrames);
		}
	}
	return Self;
}

FMetalProfiler* FMetalProfiler::GetProfiler()
{
	return Self;
}

void FMetalProfiler::DestroyProfiler()
{
	delete Self;
	Self = nullptr;
}

void FMetalProfiler::BeginCapture(int InNumFramesToCapture)
{
	check(IsInGameThread());
	
	NumFramesToCapture = InNumFramesToCapture;
	CaptureFrameNumber = 0;
	
	bRequestStartCapture = true;
}

void FMetalProfiler::EndCapture()
{
	bRequestStopCapture = true;
}

bool FMetalProfiler::TracingEnabled() const
{
	return bEnabled;
}

void FMetalProfiler::BeginFrame()
{
	if (MetalGPUProfilerIsInSafeThread())
	{
		if (bRequestStartCapture && !bEnabled)
		{
			bEnabled = true;
			bRequestStartCapture = false;
		}
	}
	
	FMetalGPUProfiler::BeginFrame();
}

void FMetalProfiler::EndFrame()
{	
	FMetalGPUProfiler::EndFrame();
	
	if (MetalGPUProfilerIsInSafeThread() && bEnabled)
	{
		CaptureFrameNumber++;
		if (bRequestStopCapture || (NumFramesToCapture > 0 && CaptureFrameNumber >= NumFramesToCapture))
		{
			bRequestStopCapture = false;
			NumFramesToCapture = -1;
			bEnabled = false;
			SaveTrace();
		}
	}
}

void FMetalProfiler::EncodeDraw(FMetalCommandBufferStats* CmdBufStats, char const* DrawCall, uint32 RHIPrimitives, uint32 RHIVertices, uint32 RHIInstances)
{
#if (RHI_NEW_GPU_PROFILER == 0)
	if (MetalGPUProfilerIsInSafeThread())
	{
		FMetalGPUProfiler::RegisterGPUWork(RHIPrimitives, RHIVertices);
	}
#endif
}

void FMetalProfiler::EncodeBlit(FMetalCommandBufferStats* CmdBufStats, char const* DrawCall)
{
#if (RHI_NEW_GPU_PROFILER == 0)
	if (MetalGPUProfilerIsInSafeThread())
	{
		FMetalGPUProfiler::RegisterGPUWork(1, 1);
	}
#endif
}

void FMetalProfiler::EncodeBlit(FMetalCommandBufferStats* CmdBufStats, FString DrawCall)
{
#if (RHI_NEW_GPU_PROFILER == 0)
	if (MetalGPUProfilerIsInSafeThread())
	{
		FMetalGPUProfiler::RegisterGPUWork(1, 1);
	}
#endif
}

void FMetalProfiler::EncodeDispatch(FMetalCommandBufferStats* CmdBufStats, char const* DrawCall)
{
#if (RHI_NEW_GPU_PROFILER == 0)
	if (MetalGPUProfilerIsInSafeThread())
	{
		FMetalGPUProfiler::RegisterGPUWork(1, 1);
	}
#endif
}

FMetalCPUStats* FMetalProfiler::AddCPUStat(FString const& Name)
{
	if (GIsRHIInitialized && bEnabled)
	{
		FScopeLock Lock(&Mutex);
		FMetalCPUStats* Stat = new FMetalCPUStats(Name);
		CPUStats.Add(Stat);
		return Stat;
	}
	else
	{
		return nullptr;
	}
}

FMetalCommandBufferStats* FMetalProfiler::AllocateCommandBuffer(MTL::CommandBuffer* Buffer, uint64 GPUThreadIndex)
{
	return new FMetalCommandBufferStats(Buffer, GPUThreadIndex);
}

void FMetalProfiler::AddCommandBuffer(FMetalCommandBufferStats *CommandBuffer)
{
	if (GIsRHIInitialized)
	{
		FScopeLock Lock(&Mutex);
		TracedBuffers.Add(CommandBuffer);
	}
	else
	{
		delete CommandBuffer;
	}
}

void FMetalProfiler::PushEvent(const TCHAR *Name, FColor Color)
{
	FMetalGPUProfiler::PushEvent(Name, Color);
}

void FMetalProfiler::PopEvent()
{
	FMetalGPUProfiler::PopEvent();
}

void FMetalProfiler::SaveTrace()
{
	GDynamicRHI->RHIBlockUntilGPUIdle();
	
	{
		FScopeLock Lock(&Mutex);
		
		TSet<uint32> ThreadIDs;
		
		for (FMetalCommandBufferStats* CmdBufStats : TracedBuffers)
		{
			ThreadIDs.Add(CmdBufStats->CPUThreadIndex);
			
			for (IMetalStatsScope* ES : CmdBufStats->Children)
			{
				ThreadIDs.Add(ES->CPUThreadIndex);
				
				for (IMetalStatsScope* DS : ES->Children)
				{
					ThreadIDs.Add(DS->CPUThreadIndex);
				}
			}
		}
		
		TSet<uint32> Displays;
		for (FMetalDisplayStats* DisplayStat : DisplayStats)
		{
			ThreadIDs.Add(DisplayStat->CPUThreadIndex);
			Displays.Add(DisplayStat->GPUThreadIndex);
		}
		
		for (FMetalCPUStats* CPUStat : CPUStats)
		{
			ThreadIDs.Add(CPUStat->CPUThreadIndex);
		}
		
		FString Filename = FString::Printf(TEXT("Profile(%s)"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
		FString TracingRootPath = FPaths::ProfilingDir() + TEXT("Traces/");
		FString OutputFilename = TracingRootPath + Filename + TEXT(".json");
		
		FArchive* OutputFile = IFileManager::Get().CreateFileWriter(*OutputFilename);
		
		WriteString(OutputFile, R"({"traceEvents":[)" "\n");
		
		int32 SortIndex = 0; // Lower numbers result in higher position in the visualizer.
		const uint32 Pid = FPlatformProcess::GetCurrentProcessId();
		
		for (int32 GPUIndex = 0; GPUIndex <= 0/*MaxGPUIndex*/; ++GPUIndex)
		{
			FString Output = FString::Printf(TEXT("{\"pid\":%d, \"tid\":%d, \"ph\": \"M\", \"name\": \"thread_name\", \"args\":{\"name\":\"GPU %d Command Buffers\"}},{\"pid\":%d, \"tid\":%d, \"ph\": \"M\", \"name\": \"thread_sort_index\", \"args\":{\"sort_index\": %d}},\n"),
											 Pid, GPUIndex, GPUIndex, Pid, GPUIndex, SortIndex
											 );
			
			WriteString(OutputFile, TCHAR_TO_UTF8(*Output));
			SortIndex++;
			
			Output = FString::Printf(TEXT("{\"pid\":%d, \"tid\":%d, \"ph\": \"M\", \"name\": \"thread_name\", \"args\":{\"name\":\"GPU %d Operations\"}},{\"pid\":%d, \"tid\":%d, \"ph\": \"M\", \"name\": \"thread_sort_index\", \"args\":{\"sort_index\": %d}},\n"),
											 Pid, GPUIndex+SortIndex, GPUIndex, Pid, GPUIndex+SortIndex, SortIndex
											 );
			
			WriteString(OutputFile, TCHAR_TO_UTF8(*Output));
			SortIndex++;
			
			Output = FString::Printf(TEXT("{\"pid\":%d, \"tid\":%d, \"ph\": \"M\", \"name\": \"thread_name\", \"args\":{\"name\":\"Render Events %d\"}},{\"pid\":%d, \"tid\":%d, \"ph\": \"M\", \"name\": \"thread_sort_index\", \"args\":{\"sort_index\": %d}},\n"),
									 Pid, GPUIndex+SortIndex, GPUIndex, Pid, GPUIndex+SortIndex, SortIndex
									 );
			
			WriteString(OutputFile, TCHAR_TO_UTF8(*Output));
			SortIndex++;
			
			Output = FString::Printf(TEXT("{\"pid\":%d, \"tid\":%d, \"ph\": \"M\", \"name\": \"thread_name\", \"args\":{\"name\":\"Driver Stats %d\"}},{\"pid\":%d, \"tid\":%d, \"ph\": \"M\", \"name\": \"thread_sort_index\", \"args\":{\"sort_index\": %d}},\n"),
									 Pid, GPUIndex+SortIndex, GPUIndex, Pid, GPUIndex+SortIndex, SortIndex
									 );
			
			WriteString(OutputFile, TCHAR_TO_UTF8(*Output));
			SortIndex++;
			
			for (uint32 Display : Displays)
			{
				Output = FString::Printf(TEXT("{\"pid\":%d, \"tid\":%d, \"ph\": \"M\", \"name\": \"thread_name\", \"args\":{\"name\":\"Display %d\"}},{\"pid\":%d, \"tid\":%d, \"ph\": \"M\", \"name\": \"thread_sort_index\", \"args\":{\"sort_index\": %d}},\n"),
										 Pid, Display + SortIndex, SortIndex - 3, Pid, Display + SortIndex, SortIndex
										 );
				
				WriteString(OutputFile, TCHAR_TO_UTF8(*Output));
				SortIndex++;
			}
		}
		
		static const uint32 BufferSize = 128;
		ANSICHAR Buffer[BufferSize];
		for (uint32 CPUIndex : ThreadIDs)
		{
			bool bThreadName = false;
			pthread_t PThread = pthread_from_mach_thread_np((mach_port_t)CPUIndex);
			if (PThread)
			{
				if (!pthread_getname_np(PThread,Buffer,BufferSize))
				{
					bThreadName = true;
				}
			}
			if (!bThreadName)
			{
				snprintf(Buffer, BufferSize, "Thread %d", CPUIndex);
			}
			
			FString Output = FString::Printf(TEXT("{\"pid\":%d, \"tid\":%d, \"ph\": \"M\", \"name\": \"thread_name\", \"args\":{\"name\":\"%s\"}},{\"pid\":%d, \"tid\":%d, \"ph\": \"M\", \"name\": \"thread_sort_index\", \"args\":{\"sort_index\": %d}},\n"),
											 Pid, CPUIndex, ANSI_TO_TCHAR(Buffer), Pid, CPUIndex, SortIndex
											 );
			
			
			WriteString(OutputFile, TCHAR_TO_UTF8(*Output));
			SortIndex++;
		}
		
		for (FMetalCommandBufferStats* CmdBufStats : TracedBuffers)
		{
			WriteString(OutputFile, TCHAR_TO_UTF8(*CmdBufStats->GetJSONRepresentation(Pid)));
			
			for (IMetalStatsScope* ES : CmdBufStats->Children)
			{
				WriteString(OutputFile, TCHAR_TO_UTF8(*ES->GetJSONRepresentation(Pid)));
				
				uint64 PrevTime = ES->GPUStartTime;
				for (IMetalStatsScope* DS : ES->Children)
				{
					WriteString(OutputFile, TCHAR_TO_UTF8(*DS->GetJSONRepresentation(Pid)));
					if (!DS->GPUStartTime)
					{
						DS->GPUStartTime = FMath::Max(PrevTime, DS->GPUStartTime);
						DS->GPUEndTime = DS->GPUStartTime + 1llu;
						WriteString(OutputFile, TCHAR_TO_UTF8(*DS->GetJSONRepresentation(Pid)));
					}
					PrevTime = DS->GPUEndTime;
				}
			}
			
			delete CmdBufStats;
		}
		TracedBuffers.Empty();
		
		for (FMetalDisplayStats* DisplayStat : DisplayStats)
		{
			DisplayStat->GPUThreadIndex += 3;
			WriteString(OutputFile, TCHAR_TO_UTF8(*DisplayStat->GetJSONRepresentation(Pid)));
			delete DisplayStat;
		}
		DisplayStats.Empty();
		
		for (FMetalCPUStats* CPUStat : CPUStats)
		{
			WriteString(OutputFile, TCHAR_TO_UTF8(*CPUStat->GetJSONRepresentation(Pid)));
			delete CPUStat;
		}
		CPUStats.Empty();
		
		// All done
		
		WriteString(OutputFile, "{}]}");
		
		OutputFile->Close();
	}
}

static void HandleMetalProfileCommand(const TArray<FString>& Args, UWorld*, FOutputDevice& Ar)
{
	if (Args.Num() < 1)
	{
		return;
	}
	FString Param = Args[0];
	if (Param == TEXT("START"))
	{
		FMetalProfiler::GetProfiler()->BeginCapture();
	}
	else if (Param == TEXT("STOP"))
	{
		FMetalProfiler::GetProfiler()->EndCapture();
	}
	else
	{
		int32 CaptureFrames = 0;
		if (FParse::Value(*Param, TEXT("FRAMES="), CaptureFrames))
		{
			FMetalProfiler::GetProfiler()->BeginCapture(CaptureFrames);
		}
	}
}

static FAutoConsoleCommand HandleMetalProfilerCmd(
	TEXT("MetalProfiler"),
	TEXT("Starts or stops Metal profiler"),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&HandleMetalProfileCommand)
);

#endif

#if RHI_NEW_GPU_PROFILER && WITH_RHI_BREADCRUMBS

void FMetalBreadcrumbProfiler::ResolveBreadcrumbTrackerStream(TArray<FMetalBreadcrumbTrackerObject>& BreadcrumbTrackerStream)
{
	for(FMetalBreadcrumbTrackerObject& Tracker : BreadcrumbTrackerStream)
	{
		switch(Tracker.Type)
		{
			case EMetalBreadcrumbTrackerType::Begin:
			{
				OnBreadcrumbBegin(Tracker.Node);
				break;
			}
			case EMetalBreadcrumbTrackerType::End:
			{	
				OnBreadcrumbEnd(Tracker.Node);
				break;
			}
			case EMetalBreadcrumbTrackerType::Encode:
			{
				AddSample(Tracker.CounterSample);
				break;
			}
			default:
				check(0);
		}
	}
}

#endif
