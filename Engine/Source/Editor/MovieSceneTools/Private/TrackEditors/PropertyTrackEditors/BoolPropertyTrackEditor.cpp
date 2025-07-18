// Copyright Epic Games, Inc. All Rights Reserved.

#include "TrackEditors/PropertyTrackEditors/BoolPropertyTrackEditor.h"

#include "Channels/MovieSceneBoolChannel.h"
#include "KeyPropertyParams.h"
#include "Misc/Guid.h"
#include "Misc/Optional.h"
#include "Sections/BoolPropertySection.h"

class ISequencer;
class ISequencerSection;
class ISequencerTrackEditor;
class UMovieSceneSection;
class UMovieSceneTrack;

#define LOCTEXT_NAMESPACE "BoolPropertyTrackEditor"

TSharedRef<ISequencerTrackEditor> FBoolPropertyTrackEditor::CreateTrackEditor(TSharedRef<ISequencer> OwningSequencer)
{
	return MakeShareable(new FBoolPropertyTrackEditor(OwningSequencer));
}

FText FBoolPropertyTrackEditor::GetDisplayName() const
{
	return LOCTEXT("BoolPropertyTrackEditor_DisplayName", "Bool Property");
}

TSharedRef<ISequencerSection> FBoolPropertyTrackEditor::MakeSectionInterface(UMovieSceneSection& SectionObject, UMovieSceneTrack& Track, FGuid ObjectBinding)
{
	return MakeShared<FBoolPropertySection>(SectionObject);
}

void FBoolPropertyTrackEditor::GenerateKeysFromPropertyChanged( const FPropertyChangedParams& PropertyChangedParams, UMovieSceneSection* SectionToKey, FGeneratedTrackKeys& OutGeneratedKeys )
{
	const bool KeyedValue = PropertyChangedParams.GetPropertyValue<bool>();
	OutGeneratedKeys.Add(FMovieSceneChannelValueSetter::Create<FMovieSceneBoolChannel>(0, KeyedValue, true));
}

#undef LOCTEXT_NAMESPACE
