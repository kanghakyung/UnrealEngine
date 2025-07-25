// Copyright Epic Games, Inc. All Rights Reserved.

#include "Scripting/SequencerModuleOutlinerScriptingObject.h"
#include "Scripting/ViewModelScriptingStruct.h"

#include "MVVM/ViewModels/SequencerOutlinerViewModel.h"
#include "MVVM/ViewModels/ViewModel.h"
#include "MVVM/ViewModels/ViewModelIterators.h"
#include "MVVM/ViewModels/OutlinerViewModel.h"
#include "MVVM/ViewModels/EditorViewModel.h"
#include "MVVM/ViewModelPtr.h"

#include "Containers/Array.h"
#include "Misc/FrameNumber.h"

#include "SequencerCommonHelpers.h"

#define LOCTEXT_NAMESPACE "SequencerModuleOutlinerScriptingObject"

TArray<UMovieSceneSection*> USequencerModuleOutlinerScriptingObject::GetSections(const TArray<FSequencerViewModelScriptingStruct>& InNodes) const
{
	using namespace UE::Sequencer;

	TViewModelPtr<FSequencerOutlinerViewModel> Outliner = CastViewModel<FSequencerOutlinerViewModel>(WeakOutliner.Pin());
	if (!Outliner)
	{
		FFrame::KismetExecutionMessage(*LOCTEXT("OutlinerInvalid", "Outliner is no longer valid.").ToString(), ELogVerbosity::Error);
		return TArray<UMovieSceneSection*>();
	}

	TSet<TWeakObjectPtr<UMovieSceneSection> > AllSections;
	TArray<TSharedRef<FViewModel>> Nodes;
	for (const FSequencerViewModelScriptingStruct& InNode : InNodes)
	{
		FViewModelPtr Item = InNode.WeakViewModel.ImplicitPin();
		if (Item)
		{
			SequencerHelpers::GetAllSections(Item, AllSections);
		}
	}

	TArray<UMovieSceneSection*> Sections;
	for (TWeakObjectPtr<UMovieSceneSection> WeakSection : AllSections)
	{
		if (WeakSection.IsValid())
		{
			Sections.Add(WeakSection.Get());
		}
	}
	return Sections;
}

FFrameNumber USequencerModuleOutlinerScriptingObject::GetNextKey(const TArray<FSequencerViewModelScriptingStruct>& InNodes, FFrameNumber FrameNumber, EMovieSceneTimeUnit TimeUnit) const
{
	using namespace UE::Sequencer;

	TViewModelPtr<FSequencerOutlinerViewModel> Outliner = CastViewModel<FSequencerOutlinerViewModel>(WeakOutliner.Pin());
	if (!Outliner)
	{
		FFrame::KismetExecutionMessage(*LOCTEXT("OutlinerInvalid", "Outliner is no longer valid.").ToString(), ELogVerbosity::Error);
		return FFrameNumber();
	}

	TRange<FFrameNumber> Range = TRange<FFrameNumber>::All();

	TArray<TSharedRef<FViewModel>> Nodes;
	for (const FSequencerViewModelScriptingStruct& InNode : InNodes)
	{
		FViewModelPtr Item = InNode.WeakViewModel.ImplicitPin();
		if (Item)
		{
			Nodes.Add(Item.ToSharedRef());
		}
	}

	return Outliner->GetNextKey(Nodes, FrameNumber, TimeUnit, Range);
}

FFrameNumber USequencerModuleOutlinerScriptingObject::GetPreviousKey(const TArray<FSequencerViewModelScriptingStruct>& InNodes, FFrameNumber FrameNumber, EMovieSceneTimeUnit TimeUnit) const
{
	using namespace UE::Sequencer;

	TViewModelPtr<FSequencerOutlinerViewModel> Outliner = CastViewModel<FSequencerOutlinerViewModel>(WeakOutliner.Pin());
	if (!Outliner)
	{
		FFrame::KismetExecutionMessage(*LOCTEXT("OutlinerInvalid", "Outliner is no longer valid.").ToString(), ELogVerbosity::Error);
		return FFrameNumber();
	}

	TRange<FFrameNumber> Range = TRange<FFrameNumber>::All();

	TArray<TSharedRef<FViewModel>> Nodes;
	for (const FSequencerViewModelScriptingStruct& InNode : InNodes)
	{
		TViewModelPtr<FViewModel> Item = InNode.WeakViewModel.ImplicitPin();
		if (Item)
		{
			Nodes.Add(Item.ToSharedRef());
		}
	}

	return Outliner->GetPreviousKey(Nodes, FrameNumber, TimeUnit, Range);
}

#undef LOCTEXT_NAMESPACE
