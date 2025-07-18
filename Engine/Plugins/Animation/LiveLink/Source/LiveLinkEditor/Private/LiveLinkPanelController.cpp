// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLinkPanelController.h"
#include "Async/TaskGraphInterfaces.h"
#include "Framework/Commands/UICommandList.h"
#include "Features/IModularFeatures.h"
#include "IDetailsView.h"
#include "LiveLinkClient.h"
#include "LiveLinkClientCommands.h"
#include "LiveLinkClientPanelViews.h"
#include "Misc/ConfigCacheIni.h"
#include "SLiveLinkDataView.h"


FLiveLinkPanelController::FLiveLinkPanelController(TAttribute<bool> bInReadOnly)
{
	bSeparateSourcesSubjects = GConfig->GetBoolOrDefault(TEXT("LiveLink"), TEXT("bPanelControllerSeparateSourcesSubjects"), false, GEngineIni);

	Client = (FLiveLinkClient*)&IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);

	OnSourcesChangedHandle = Client->OnLiveLinkSourcesChanged().AddRaw(this, &FLiveLinkPanelController::OnSourcesChangedHandler);
	OnSubjectsChangedHandle = Client->OnLiveLinkSubjectsChanged().AddRaw(this, &FLiveLinkPanelController::OnSubjectsChangedHandler);

	FLiveLinkClientCommands::Register();
	CommandList = MakeShared<FUICommandList>();
	BindCommands();

	SourcesView = MakeShared<FLiveLinkSourcesView>(Client, CommandList, bInReadOnly, FLiveLinkSourcesView::FOnSourceSelectionChanged::CreateRaw(this, &FLiveLinkPanelController::OnSourceSelectionChangedHandler));
	SubjectsView = MakeShared<FLiveLinkSubjectsView>(FLiveLinkSubjectsView::FOnSubjectSelectionChanged::CreateRaw(this, &FLiveLinkPanelController::OnSubjectSelectionChangedHandler), CommandList, bInReadOnly);
	SourcesDetailsView = UE::LiveLink::CreateSourcesDetailsView(SourcesView, bInReadOnly);
	SubjectsDetailsView = UE::LiveLink::CreateSubjectsDetailsView(Client, bInReadOnly);

	RebuildSourceList();
	RebuildSubjectList();
}

FLiveLinkPanelController::~FLiveLinkPanelController()
{
	if (IModularFeatures::Get().IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
	{
		ILiveLinkClient* LiveLinkClient = (FLiveLinkClient*)&IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
		if (LiveLinkClient)
		{
			LiveLinkClient->OnLiveLinkSourcesChanged().Remove(OnSourcesChangedHandle);
			OnSourcesChangedHandle.Reset();

			LiveLinkClient->OnLiveLinkSubjectsChanged().Remove(OnSubjectsChangedHandle);
			OnSubjectsChangedHandle.Reset();
		}
	}
}

void FLiveLinkPanelController::OnSourceSelectionChangedHandler(FLiveLinkSourceUIEntryPtr Entry, ESelectInfo::Type SelectionType) const
{
	if (bSelectionChangedGuard)
	{
		return;
	}
	
	TGuardValue<bool> ReentrantGuard(bSelectionChangedGuard, true);
	
	SubjectsDetailsView->SetSubjectKey(FLiveLinkSubjectKey());

	int32 FoundSubjectEntryIndex = INDEX_NONE;
	if (Entry.IsValid())
	{
		SourcesDetailsView->SetObject(Entry->GetSourceSettings());

		// Find the corresponding subject
		FoundSubjectEntryIndex = SubjectsView->SubjectData.IndexOfByPredicate([Entry](const FLiveLinkSubjectUIEntryPtr& SubjectEntry) { return SubjectEntry->SubjectKey.Source == Entry->GetGuid() && SubjectEntry->IsSource(); });
	}
	else
	{
		SourcesDetailsView->SetObject(nullptr);
	}

	// Set the corresponding subject
	if (FoundSubjectEntryIndex != INDEX_NONE)
	{
		SubjectsView->SubjectsTreeView->SetSelection(SubjectsView->SubjectData[FoundSubjectEntryIndex]);
	}
	else
	{
		SubjectsView->SubjectsTreeView->ClearSelection();
	}
}

void FLiveLinkPanelController::OnSubjectSelectionChangedHandler(FLiveLinkSubjectUIEntryPtr SubjectEntry, ESelectInfo::Type SelectInfo)
{
	if (bSelectionChangedGuard)
	{
		return;
	}
	TGuardValue<bool> ReentrantGuard(bSelectionChangedGuard, true);

	int32 FoundSourceIndex = INDEX_NONE;
	bool bDetailViewSet = false;
	if (SubjectEntry.IsValid())
	{
		// Find the corresponding Source entry
		FGuid SourceGuid = SubjectEntry->SubjectKey.Source;
		FoundSourceIndex = SourcesView->SourceData.IndexOfByPredicate([SourceGuid](FLiveLinkSourceUIEntryPtr SourceEntry) { return SourceEntry->GetGuid() == SourceGuid; });

		if (SubjectEntry->IsSource())
		{
			SourcesDetailsView->SetObject(SubjectEntry->GetSettings());
			if (!bSeparateSourcesSubjects)
			{
				SubjectsDetailsView->SetSubjectKey(FLiveLinkSubjectKey());
			}
		}
		else
		{
			if (!bSeparateSourcesSubjects)
			{
				SourcesDetailsView->SetObject(nullptr);
			}
			SubjectsDetailsView->SetSubjectKey(SubjectEntry->SubjectKey);
		}
		bDetailViewSet = true;

		SubjectSelectionChangedDelegate.Broadcast(SubjectEntry->SubjectKey);
	}

	if (!bDetailViewSet)
	{
		if (!bSeparateSourcesSubjects)
		{
			SourcesDetailsView->SetObject(nullptr);
		}
		SubjectsDetailsView->SetSubjectKey(FLiveLinkSubjectKey());
	}

	// Select the corresponding Source entry
	if (FoundSourceIndex != INDEX_NONE)
	{
		SourcesView->SourcesListView->SetSelection(SourcesView->SourceData[FoundSourceIndex]);

		if (bSeparateSourcesSubjects)
		{
			// Update source details to the selected subject's source.
			SourcesDetailsView->SetObject(SourcesView->SourceData[FoundSourceIndex]->GetSourceSettings());
		}
	}
	else
	{
		SourcesView->SourcesListView->ClearSelection();
	}
}

void FLiveLinkPanelController::BindCommands()
{
	CommandList->MapAction(FLiveLinkClientCommands::Get().RemoveSource,
		FExecuteAction::CreateRaw(this, &FLiveLinkPanelController::HandleRemoveSource),
		FCanExecuteAction::CreateRaw(this, &FLiveLinkPanelController::CanRemoveSource)
	);

	CommandList->MapAction(FLiveLinkClientCommands::Get().RemoveAllSources,
		FExecuteAction::CreateRaw(this, &FLiveLinkPanelController::HandleRemoveAllSources),
		FCanExecuteAction::CreateRaw(this, &FLiveLinkPanelController::HasSource)
	);

	CommandList->MapAction(FLiveLinkClientCommands::Get().RemoveSubject,
		FExecuteAction::CreateRaw(this, &FLiveLinkPanelController::HandleRemoveSubject),
		FCanExecuteAction::CreateRaw(this, &FLiveLinkPanelController::CanRemoveSubject)
	);

	CommandList->MapAction(FLiveLinkClientCommands::Get().PauseSubject,
		FExecuteAction::CreateRaw(this, &FLiveLinkPanelController::HandlePauseSubject),
		FCanExecuteAction::CreateRaw(this, &FLiveLinkPanelController::CanPauseSubject)
	);
}

void FLiveLinkPanelController::OnSourcesChangedHandler()
{
	// Since this can be called from any thread, make sure we only update slate on the game thread.
	FFunctionGraphTask::CreateAndDispatchWhenReady([this]()
	{
		SourcesView->RefreshSourceData(true);
		RebuildSubjectList();
	}, TStatId(), nullptr, ENamedThreads::GameThread);
}

void FLiveLinkPanelController::OnSubjectsChangedHandler()
{
	// Since this can be called from any thread, make sure we only update slate on the game thread.
	FFunctionGraphTask::CreateAndDispatchWhenReady([this]()
	{
		RebuildSubjectList();
		SourcesDetailsView->ForceRefresh();
	}, TStatId(), nullptr, ENamedThreads::GameThread);
}

void FLiveLinkPanelController::RebuildSourceList()
{
	SourcesView->RefreshSourceData(true);
}

void FLiveLinkPanelController::RebuildSubjectList()
{
	if (SubjectsView)
	{
		SubjectsView->RefreshSubjects();
	}
}

bool FLiveLinkPanelController::HasSource() const
{
	constexpr bool bIncludeVirtualSources = true;
	return Client->GetDisplayableSources(bIncludeVirtualSources).Num() > 0;
}

bool FLiveLinkPanelController::CanRemoveSource() const
{
	return SourcesView->SourcesListView->GetNumItemsSelected() > 0;
}

void FLiveLinkPanelController::HandleRemoveSource()
{
	TArray<FLiveLinkSourceUIEntryPtr> Selected;
	SourcesView->SourcesListView->GetSelectedItems(Selected);
	if (Selected.Num() > 0)
	{
		Selected[0]->RemoveFromClient();
	}
}

void FLiveLinkPanelController::HandleRemoveAllSources()
{
	Client->RemoveAllSources();
}

bool FLiveLinkPanelController::CanRemoveSubject() const
{
	return SubjectsView->CanRemoveSubject();
}

void FLiveLinkPanelController::HandleRemoveSubject()
{
	TArray<FLiveLinkSubjectUIEntryPtr> Selected;
	SubjectsView->SubjectsTreeView->GetSelectedItems(Selected);
	if (Selected.Num() > 0 && Selected[0])
	{
		Selected[0]->RemoveFromClient();
	}
}

bool FLiveLinkPanelController::CanPauseSubject() const
{
	return SubjectsView->CanPauseSubject();
}

void FLiveLinkPanelController::HandlePauseSubject()
{
	TArray<FLiveLinkSubjectUIEntryPtr> Selected;
	SubjectsView->SubjectsTreeView->GetSelectedItems(Selected);
	if (Selected.Num() > 0 && Selected[0])
	{
		Selected[0]->PauseSubject();
	}
}
