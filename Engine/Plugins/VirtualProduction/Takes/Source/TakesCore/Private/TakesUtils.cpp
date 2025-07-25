// Copyright Epic Games, Inc. All Rights Reserved.

#include "TakesUtils.h"
#include "AssetRegistry/AssetData.h"
#include "TakesCoreLog.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "LevelSequence.h"
#include "ILevelSequenceEditorToolkit.h"
#include "MovieScene.h"
#include "Math/Range.h"
#include "MovieSceneSection.h"
#include "MovieSceneTimeHelpers.h"
#include "HAL/FileManager.h"
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "Sections/MovieSceneCameraCutSection.h"
#include "UObject/SavePackage.h"

#define LOCTEXT_NAMESPACE "TakesUtils"

namespace TakesUtils
{

/** Helper function - get the first PIE world (or first PIE client world if there is more than one) */
UWorld* GetFirstPIEWorld()
{
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (Context.World()->IsPlayInEditor())
		{
			if (Context.World()->GetNetMode() == ENetMode::NM_Standalone ||
				(Context.World()->GetNetMode() == ENetMode::NM_Client && Context.PIEInstance == 2))
			{
				return Context.World();
			}
		}
	}

	return nullptr;
}

void ClampPlaybackRangeToEncompassAllSections(UMovieScene* InMovieScene, bool bUpperBoundOnly)
{
	check(InMovieScene);

	TOptional < TRange<FFrameNumber> > PlayRange;

	TArray<UMovieSceneSection*> MovieSceneSections = InMovieScene->GetAllSections();
	for (UMovieSceneSection* Section : MovieSceneSections)
	{
		TRange<FFrameNumber> SectionRange = Section->GetRange();
		if (SectionRange.GetLowerBound().IsClosed() && SectionRange.GetUpperBound().IsClosed())
		{
			if (!PlayRange.IsSet())
			{
				PlayRange = SectionRange;
			}
			else
			{
				PlayRange = TRange<FFrameNumber>::Hull(PlayRange.GetValue(), SectionRange);
			}
		}
	}

	if (!PlayRange.IsSet())
	{
		return;
	}

	// Extend only the upper bound because the start was set at the beginning of recording
	if (bUpperBoundOnly)
	{
		PlayRange.GetValue().SetLowerBoundValue(InMovieScene->GetPlaybackRange().GetLowerBoundValue());
	}

	InMovieScene->SetPlaybackRange(PlayRange.GetValue());
}

void ResetViewAndWorkRange(UMovieScene* InMovieScene)
{
	TRange<FFrameNumber> PlayRange = InMovieScene->GetPlaybackRange();

	// Initialize the working and view range with a little bit more space
	FFrameRate  TickResolution = InMovieScene->GetTickResolution();
	const double OutputViewSize = PlayRange.Size<FFrameNumber>() / TickResolution;
	const double OutputChange = OutputViewSize * 0.1;

	TRange<double> NewRange = UE::MovieScene::ExpandRange(PlayRange / TickResolution, OutputChange);
	FMovieSceneEditorData& EditorData = InMovieScene->GetEditorData();
	EditorData.ViewStart = EditorData.WorkStart = NewRange.GetLowerBoundValue();
	EditorData.ViewEnd = EditorData.WorkEnd = NewRange.GetUpperBoundValue();
}

void SaveAsset(UObject* InObject)
{
	if (!InObject)
	{
		return;
	}

	// auto-save asset outside of the editor
	UPackage* const Package = InObject->GetOutermost();
	FString const PackageName = Package->GetName();
	FString const PackageFileName = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());

	if (IFileManager::Get().IsReadOnly(*PackageFileName))
	{
		UE_LOG(LogTakesCore, Error, TEXT("Could not save read only file: %s"), *PackageFileName);
		return;
	}

	double StartTime = FPlatformTime::Seconds();

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError | SAVE_Async;
	UPackage::SavePackage(Package, NULL, *PackageFileName, SaveArgs);

	double ElapsedTime = FPlatformTime::Seconds() - StartTime;
	UE_LOG(LogTakesCore, Log, TEXT("Saved %s in %0.2f seconds"), *PackageName, ElapsedTime);
}

void CreateCameraCutTrack(ULevelSequence* LevelSequence, const FGuid& RecordedCameraGuid, const FMovieSceneSequenceID& SequenceID, const TRange<FFrameNumber>& InRange)
{
	if (!RecordedCameraGuid.IsValid() || !LevelSequence)
	{
		return;
	}

	UMovieSceneTrack* CameraCutTrack = LevelSequence->GetMovieScene()->GetCameraCutTrack();
	if (CameraCutTrack && CameraCutTrack->GetAllSections().Num() > 1)
	{
		return;
	}


	if (!CameraCutTrack)
	{
		CameraCutTrack = LevelSequence->GetMovieScene()->AddCameraCutTrack(UMovieSceneCameraCutTrack::StaticClass());
	}
	else
	{
		CameraCutTrack->RemoveAllAnimationData();
	}

	UMovieSceneCameraCutSection* CameraCutSection = Cast<UMovieSceneCameraCutSection>(CameraCutTrack->CreateNewSection());
	CameraCutSection->SetCameraBindingID(UE::MovieScene::FRelativeObjectBindingID(RecordedCameraGuid, SequenceID));
	CameraCutSection->SetRange(InRange);
	CameraCutTrack->AddSection(*CameraCutSection);
}

UWorld* DiscoverSourceWorld()
{
	UWorld* SourceWorld = nullptr;

	for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
	{
		if (WorldContext.WorldType == EWorldType::PIE || WorldContext.WorldType == EWorldType::Game )
		{
			SourceWorld = WorldContext.World();
			break;
		}
		if (WorldContext.WorldType == EWorldType::Editor)
		{
			SourceWorld = WorldContext.World();
		}
	}

	check(SourceWorld);
	return SourceWorld;
}

TSharedPtr<ISequencer> OpenSequencer(ULevelSequence* LevelSequence, FText* OutError)
{
	TSharedPtr<ISequencer> Sequencer;
	if ( GEditor != nullptr )
	{
		// Open the sequence and set the sequencer ptr
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(LevelSequence);

		IAssetEditorInstance*        AssetEditor         = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorForAsset(LevelSequence, false);
		ILevelSequenceEditorToolkit* LevelSequenceEditor = static_cast<ILevelSequenceEditorToolkit*>(AssetEditor);

		Sequencer = LevelSequenceEditor ? LevelSequenceEditor->GetSequencer() : nullptr;

		if (!Sequencer.IsValid() && OutError)
		{
			*OutError = FText::Format(LOCTEXT("FailedToOpenSequencerError", "Failed to open Sequencer for asset '{0}."), FText::FromString(LevelSequence->GetPathName()));
		}
	}
		
	return Sequencer;
}

FQualifiedFrameTime GetRecordTime(TSharedPtr<ISequencer> Sequencer, ULevelSequence* SequenceAsset, const FTimecode& TimecodeAtStart, bool bStartAtCurrentTimecode)
{
	FQualifiedFrameTime RecordTime;

	if (Sequencer.IsValid())
	{
		RecordTime = Sequencer->GetGlobalTime();
	}
	else if (SequenceAsset)
	{
		if (UMovieScene* MovieScene = SequenceAsset->GetMovieScene())
		{
			FFrameRate FrameRate = MovieScene->GetDisplayRate();
			FFrameRate TickResolution = MovieScene->GetTickResolution();

			FTimecode CurrentTimecode = FApp::GetTimecode();
		
			FFrameNumber CurrentFrame = FFrameRate::TransformTime(FFrameTime(CurrentTimecode.ToFrameNumber(FrameRate)), FrameRate, TickResolution).FloorToFrame();
			FFrameNumber FrameAtStart = FFrameRate::TransformTime(FFrameTime(TimecodeAtStart.ToFrameNumber(FrameRate)), FrameRate, TickResolution).FloorToFrame();

			if (bStartAtCurrentTimecode)
			{
				RecordTime = FQualifiedFrameTime(CurrentFrame, TickResolution);
			}
			else
			{
				RecordTime = FQualifiedFrameTime(CurrentFrame - FrameAtStart, TickResolution);
			}
		}
	}

	return RecordTime;
}
}

#undef LOCTEXT_NAMESPACE
