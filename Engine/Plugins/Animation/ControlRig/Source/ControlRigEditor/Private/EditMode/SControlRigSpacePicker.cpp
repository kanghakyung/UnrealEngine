// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditMode/SControlRigSpacePicker.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "AssetRegistry/AssetData.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Layout/SSpacer.h"
#include "Styling/AppStyle.h"
#include "ISequencer.h"
#include "ScopedTransaction.h"
#include "ControlRig.h"
#include "EditMode/ControlRigEditMode.h"
#include "MovieSceneSequence.h"
#include "MovieScene.h"
#include "ControlRigSpaceChannelEditors.h"

#define LOCTEXT_NAMESPACE "ControlRigSpacePicker"

void SControlRigSpacePicker::Construct(const FArguments& InArgs, FControlRigEditMode& InEditMode)
{
	
	ChildSlot
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SNew(SVerticalBox)
				+SVerticalBox::Slot()
				.AutoHeight()
				[
					SAssignNew(PickerExpander, SExpandableArea)
					.InitiallyCollapsed(true)
					.AreaTitle(LOCTEXT("Picker_SpaceWidget", "Spaces"))
					.AreaTitleFont(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
					.BorderBackgroundColor(FLinearColor(.6f, .6f, .6f))
					.Padding(FMargin(8.f))
					.HeaderContent()
					[
						SNew(SHorizontalBox)

						+SHorizontalBox::Slot()
						.AutoWidth()
						.HAlign(HAlign_Left)
						.VAlign(VAlign_Center)
						.Padding(0.f, 0.f, 0.f, 0.f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("Picker_SpaceWidget", "Spaces"))
							.Font(FCoreStyle::Get().GetFontStyle("ExpandableArea.TitleFont"))
						]
					
						+SHorizontalBox::Slot()
						.FillWidth(1.f)
						[
							SNew(SSpacer)
						]

						+SHorizontalBox::Slot()
						.AutoWidth()
						.HAlign(HAlign_Right)
						.VAlign(VAlign_Center)
						.Padding(0.f, 2.f, 8.f, 2.f)
						[
							SNew(SButton)
							.ContentPadding(0.0f)
							.ButtonStyle(FAppStyle::Get(), "NoBorder")
							.OnClicked(this, &SControlRigSpacePicker::HandleAddSpaceClicked)
							.Cursor(EMouseCursor::Default)
							.ToolTipText(LOCTEXT("AddSpace", "Add Space"))
							[
								SNew(SImage)
								.Image(FAppStyle::GetBrush(TEXT("Icons.PlusCircle")))
							]
							.Visibility_Lambda([this]()
							{
								return GetAddSpaceButtonVisibility();
							})
						]
					]
					.BodyContent()
					[
						SAssignNew(SpacePickerWidget, SRigSpacePickerWidget)
						.AllowDelete(true)
						.AllowReorder(true)
						.AllowAdd(false)
						.ShowBakeAndCompensateButton(true)
						.GetControlCustomization(this, &SControlRigSpacePicker::HandleGetControlElementCustomization)
						.OnActiveSpaceChanged(this, &SControlRigSpacePicker::HandleActiveSpaceChanged)
						.OnSpaceListChanged(this, &SControlRigSpacePicker::HandleSpaceListChanged)
						.OnCompensateKeyButtonClicked(this, &SControlRigSpacePicker::OnCompensateKeyClicked)
						.OnCompensateAllButtonClicked(this, &SControlRigSpacePicker::OnCompensateAllClicked)
						.OnBakeButtonClicked(this, &SControlRigSpacePicker::OnBakeControlsToNewSpaceButtonClicked)
						// todo: implement GetAdditionalSpacesDelegate to pull spaces from sequencer
					]
				]

			]
		];

	SetEditMode(InEditMode);
}

SControlRigSpacePicker::~SControlRigSpacePicker()
{
	//base class handles control rig related cleanup
}

UControlRig* SControlRigSpacePicker::GetControlRig() const
{
	TArray<UControlRig*> ControlRigs = GetControlRigs();
	for(UControlRig* ControlRig: ControlRigs)
	{
		if (ControlRig)
		{
			TArray<FRigElementKey> SelectedControls = ControlRig->GetHierarchy()->GetSelectedKeys(ERigElementType::Control);
			if(SelectedControls.Num() >0)
			{
				return ControlRig;
			}
		}
	}
	return nullptr;
}

void SControlRigSpacePicker::HandleControlSelected(UControlRig* Subject, FRigControlElement* ControlElement, bool bSelected)
{
	FControlRigBaseDockableView::HandleControlSelected(Subject, ControlElement, bSelected);
	if (UControlRig* ControlRig = GetControlRig())
	{
		// get the selected controls
		TArray<FRigElementKey> SelectedControls = ControlRig->GetHierarchy()->GetSelectedKeys(ERigElementType::Control);
		SpacePickerWidget->SetControls(ControlRig->GetHierarchy(), SelectedControls);
	}
	else //set nothing
	{
		TArray<FRigElementKey> SelectedControls;
		SpacePickerWidget->SetControls(nullptr, SelectedControls);
	}
}

const FRigControlElementCustomization* SControlRigSpacePicker::HandleGetControlElementCustomization(URigHierarchy* InHierarchy, const FRigElementKey& InControlKey)
{
	if (UControlRig* ControlRig = GetControlRig())
	{
		return ControlRig->GetControlCustomization(InControlKey);
	}
	return nullptr;
}

void SControlRigSpacePicker::HandleActiveSpaceChanged(URigHierarchy* InHierarchy, const FRigElementKey& InControlKey,
	const FRigElementKey& InSpaceKey)
{

	if (ISequencer* Sequencer = GetSequencer())
	{
		if (UControlRig* ControlRig = GetControlRig())
		{
			FString FailureReason;
			FRigVMDependenciesProvider DependencyProvider(InHierarchy, ControlRig->GetVM());
			if (!InHierarchy->CanSwitchToParent(InControlKey, InSpaceKey, DependencyProvider, &FailureReason))
			{
				// notification
				FNotificationInfo Info(FText::FromString(FailureReason));
				Info.bFireAndForget = true;
				Info.FadeOutDuration = 2.0f;
				Info.ExpireDuration = 8.0f;

				const TSharedPtr<SNotificationItem> NotificationPtr = FSlateNotificationManager::Get().AddNotification(Info);
				NotificationPtr->SetCompletionState(SNotificationItem::CS_Fail);
				return;
			}

			if (const FRigControlElement* ControlElement = InHierarchy->Find<FRigControlElement>(InControlKey))
			{
				FScopedTransaction Transaction(LOCTEXT("KeyControlRigSpace", "Key Control Rig Space"));
				ControlRig->Modify();

				FSpaceChannelAndSection SpaceChannelAndSection = FControlRigSpaceChannelHelpers::FindSpaceChannelAndSectionForControl(ControlRig, InControlKey.Name, Sequencer, true /*bCreateIfNeeded*/);
				if (SpaceChannelAndSection.SpaceChannel)
				{
					const FFrameRate TickResolution = Sequencer->GetFocusedTickResolution();
					const FFrameTime FrameTime = Sequencer->GetLocalTime().ConvertTo(TickResolution);
					FFrameNumber CurrentTime = FrameTime.GetFrame();
					FControlRigSpaceChannelHelpers::SequencerKeyControlRigSpaceChannel(ControlRig, Sequencer, SpaceChannelAndSection.SpaceChannel, SpaceChannelAndSection.SectionToKey, CurrentTime, InHierarchy, InControlKey, InSpaceKey);
				}
			}
		}
	}
}

void SControlRigSpacePicker::HandleSpaceListChanged(URigHierarchy* InHierarchy, const FRigElementKey& InControlKey,
	const TArray<FRigElementKeyWithLabel>& InSpaceList)
{
	if (UControlRig* ControlRig = GetControlRig())
	{
		if (const FRigControlElement* ControlElement = InHierarchy->Find<FRigControlElement>(InControlKey))
		{
			FRigControlElementCustomization ControlCustomization = *ControlRig->GetControlCustomization(InControlKey);
			ControlCustomization.AvailableSpaces = InSpaceList;
			ControlCustomization.RemovedSpaces.Reset();

			// remember  the elements which are in the asset's available list but removed by the user
			for (const FRigElementKeyWithLabel& AvailableSpace : ControlElement->Settings.Customization.AvailableSpaces)
			{
				if (ControlCustomization.AvailableSpaces.FindByKey(AvailableSpace.Key) == nullptr)
				{
					ControlCustomization.RemovedSpaces.Add(AvailableSpace.Key);
				}
			}

			ControlRig->SetControlCustomization(InControlKey, ControlCustomization);

			if (FControlRigEditMode* EditMode = static_cast<FControlRigEditMode*>(ModeTools->GetActiveMode(FControlRigEditMode::ModeName)))
			{
				EditMode->SuspendHierarchyNotifs(true);
				InHierarchy->Notify(ERigHierarchyNotification::ControlSettingChanged, ControlElement);
				EditMode->SuspendHierarchyNotifs(false);
			}
			else
			{
				InHierarchy->Notify(ERigHierarchyNotification::ControlSettingChanged, ControlElement);
			}

			SpacePickerWidget->RefreshContents();
		}
	}
}

FReply SControlRigSpacePicker::HandleAddSpaceClicked()
{
	return SpacePickerWidget->HandleAddElementClicked();
}

bool SControlRigSpacePicker::ReadyForBakeOrCompensation() const
{
	if (SpacePickerWidget->GetHierarchy() == nullptr)
	{
		return false;
	}
	if (SpacePickerWidget->GetControls().Num() == 0)
	{
		return false;
	}
	if (!GetControlRig())
	{
		return false;
	}
	ISequencer* Sequencer = GetSequencer();
	if (Sequencer == nullptr || Sequencer->GetFocusedMovieSceneSequence() == nullptr || Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene() == nullptr)
	{
		return false;
	}
	return true;
}

FReply SControlRigSpacePicker::OnCompensateKeyClicked()
{
	if (ReadyForBakeOrCompensation() == false)
	{
		return FReply::Unhandled();
	}
	ISequencer* Sequencer = GetSequencer();
	const FFrameRate TickResolution = Sequencer->GetFocusedTickResolution();
	const FFrameTime FrameTime = Sequencer->GetLocalTime().ConvertTo(TickResolution);
	const TOptional<FFrameNumber> OptionalKeyTime = FrameTime.GetFrame();
	const bool bSetPreviousKey = true;
	Compensate(OptionalKeyTime, bSetPreviousKey);
	return FReply::Handled();
}

FReply SControlRigSpacePicker::OnCompensateAllClicked()
{
	if (ReadyForBakeOrCompensation() == false)
	{
		return FReply::Unhandled();
	}
	const TOptional<FFrameNumber> OptionalKeyTime;
	ISequencer* Sequencer = GetSequencer();
	const bool bSetPreviousKey = true;
	Compensate(OptionalKeyTime, bSetPreviousKey);
	return FReply::Handled();
}



void SControlRigSpacePicker::Compensate(TOptional<FFrameNumber> OptionalKeyTime, bool bSetPreviousTick)
{
	if (ReadyForBakeOrCompensation() == false)
	{
		return;
	}
	ISequencer* Sequencer = GetSequencer();
	UControlRig* ControlRig = GetControlRig(); //!!!!! THIS SHOULD SUPPORT MULTIPLE!

	if (ControlRig && SpacePickerWidget->GetHierarchy() == ControlRig->GetHierarchy())
	{
		// compensate spaces
		if (UMovieSceneControlRigParameterSection* CRSection = FControlRigSpaceChannelHelpers::GetControlRigSection(Sequencer, ControlRig))
		{
			// compensate spaces
			FControlRigSpaceChannelHelpers::CompensateIfNeeded(
				ControlRig, Sequencer, CRSection,
				OptionalKeyTime, bSetPreviousTick);
		}
	}
}

FReply SControlRigSpacePicker::OnBakeControlsToNewSpaceButtonClicked()
{
	if (ReadyForBakeOrCompensation() == false)
	{
		return FReply::Unhandled();
	}

	ISequencer* Sequencer = GetSequencer();
	UControlRig* ControlRig = GetControlRig();

	FRigSpacePickerBakeSettings Settings;
	//Find default target space, just use first control and find space at current sequencer time
	//Then Find range

	// FindSpaceChannelAndSectionForControl() will trigger RecreateCurveEditor(), which will deselect the controls
	// but in theory the selection will be recovered in the next tick, so here we just cache the selected controls
	// and use it throughout this function. If this deselection is causing other problems, this part could use a revisit.
	TArray<FRigElementKey> ControlKeys = SpacePickerWidget->GetControls();

	FSpaceChannelAndSection SpaceChannelAndSection = FControlRigSpaceChannelHelpers::FindSpaceChannelAndSectionForControl(ControlRig, ControlKeys[0].Name, Sequencer, true /*bCreateIfNeeded*/);
	if (SpaceChannelAndSection.SpaceChannel != nullptr)
	{
		using namespace UE::MovieScene;

		Settings.TargetSpace = URigHierarchy::GetDefaultParentKey();

		TRange<FFrameNumber> Range = Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene()->GetPlaybackRange();

		Settings.Settings.StartFrame = Range.GetLowerBoundValue();
		Settings.Settings.EndFrame = Range.GetUpperBoundValue();

		TSharedRef<SRigSpacePickerBakeWidget> BakeWidget =
			SNew(SRigSpacePickerBakeWidget)
			.Settings(Settings)
			.Hierarchy(SpacePickerWidget->GetHierarchy())
			.Controls(ControlKeys) // use the cached controls here since the selection is not recovered until next tick.
			.Sequencer(Sequencer)
			.GetControlCustomization(this, &SControlRigSpacePicker::HandleGetControlElementCustomization)
			.OnBake_Lambda([Sequencer, ControlRig](URigHierarchy* InHierarchy, TArray<FRigElementKey> InControls, FRigSpacePickerBakeSettings InSettings)
		{
	
			FScopedTransaction Transaction(LOCTEXT("BakeControlToSpace", "Bake Control In Space"));
			for (const FRigElementKey& ControlKey : InControls)
			{
				//when baking we will now create a channel if one doesn't exist, was causing confusion
				FSpaceChannelAndSection SpaceChannelAndSection = FControlRigSpaceChannelHelpers::FindSpaceChannelAndSectionForControl(ControlRig, ControlKey.Name, Sequencer, true /*bCreateIfNeeded*/);
				if (SpaceChannelAndSection.SpaceChannel)
				{
					FControlRigSpaceChannelHelpers::SequencerBakeControlInSpace(ControlRig, Sequencer, SpaceChannelAndSection.SpaceChannel, SpaceChannelAndSection.SectionToKey,
						InHierarchy, ControlKey, InSettings);
				}
			}
			return FReply::Handled();
		});

		return BakeWidget->OpenDialog(true);
	}
	return FReply::Unhandled();
}

EVisibility SControlRigSpacePicker::GetAddSpaceButtonVisibility() const
{
	if(const URigHierarchy* Hierarchy = SpacePickerWidget->GetHierarchy())
	{
		for(const FRigElementKey& Control : SpacePickerWidget->GetControls())
		{
			if(const FRigControlElement* ControlElement = Hierarchy->Find<FRigControlElement>(Control))
			{
				if(ControlElement->Settings.bRestrictSpaceSwitching)
				{
					return EVisibility::Collapsed;
				}
			}
		}
	}

	return EVisibility::Visible;
}

#undef LOCTEXT_NAMESPACE
