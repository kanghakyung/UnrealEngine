// Copyright Epic Games, Inc. All Rights Reserved.

#include "Editor/SRigSpacePickerWidget.h"
#include "DetailLayoutBuilder.h"
#include "Editor.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Editor/SRigHierarchyTreeView.h"
#include "ControlRigEditorStyle.h"
#include "PropertyCustomizationHelpers.h"
#include "ISequencer.h"
#include "Widgets/Input/NumericTypeInterface.h"
#include "FrameNumberDetailsCustomization.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Modules/ModuleManager.h"
#include "ControlRig.h"
#include "ControlRigBlueprint.h"
#include "RigVMBlueprintGeneratedClass.h"

#define LOCTEXT_NAMESPACE "SRigSpacePickerWidget"

//////////////////////////////////////////////////////////////
/// SRigSpacePickerWidget
///////////////////////////////////////////////////////////

FRigElementKey SRigSpacePickerWidget::InValidKey;

void SRigSpacePickerWidget::Construct(const FArguments& InArgs)
{
	GEditor->RegisterForUndo(this);

	bShowDefaultSpaces = InArgs._ShowDefaultSpaces;
	bShowFavoriteSpaces = InArgs._ShowFavoriteSpaces;
	bShowAdditionalSpaces = InArgs._ShowAdditionalSpaces;
	bAllowReorder = InArgs._AllowReorder;
	bAllowDelete = InArgs._AllowDelete;
	bAllowAdd = InArgs._AllowAdd;
	bShowBakeAndCompensateButton = InArgs._ShowBakeAndCompensateButton;
	GetActiveSpaceDelegate = InArgs._GetActiveSpace;
	GetControlCustomizationDelegate = InArgs._GetControlCustomization;
	GetAdditionalSpacesDelegate = InArgs._GetAdditionalSpaces;
	bRepopulateRequired = false;
	bLaunchingContextMenu = false;

	if(!GetActiveSpaceDelegate.IsBound())
	{
		GetActiveSpaceDelegate = FRigSpacePickerGetActiveSpace::CreateRaw(this, &SRigSpacePickerWidget::GetActiveSpace_Private);
	}
	if(!GetAdditionalSpacesDelegate.IsBound())
	{
		GetAdditionalSpacesDelegate = FRigSpacePickerGetAdditionalSpaces::CreateRaw(this, &SRigSpacePickerWidget::GetCurrentParents_Private);
	}

	if(InArgs._OnActiveSpaceChanged.IsBound())
	{
		OnActiveSpaceChanged().Add(InArgs._OnActiveSpaceChanged);
	}
	if(InArgs._OnSpaceListChanged.IsBound())
	{
		OnSpaceListChanged().Add(InArgs._OnSpaceListChanged);
	}

	Hierarchy = nullptr;
	ControlKeys.Reset();

	ChildSlot
	[
		SNew(SBorder)
		.Visibility(EVisibility::Visible)
		.BorderImage(InArgs._BackgroundBrush)
		[
			SAssignNew(TopLevelListBox, SVerticalBox)
		]
	];

	if(!InArgs._Title.IsEmpty())
	{
		TopLevelListBox->AddSlot()
		.AutoHeight()
		.VAlign(VAlign_Top)
		.HAlign(HAlign_Left)
		.Padding(4.0, 0.0, 4.0, 12.0)
		[
			SNew( STextBlock )
			.Text( InArgs._Title )
			.Font( IDetailLayoutBuilder::GetDetailFontBold() )
		];
	}

	if(InArgs._ShowDefaultSpaces)
	{
		AddSpacePickerRow(
			TopLevelListBox,
			ESpacePickerType_Parent,
			URigHierarchy::GetDefaultParentKey(),
			FAppStyle::Get().GetBrush("Icons.Transform"),
			FSlateColor::UseForeground(),
			LOCTEXT("Parent", "Parent"),
			FOnClicked::CreateSP(this, &SRigSpacePickerWidget::HandleParentSpaceClicked)
		);
		
		AddSpacePickerRow(
			TopLevelListBox,
			ESpacePickerType_World,
			URigHierarchy::GetWorldSpaceReferenceKey(),
			FAppStyle::GetBrush("EditorViewport.RelativeCoordinateSystem_World"),
			FSlateColor::UseForeground(),
			LOCTEXT("World", "World"),
			FOnClicked::CreateSP(this, &SRigSpacePickerWidget::HandleWorldSpaceClicked)
		);
	}

	TopLevelListBox->AddSlot()
	.AutoHeight()
	.VAlign(VAlign_Top)
	.HAlign(HAlign_Fill)
	.Padding(0.0)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Fill)
		.Padding(0)
		[
			SAssignNew(ItemSpacesListBox, SVerticalBox)
		]
	];

	if(bAllowAdd || bShowBakeAndCompensateButton)
	{
		TopLevelListBox->AddSlot()
		.AutoHeight()
		.VAlign(VAlign_Top)
		.HAlign(HAlign_Fill)
		.Padding(11.f, 8.f, 4.f, 4.f)
		[
			SAssignNew(BottomButtonsListBox, SHorizontalBox)
		];

		if(bAllowAdd)
		{
			BottomButtonsListBox->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Left)
			.Padding(0.f)
			[
				SNew(SButton)
				.ContentPadding(0.0f)
				.ButtonStyle(FAppStyle::Get(), "NoBorder")
				.OnClicked(this, &SRigSpacePickerWidget::HandleAddElementClicked)
				.Cursor(EMouseCursor::Default)
				.ToolTipText(LOCTEXT("AddSpace", "Add Space"))
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush(TEXT("Icons.PlusCircle")))
				]
				.Visibility_Lambda([this]()
				{
					return IsRestricted() ? EVisibility::Collapsed : EVisibility::Visible;
				})
			];
		}

		BottomButtonsListBox->AddSlot()
		.FillWidth(1.f)
		.HAlign(HAlign_Fill)
		[
			SNew(SSpacer)
		];

		if(bShowBakeAndCompensateButton)
		{
			BottomButtonsListBox->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Left)
			.Padding(0.f)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.Text(LOCTEXT("CompensateKeyButton", "Comp Key"))
				.OnClicked(InArgs._OnCompensateKeyButtonClicked)
				.IsEnabled_Lambda([this]()
				{
					return (ControlKeys.Num() > 0);
				})
				.ToolTipText(LOCTEXT("CompensateKeyTooltip", "Compensate key at the current time."))
			];
			BottomButtonsListBox->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Left)
			.Padding(0.f)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.Text(LOCTEXT("CompensateAllButton", "Comp All"))
				.OnClicked(InArgs._OnCompensateAllButtonClicked)
				.IsEnabled_Lambda([this]()
				{
					return (ControlKeys.Num() > 0);
				})
				.ToolTipText(LOCTEXT("CompensateAllTooltip", "Compensate all space switch keys."))
			];
			BottomButtonsListBox->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Right)
			.Padding(0.f)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.Default")
				.Text(LOCTEXT("BakeButton", "Bake..."))
				.OnClicked(InArgs._OnBakeButtonClicked)
				.IsEnabled_Lambda([this]()
				{
					return (ControlKeys.Num() > 0);
				})
				.ToolTipText(LOCTEXT("BakeButtonToolTip", "Allows to bake the animation of one or more controls to a single space."))
			];
		}
	}

	SetControls(InArgs._Hierarchy, InArgs._Controls);
	SetCanTick(true);
}

SRigSpacePickerWidget::~SRigSpacePickerWidget()
{
	UnregisterPendingSelection();
	
	GEditor->UnregisterForUndo(this);

	if(HierarchyModifiedHandle.IsValid())
	{
		if(Hierarchy.IsValid())
		{
			Hierarchy->OnModified().Remove(HierarchyModifiedHandle);
			HierarchyModifiedHandle.Reset();
		}
	}
}

void SRigSpacePickerWidget::SetControls(URigHierarchy* InHierarchy, const TArray<FRigElementKey>& InControls)
{
	UnregisterPendingSelection();
	
	PendingSelectionHandle = RegisterActiveTimer(0.f,
		FWidgetActiveTimerDelegate::CreateLambda([this, WeakHierarchy = TWeakObjectPtr<URigHierarchy>(InHierarchy), InControls](double, float)
		{
			if(URigHierarchy* HierarchyToUpdate = WeakHierarchy.Get())
			{
				UpdateSelection(HierarchyToUpdate, InControls);
			}
			return EActiveTimerReturnType::Stop;
		})
	);
}

void SRigSpacePickerWidget::UnregisterPendingSelection()
{
	if (PendingSelectionHandle.IsValid())
	{
		if (TSharedPtr<FActiveTimerHandle> ActiveTimerHandle = PendingSelectionHandle.Pin())
		{
			UnRegisterActiveTimer(ActiveTimerHandle.ToSharedRef());
		}
		PendingSelectionHandle.Reset();
	}
}

void SRigSpacePickerWidget::UpdateSelection(URigHierarchy* InHierarchy, const TArray<FRigElementKey>& InControls)
{
	if (Hierarchy.IsValid())
	{
		URigHierarchy* StrongHierarchy = Hierarchy.Get();
		if (StrongHierarchy != InHierarchy)
		{
			if (HierarchyModifiedHandle.IsValid())
			{
				StrongHierarchy->OnModified().Remove(HierarchyModifiedHandle);
				HierarchyModifiedHandle.Reset();
			}
		}
	}
		
	Hierarchy = InHierarchy;
	ControlKeys.SetNum(0);
		
	for (const FRigElementKey& Key : InControls)
	{
		if (const FRigControlElement* ControlElement = Hierarchy->FindChecked<FRigControlElement>(Key))
		{
			//if it has no shape or not animatable then bail
			if (ControlElement->Settings.SupportsShape() == false || Hierarchy->IsAnimatable(ControlElement) == false)
			{
				continue;
			}
			if (ControlElement->Settings.ControlType == ERigControlType::Bool ||
				ControlElement->Settings.ControlType == ERigControlType::Float ||
				ControlElement->Settings.ControlType == ERigControlType::ScaleFloat ||
				ControlElement->Settings.ControlType == ERigControlType::Integer)
			{
				//if it has a channel and has a parent bail
				if (const FRigControlElement* ParentControlElement = Cast<FRigControlElement>(Hierarchy->GetFirstParent(ControlElement)))
				{
					continue;
				}
			}
		}
		ControlKeys.Add(Key);
	}
			
	if (Hierarchy.IsValid() && HierarchyModifiedHandle.IsValid() == false)
	{
		HierarchyModifiedHandle = InHierarchy->OnModified().AddSP(this, &SRigSpacePickerWidget::OnHierarchyModified);
	}
			
	UpdateActiveSpaces();
	RepopulateItemSpaces();
}

class SRigSpaceDialogWindow : public SWindow
{
}; 

FReply SRigSpacePickerWidget::OpenDialog(bool bModal)
{
	check(!DialogWindow.IsValid());
		
	const FVector2D CursorPos = FSlateApplication::Get().GetCursorPos();

	TSharedRef<SRigSpaceDialogWindow> Window = SNew(SRigSpaceDialogWindow)
	.Title( LOCTEXT("SRigSpacePickerWidgetPickSpace", "Pick a new space") )
	.CreateTitleBar(false)
	.Type(EWindowType::Menu)
	.IsPopupWindow(true) // the window automatically closes when user clicks outside of it
	.SizingRule( ESizingRule::Autosized )
	.ScreenPosition(CursorPos)
	.FocusWhenFirstShown(true)
	.ActivationPolicy(EWindowActivationPolicy::FirstShown)
	[
		AsShared()
	];
	
	Window->SetWidgetToFocusOnActivate(AsShared());
	if (!Window->GetOnWindowDeactivatedEvent().IsBoundToObject(this))
	{
		Window->GetOnWindowDeactivatedEvent().AddLambda([this]()
		{
			// Do not reset if we lost focus because of opening the context menu
			if (!ContextMenu.IsValid())
			{
				SetControls(nullptr, {});
			}
		});
	}
	
	DialogWindow = Window;

	Window->MoveWindowTo(CursorPos);

	if(bModal)
	{
		GEditor->EditorAddModalWindow(Window);
	}
	else
	{
		FSlateApplication::Get().AddWindow( Window );
	}

	return FReply::Handled();
}

void SRigSpacePickerWidget::CloseDialog()
{
	if(bLaunchingContextMenu)
	{
		return;
	}
	
	if(ContextMenu.IsValid())
	{
		return;
	}
	
	if ( DialogWindow.IsValid() )
	{
		DialogWindow.Pin()->GetOnWindowDeactivatedEvent().RemoveAll(this);
		DialogWindow.Pin()->RequestDestroyWindow();
		DialogWindow.Reset();
	}
}

FReply SRigSpacePickerWidget::OnKeyDown( const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent )
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		if(DialogWindow.IsValid())
		{
			CloseDialog();
		}
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

bool SRigSpacePickerWidget::SupportsKeyboardFocus() const
{
	return true;
}

void SRigSpacePickerWidget::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	if(bRepopulateRequired)
	{
		UpdateActiveSpaces();
		RepopulateItemSpaces();
		bRepopulateRequired = false;
	}
	else if(GetAdditionalSpacesDelegate.IsBound())
	{
		if (Hierarchy.IsValid())
		{
			URigHierarchy* StrongHierarchy = Hierarchy.Get();
			TArray<FRigElementKeyWithLabel> CurrentAdditionalSpaces;
			for(const FRigElementKey& ControlKey: ControlKeys)
			{
				CurrentAdditionalSpaces.Append(GetAdditionalSpacesDelegate.Execute(StrongHierarchy, ControlKey));
			}
		
			if(CurrentAdditionalSpaces != AdditionalSpaces)
			{
				RepopulateItemSpaces();
			}
		}
	}
}

const TArray<FRigElementKey>& SRigSpacePickerWidget::GetActiveSpaces() const
{
	return ActiveSpaceKeys;
}

const TArray<FRigElementKeyWithLabel>& SRigSpacePickerWidget::GetDefaultSpaces() const
{
	static const TArray<FRigElementKeyWithLabel> DefaultSpaces = {
		FRigElementKeyWithLabel(URigHierarchy::GetDefaultParentKey(), URigHierarchy::DefaultParentKeyLabel),
		FRigElementKeyWithLabel(URigHierarchy::GetWorldSpaceReferenceKey(), URigHierarchy::WorldSpaceKeyLabel)
	};
	return DefaultSpaces;
}

TArray<FRigElementKeyWithLabel> SRigSpacePickerWidget::GetSpaceList(bool bIncludeDefaultSpaces) const
{
	if(bIncludeDefaultSpaces && bShowDefaultSpaces)
	{
		TArray<FRigElementKeyWithLabel> Spaces;
		Spaces.Append(GetDefaultSpaces());
		Spaces.Append(CurrentSpaceKeys);
		return Spaces;
	}
	return CurrentSpaceKeys;
}

void SRigSpacePickerWidget::RefreshContents()
{
	UpdateActiveSpaces();
	RepopulateItemSpaces();
}

void SRigSpacePickerWidget::AddSpacePickerRow(
	TSharedPtr<SVerticalBox> InListBox,
	ESpacePickerType InType,
	const FRigElementKey& InKey,
	const FSlateBrush* InBush,
	const FSlateColor& InColor,
	const FText& InTitle,
    FOnClicked OnClickedDelegate)
{
	static const FSlateBrush* RoundedBoxBrush = FControlRigEditorStyle::Get().GetBrush(TEXT("ControlRig.SpacePicker.RoundedRect"));

	TSharedPtr<SHorizontalBox> RowBox, ButtonBox;
	InListBox->AddSlot()
	.AutoHeight()
	.VAlign(VAlign_Top)
	.HAlign(HAlign_Fill)
	.Padding(4.0, 0.0, 4.0, 0.0)
	[
		SNew( SButton )
		.ButtonStyle(FAppStyle::Get(), "SimpleButton")
		.ContentPadding(FMargin(0.0))
		.OnClicked(OnClickedDelegate)
		[
			SAssignNew(RowBox, SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Fill)
			.Padding(0)
			[
				SNew(SBorder)
				.Padding(FMargin(5.0, 2.0, 5.0, 2.0))
				.BorderImage(RoundedBoxBrush)
				.BorderBackgroundColor(this, &SRigSpacePickerWidget::GetButtonColor, InType, InKey)
				.Content()
				[
					SAssignNew(ButtonBox, SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.HAlign(HAlign_Left)
					.Padding(FMargin(0.f, 0.f, 3.f, 0.f))
					[
						SNew(SImage)
						.Image(InBush)
						.ColorAndOpacity(InColor)
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.HAlign(HAlign_Left)
					.Padding(0)
					[
						SNew( STextBlock )
						.Text( InTitle )
						.Font( IDetailLayoutBuilder::GetDetailFont() )
						.ToolTipText(FText::FromName(InKey.Name))
					]

					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					[
						SNew(SSpacer)
					]
				]
			]
		]
	];

	if(!IsDefaultSpace(InKey))
	{
		const TAttribute<EVisibility> RestrictedVisibility = TAttribute<EVisibility>::CreateLambda([this]
		{
			return IsRestricted() ? EVisibility::Collapsed : EVisibility::Visible;
		});
		
		if(bAllowDelete || bAllowReorder)
		{
			RowBox->AddSlot()
			.FillWidth(1.f)
			[
				SNew(SSpacer)
				.Visibility(RestrictedVisibility)
			];
		}
		
		if(bAllowReorder)
		{
			RowBox->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Left)
			.Padding(0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton"))
				.ContentPadding(0)
				.OnClicked(this, &SRigSpacePickerWidget::HandleSpaceMoveUp, InKey)
				.IsEnabled(this, &SRigSpacePickerWidget::IsSpaceMoveUpEnabled, InKey)
				.ToolTipText(LOCTEXT("MoveSpaceDown", "Move this space down in the list."))
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("Icons.ChevronUp"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
				.Visibility(RestrictedVisibility)
			];

			RowBox->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Left)
			.Padding(0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton"))
				.ContentPadding(0)
				.OnClicked(this, &SRigSpacePickerWidget::HandleSpaceMoveDown, InKey)
				.IsEnabled(this, &SRigSpacePickerWidget::IsSpaceMoveDownEnabled, InKey)
				.ToolTipText(LOCTEXT("MoveSpaceUp", "Move this space up in the list."))
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("Icons.ChevronDown"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
				.Visibility(RestrictedVisibility)
			];
		}

		if(bAllowDelete)
		{
			const TSharedRef<SWidget> ClearButton = PropertyCustomizationHelpers::MakeClearButton(FSimpleDelegate::CreateSP(this, &SRigSpacePickerWidget::HandleSpaceDelete, InKey), LOCTEXT("DeleteSpace", "Remove this space."), true);
			ClearButton->SetVisibility(RestrictedVisibility);

			RowBox->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Left)
			.Padding(0)
			[
				ClearButton
			];
		}
	}
}

FReply SRigSpacePickerWidget::HandleParentSpaceClicked()
{
	return HandleElementSpaceClicked(URigHierarchy::GetDefaultParentKey());
}

FReply SRigSpacePickerWidget::HandleWorldSpaceClicked()
{
	return HandleElementSpaceClicked(URigHierarchy::GetWorldSpaceReferenceKey());
}

FReply SRigSpacePickerWidget::HandleElementSpaceClicked(FRigElementKey InKey)
{
	if (Hierarchy.IsValid())
	{
		URigHierarchy* StrongHierarchy = Hierarchy.Get();
		
		//need to make copy since array may get shrunk during the event broadcast
		TArray<FRigElementKey> ControlKeysCopy = ControlKeys;
		for (const FRigElementKey& ControlKey : ControlKeysCopy)
		{
			ActiveSpaceChangedEvent.Broadcast(StrongHierarchy, ControlKey, InKey);
		}
	}

	if(DialogWindow.IsValid())
	{
		CloseDialog();
	}
	
	return FReply::Handled();
}

FReply SRigSpacePickerWidget::HandleSpaceMoveUp(FRigElementKey InKey)
{
	if(CurrentSpaceKeys.Num() > 1)
	{
		const int32 Index = CurrentSpaceKeys.IndexOfByKey(InKey);
		if(CurrentSpaceKeys.IsValidIndex(Index))
		{
			if(Index > 0)
			{
				TArray<FRigElementKeyWithLabel> ChangedSpaceKeys = CurrentSpaceKeys;
				ChangedSpaceKeys.Swap(Index, Index - 1);

				if (Hierarchy.IsValid())
				{
					URigHierarchy* StrongHierarchy = Hierarchy.Get();
					for(const FRigElementKey& ControlKey : ControlKeys)
					{
						SpaceListChangedEvent.Broadcast(StrongHierarchy, ControlKey, ChangedSpaceKeys);
					}
				}

				return FReply::Handled();
			}
		}
	}
	return FReply::Unhandled();
}

FReply SRigSpacePickerWidget::HandleSpaceMoveDown(FRigElementKey InKey)
{
	if(CurrentSpaceKeys.Num() > 1)
	{
		const int32 Index = CurrentSpaceKeys.IndexOfByKey(InKey);
		if(CurrentSpaceKeys.IsValidIndex(Index))
		{
			if(Index < CurrentSpaceKeys.Num() - 1)
			{
				TArray<FRigElementKeyWithLabel> ChangedSpaceKeys = CurrentSpaceKeys;
				ChangedSpaceKeys.Swap(Index, Index + 1);

				if (Hierarchy.IsValid())
				{
					URigHierarchy* StrongHierarchy = Hierarchy.Get();
					for(const FRigElementKey& ControlKey : ControlKeys)
					{
						SpaceListChangedEvent.Broadcast(StrongHierarchy, ControlKey, ChangedSpaceKeys);
					}
				}
				
				return FReply::Handled();
			}
		}
	}
	return FReply::Unhandled();
}

void SRigSpacePickerWidget::HandleSpaceDelete(FRigElementKey InKey)
{
	TArray<FRigElementKeyWithLabel> ChangedSpaceKeys = CurrentSpaceKeys;
	const int32 ExistingSpaceIndex = ChangedSpaceKeys.IndexOfByKey(InKey);
	if(ExistingSpaceIndex != INDEX_NONE)
	{
		ChangedSpaceKeys.RemoveAt(ExistingSpaceIndex);
		if (Hierarchy.IsValid())
		{
			URigHierarchy* StrongHierarchy = Hierarchy.Get();
			for(const FRigElementKey& ControlKey : ControlKeys)
			{
				SpaceListChangedEvent.Broadcast(StrongHierarchy, ControlKey, ChangedSpaceKeys);
			}
		}
	}
}

FReply SRigSpacePickerWidget::HandleAddElementClicked()
{
	HierarchyDisplaySettings.bShowConnectors = false;
	HierarchyDisplaySettings.bShowSockets = false;
	HierarchyDisplaySettings.bShowComponents = false;
	
	FRigTreeDelegates TreeDelegates;
	TreeDelegates.OnGetDisplaySettings = FOnGetRigTreeDisplaySettings::CreateSP(this, &SRigSpacePickerWidget::GetHierarchyDisplaySettings); 
	TreeDelegates.OnGetHierarchy = FOnGetRigTreeHierarchy::CreateSP(this, &SRigSpacePickerWidget::GetHierarchyConst);
	TreeDelegates.OnMouseButtonClick = FOnRigTreeMouseButtonClick::CreateLambda([this](TSharedPtr<FRigTreeElement> InItem)
	{
		if(InItem.IsValid())
		{
			const FRigElementKey Key = InItem->Key.GetElement();
			if(!IsDefaultSpace(Key) && IsValidKey(Key))
			{
				if (Hierarchy.IsValid())
				{
					URigHierarchy* StrongHierarchy = Hierarchy.Get();
					for(const FRigElementKey& ControlKey : ControlKeys)
					{
						FRigVMDependenciesProvider DependencyProvider;
						FString FailureReason;

						if(UControlRig* ControlRig = StrongHierarchy->GetTypedOuter<UControlRig>())
						{
							DependencyProvider = FRigVMDependenciesProvider(StrongHierarchy, ControlRig->GetVM()); 
						}
						else if(UControlRigBlueprint* RigBlueprint = StrongHierarchy->GetTypedOuter<UControlRigBlueprint>())
						{
							if(UControlRig* CDO = Cast<UControlRig>(RigBlueprint->GetRigVMBlueprintGeneratedClass()->GetDefaultObject()))
							{
								DependencyProvider = FRigVMDependenciesProvider(StrongHierarchy, CDO->GetVM()); 
							}
						}
						
						if(!StrongHierarchy->CanSwitchToParent(ControlKey, Key, DependencyProvider, &FailureReason))
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
					}
					
					TArray<FRigElementKeyWithLabel> ChangedSpaceKeys = CurrentSpaceKeys;
					if(ChangedSpaceKeys.FindByKey(Key) == nullptr)
					{
						if(ControlKeys.IsEmpty())
						{
							ChangedSpaceKeys.Emplace(Key);
						}
						else
						{
							ChangedSpaceKeys.Emplace(Key, StrongHierarchy->GetDisplayLabelForParent(ControlKeys[0], Key));
						}
					}

					for(const FRigElementKey& ControlKey : ControlKeys)
					{
						SpaceListChangedEvent.Broadcast(StrongHierarchy, ControlKey, ChangedSpaceKeys);
					}
				}
			}
		}

		if(ContextMenu.IsValid())
		{
			ContextMenu.Pin()->Dismiss();
			ContextMenu.Reset();
		}
	});

	TreeDelegates.OnCompareKeys = FOnRigTreeCompareKeys::CreateLambda([](const FRigHierarchyKey& A, const FRigHierarchyKey& B) -> bool
	{
		if(A.IsElement() && B.IsComponent())
		{
			return true;
		}
		if(B.IsElement() && A.IsComponent())
		{
			return false;
		}

		// controls should always show up first - so we'll sort them to the start of the list
		if(A.IsElement() && B.IsElement())
		{
			if(A.GetElement().Type == ERigElementType::Control && B.GetElement().Type != ERigElementType::Control)
			{
				return true;
			}
			if(B.GetElement().Type == ERigElementType::Control && A.GetElement().Type != ERigElementType::Control)
			{
				return false;
			}
		}
		return A < B;
	});

	TSharedPtr<SSearchableRigHierarchyTreeView> SearchableTreeView = SNew(SSearchableRigHierarchyTreeView)
	.RigTreeDelegates(TreeDelegates);
	SearchableTreeView->GetTreeView()->RefreshTreeView(true);

	const bool bFocusImmediately = false;
	// Create as context menu
	TGuardValue<bool> AboutToShowMenu(bLaunchingContextMenu, true);
	ContextMenu = FSlateApplication::Get().PushMenu(
		AsShared(),
		FWidgetPath(),
		SearchableTreeView.ToSharedRef(),
		FSlateApplication::Get().GetCursorPos(),
		FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu),
		bFocusImmediately
	);

	if(!ContextMenu.IsValid())
	{
		return FReply::Unhandled();
	}
	
	ContextMenu.Pin()->GetOnMenuDismissed().AddLambda([this](TSharedRef<IMenu> InMenu)
	{
		ContextMenu.Reset();

		if(DialogWindow.IsValid())
		{
			DialogWindow.Pin()->BringToFront(true);

			TSharedRef<SWidget> ThisRef = AsShared();
			FSlateApplication::Get().ForEachUser([&ThisRef](FSlateUser& User) {
				User.SetFocus(ThisRef, EFocusCause::SetDirectly);
			});
		}
	});

	return FReply::Handled().SetUserFocus(SearchableTreeView->GetSearchBox(), EFocusCause::SetDirectly);
}

bool SRigSpacePickerWidget::IsRestricted() const
{
	if(URigHierarchy* CurrentHierarchy = GetHierarchy())
	{
		for(const FRigElementKey& Control : GetControls())
		{
			if(const FRigControlElement* ControlElement = CurrentHierarchy->Find<FRigControlElement>(Control))
			{
				if(ControlElement->Settings.bRestrictSpaceSwitching)
				{
					return true;
				}
			}
		}
	}
	return false;
}

bool SRigSpacePickerWidget::IsSpaceMoveUpEnabled(FRigElementKey InKey) const
{
	if(CurrentSpaceKeys.IsEmpty())
	{
		return false;
	}
	return CurrentSpaceKeys[0].Key != InKey;
}

bool SRigSpacePickerWidget::IsSpaceMoveDownEnabled(FRigElementKey InKey) const
{
	if(CurrentSpaceKeys.IsEmpty())
	{
		return false;
	}
	return CurrentSpaceKeys.Last().Key != InKey;
}

void SRigSpacePickerWidget::OnHierarchyModified(ERigHierarchyNotification InNotif, URigHierarchy* InHierarchy,
                                                const FRigNotificationSubject& InSubject)
{
	const FRigBaseElement* InElement = InSubject.Element;
	const FRigBaseComponent* InComponent = InSubject.Component;

	if(InElement == nullptr)
	{
		return;
	}

	if(!ControlKeys.Contains(InElement->GetKey()))
	{
		return;
	}
	
	switch(InNotif)
	{
		case ERigHierarchyNotification::ParentChanged:
		case ERigHierarchyNotification::ParentWeightsChanged:
		case ERigHierarchyNotification::ControlSettingChanged:
		{
			bRepopulateRequired = true;
			break;
		}
		default:
		{
			break;
		}
	}
}

FSlateColor SRigSpacePickerWidget::GetButtonColor(ESpacePickerType InType, FRigElementKey InKey) const
{
	static const FSlateColor ActiveColor = FControlRigEditorStyle::Get().SpacePickerSelectColor;

	switch(InType)
	{
		case ESpacePickerType_Parent:
		{
			// this is also true if the object has no parent
			if(ActiveSpaceKeys.Contains(URigHierarchy::GetDefaultParentKey()))
			{
				return ActiveColor;
			}
			break;
		}
		case ESpacePickerType_World:
		{
			if(ActiveSpaceKeys.Contains(URigHierarchy::GetWorldSpaceReferenceKey()))
			{
				return ActiveColor;
			}
			break;
		}
		case ESpacePickerType_Item:
		default:
		{
			if(ActiveSpaceKeys.Contains(InKey) && InKey.IsValid())
			{
				return ActiveColor;
			}
			break;
		}
	}
	return FStyleColors::Transparent;
}

FRigElementKey SRigSpacePickerWidget::GetActiveSpace_Private(URigHierarchy* InHierarchy,
	const FRigElementKey& InControlKey) const
{
	if(InHierarchy)
	{
		return InHierarchy->GetActiveParent(InControlKey);
	}
	return URigHierarchy::GetDefaultParentKey();
}

TArray<FRigElementKeyWithLabel> SRigSpacePickerWidget::GetCurrentParents_Private(URigHierarchy* InHierarchy,
                                                                const FRigElementKey& InControlKey) const
{
	if(!InControlKey.IsValid() || InHierarchy == nullptr)
	{
		return TArray<FRigElementKeyWithLabel>();
	}

	check(ControlKeys.Contains(InControlKey));
	TArray<FRigElementKey> Parents = InHierarchy->GetParents(InControlKey);
	TArray<FRigElementKeyWithLabel> ParentSpaces;
	if(Parents.Num() > 0)
	{
		if(!IsDefaultSpace(Parents[0]))
		{
			Parents[0] = URigHierarchy::GetDefaultParentKey();
		}
	}
	ParentSpaces.Reserve(Parents.Num());
	for(const FRigElementKey& ParentKey : Parents)
	{
		ParentSpaces.Emplace(ParentKey, InHierarchy->GetDisplayLabelForParent(InControlKey, ParentKey));
	}
	return ParentSpaces;
}

void SRigSpacePickerWidget::RepopulateItemSpaces()
{
	if(!ItemSpacesListBox.IsValid())
	{
		return;
	}
	if(!Hierarchy.IsValid())
	{
		return;
	}

	URigHierarchy* StrongHierarchy = Hierarchy.Get();
	
	TArray<FRigElementKeyWithLabel> FavoriteKeys, SpacesFromDelegate;

	if(bShowFavoriteSpaces)
	{
		for(const FRigElementKey& ControlKey : ControlKeys)
		{
			const FRigControlElementCustomization* Customization = nullptr;
			if(GetControlCustomizationDelegate.IsBound())
			{
				Customization = GetControlCustomizationDelegate.Execute(StrongHierarchy, ControlKey);
			}

			if(Customization)
			{
				for(const FRigElementKeyWithLabel& AvailableSpace : Customization->AvailableSpaces)
				{
					if(IsDefaultSpace(AvailableSpace.Key) || !IsValidKey(AvailableSpace.Key))
					{
						continue;
					}
					FavoriteKeys.AddUnique(AvailableSpace);
				}
			}
			
			// check if the customization is different from the base one in the asset
			if(const FRigControlElement* ControlElement = StrongHierarchy->Find<FRigControlElement>(ControlKey))
			{
				if(Customization != &ControlElement->Settings.Customization)
				{
					for(const FRigElementKeyWithLabel& AvailableSpace : ControlElement->Settings.Customization.AvailableSpaces)
					{
						if(IsDefaultSpace(AvailableSpace.Key) || !IsValidKey(AvailableSpace.Key))
						{
							continue;
						}

						if(Customization)
						{
							if(Customization->AvailableSpaces.FindByKey(AvailableSpace.Key) != nullptr)
							{
								continue;
							}
							if(Customization->RemovedSpaces.Contains(AvailableSpace.Key))
							{
								continue;
							}
						}
						FavoriteKeys.AddUnique(AvailableSpace);
					}
				}
			}
		}
	}
	
	// now gather all of the spaces using the get additional spaces delegate
	if(GetAdditionalSpacesDelegate.IsBound() && bShowAdditionalSpaces)
	{
		AdditionalSpaces.Reset();
		for(const FRigElementKey& ControlKey: ControlKeys)
		{
			AdditionalSpaces.Append(GetAdditionalSpacesDelegate.Execute(StrongHierarchy, ControlKey));
		}
		
		for(const FRigElementKeyWithLabel& AdditionalSpace : AdditionalSpaces)
		{
			if(IsDefaultSpace(AdditionalSpace.Key)  || !IsValidKey(AdditionalSpace.Key))
			{
				continue;
			}
			SpacesFromDelegate.AddUnique(AdditionalSpace);
		}
	}

	/*
	struct FKeySortPredicate
	{
		bool operator()(const FRigElementKey& A, const FRigElementKey& B) const
		{
			static TMap<ERigElementType, int32> TypeOrder;
			if(TypeOrder.IsEmpty())
			{
				TypeOrder.Add(ERigElementType::Control, 0);
				TypeOrder.Add(ERigElementType::Reference, 1);
				TypeOrder.Add(ERigElementType::Null, 2);
				TypeOrder.Add(ERigElementType::Bone, 3);
				TypeOrder.Add(ERigElementType::RigidBody, 4);
			}

			const int32 TypeIndexA = TypeOrder.FindChecked(A.Type);
			const int32 TypeIndexB = TypeOrder.FindChecked(B.Type);
			if(TypeIndexA != TypeIndexB)
			{
				return TypeIndexA < TypeIndexB;
			}

			return A.Name.Compare(B.Name) < 0; 
		}
	};
	SpacesFromDelegate.Sort(FKeySortPredicate());
	*/

	TArray<FRigElementKeyWithLabel> CombinedSpaces = FavoriteKeys;
	for(const FRigElementKeyWithLabel& Space : SpacesFromDelegate)
	{
		if(CombinedSpaces.FindByKey(Space.Key) == nullptr)
		{
			CombinedSpaces.AddUnique(Space);
		}
	}

	if(CombinedSpaces == CurrentSpaceKeys)
	{
		return;
	}

	ClearListBox(ItemSpacesListBox);

	for(const FRigElementKeyWithLabel& Space : CombinedSpaces)
	{
		TPair<const FSlateBrush*, FSlateColor> IconAndColor = SRigHierarchyItem::GetBrushForElementType(StrongHierarchy, Space.Key);
		
		AddSpacePickerRow(
			ItemSpacesListBox,
			ESpacePickerType_Item,
			Space.Key,
			IconAndColor.Key,
			IconAndColor.Value,
			FText::FromName(Space.GetLabel()),
			FOnClicked::CreateSP(this, &SRigSpacePickerWidget::HandleElementSpaceClicked, Space.Key)
		);
	}

	CurrentSpaceKeys = CombinedSpaces;
}

void SRigSpacePickerWidget::ClearListBox(TSharedPtr<SVerticalBox> InListBox)
{
	InListBox->ClearChildren();
}

void SRigSpacePickerWidget::UpdateActiveSpaces()
{
	ActiveSpaceKeys.Reset();

	if(!Hierarchy.IsValid())
	{
		return;
	}

	URigHierarchy* StrongHierarchy = Hierarchy.Get();	
	for(int32 ControlIndex=0;ControlIndex<ControlKeys.Num();ControlIndex++)
	{
		ActiveSpaceKeys.Add(URigHierarchy::GetDefaultParentKey());

		if(GetActiveSpaceDelegate.IsBound())
		{
			ActiveSpaceKeys[ControlIndex] = GetActiveSpaceDelegate.Execute(StrongHierarchy, ControlKeys[ControlIndex]);
		}
	}
}

bool SRigSpacePickerWidget::IsValidKey(const FRigElementKey& InKey) const
{
	if(!InKey.IsValid())
	{
		return false;
	}
	if(Hierarchy == nullptr)
	{
		return false;
	}
	return Hierarchy->Contains(InKey);
}

bool SRigSpacePickerWidget::IsDefaultSpace(const FRigElementKey& InKey) const
{
	if(bShowDefaultSpaces)
	{
		return InKey == URigHierarchy::GetDefaultParentKey() || InKey == URigHierarchy::GetWorldSpaceReferenceKey();
	}
	return false;
}

void SRigSpacePickerWidget::PostUndo(bool bSuccess)
{
	RefreshContents();
}
void SRigSpacePickerWidget::PostRedo(bool bSuccess)
{
	RefreshContents();
}

//////////////////////////////////////////////////////////////
/// SRigSpacePickerBakeWidget
///////////////////////////////////////////////////////////

void SRigSpacePickerBakeWidget::Construct(const FArguments& InArgs)
{
	check(InArgs._Hierarchy);
	check(InArgs._Controls.Num() > 0);
	check(InArgs._Sequencer);
	check(InArgs._OnBake.IsBound());
	
	Settings = MakeShared<TStructOnScope<FRigSpacePickerBakeSettings>>();
	Settings->InitializeAs<FRigSpacePickerBakeSettings>();
	*Settings = InArgs._Settings;
	//always setting space to be parent as default, since stored space may not be available.
	Settings->Get()->TargetSpace = URigHierarchy::GetDefaultParentKey();
	Sequencer = InArgs._Sequencer;

	FStructureDetailsViewArgs StructureViewArgs;
	StructureViewArgs.bShowObjects = true;
	StructureViewArgs.bShowAssets = true;
	StructureViewArgs.bShowClasses = true;
	StructureViewArgs.bShowInterfaces = true;

	FDetailsViewArgs ViewArgs;
	ViewArgs.bAllowSearch = false;
	ViewArgs.bHideSelectionTip = false;
	ViewArgs.bShowObjectLabel = false;

	FPropertyEditorModule& PropertyEditor = FModuleManager::Get().LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));

	DetailsView = PropertyEditor.CreateStructureDetailView(ViewArgs, StructureViewArgs, TSharedPtr<FStructOnScope>());

	DetailsView->GetDetailsView()->RegisterInstancedCustomPropertyTypeLayout("FrameNumber",
		FOnGetPropertyTypeCustomizationInstance::CreateSP(Sequencer, &ISequencer::MakeFrameNumberDetailsCustomization));
	DetailsView->SetStructureData(Settings);

	ChildSlot
	[
		SNew(SBorder)
		.Visibility(EVisibility::Visible)
		[
			SNew(SVerticalBox)

			+SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(SpacePickerWidget, SRigSpacePickerWidget)
				.Hierarchy(InArgs._Hierarchy)
				.Controls(InArgs._Controls)
				.AllowDelete(false)
				.AllowReorder(false)
				.AllowAdd(true)
				.ShowBakeAndCompensateButton(false)
				.GetControlCustomization_Lambda([this] (URigHierarchy*, const FRigElementKey)
				{
					return &Customization;
				})
				.OnSpaceListChanged_Lambda([this](URigHierarchy*, const FRigElementKey&, const TArray<FRigElementKeyWithLabel>& InSpaceList)
				{
					if(Customization.AvailableSpaces != InSpaceList)
					{
						Customization.AvailableSpaces = InSpaceList;
						SpacePickerWidget->RefreshContents();
					}
				})
				.GetActiveSpace_Lambda([this](URigHierarchy*, const FRigElementKey&)
				{
					return Settings->Get()->TargetSpace;
				})
				.OnActiveSpaceChanged_Lambda([this] (URigHierarchy*, const FRigElementKey&, const FRigElementKey InSpaceKey)
				{
					if(Settings->Get()->TargetSpace != InSpaceKey)
					{
						Settings->Get()->TargetSpace = InSpaceKey;
						SpacePickerWidget->RefreshContents();
					}
				})
			]

			+SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.f, 0.f, 0.f, 0.f)
			[
				DetailsView->GetWidget().ToSharedRef()
			]

			+SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.f, 16.f, 0.f, 16.f)
			[
				SNew(SHorizontalBox)

				+SHorizontalBox::Slot()
				.FillWidth(1.f)
				[
					SNew(SSpacer)
				]

				+SHorizontalBox::Slot()
				.AutoWidth()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				.Padding(8.f, 0.f, 0.f, 0.f)
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.ContentPadding(FAppStyle::GetMargin("StandardDialog.ContentPadding"))
					.Text(LOCTEXT("OK", "OK"))
					.OnClicked_Lambda([this, InArgs]()
					{
						FReply Reply =  InArgs._OnBake.Execute(SpacePickerWidget->GetHierarchy(), SpacePickerWidget->GetControls(),*(Settings->Get()));
						CloseDialog();
						return Reply;

					})
					.IsEnabled_Lambda([this]()
					{
						return Settings->Get()->TargetSpace.IsValid();
					})
				]

				+SHorizontalBox::Slot()
				.AutoWidth()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				.Padding(8.f, 0.f, 16.f, 0.f)
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.ContentPadding(FAppStyle::GetMargin("StandardDialog.ContentPadding"))
					.Text(LOCTEXT("Cancel", "Cancel"))
					.OnClicked_Lambda([this]()
					{
						CloseDialog();
						return FReply::Handled();
					})
				]
			]
		]
	];
}

FReply SRigSpacePickerBakeWidget::OpenDialog(bool bModal)
{
	check(!DialogWindow.IsValid());
		
	const FVector2D CursorPos = FSlateApplication::Get().GetCursorPos();

	TSharedRef<SRigSpaceDialogWindow> Window = SNew(SRigSpaceDialogWindow)
	.Title( LOCTEXT("SRigSpacePickerBakeWidgetTitle", "Bake Controls To Specified Space") )
	.CreateTitleBar(true)
	.Type(EWindowType::Normal)
	.SizingRule( ESizingRule::Autosized )
	.ScreenPosition(CursorPos)
	.FocusWhenFirstShown(true)
	.ActivationPolicy(EWindowActivationPolicy::FirstShown)
	[
		AsShared()
	];
	
	Window->SetWidgetToFocusOnActivate(AsShared());
	
	DialogWindow = Window;

	Window->MoveWindowTo(CursorPos);

	if(bModal)
	{
		GEditor->EditorAddModalWindow(Window);
	}
	else
	{
		FSlateApplication::Get().AddWindow( Window );
	}

	return FReply::Handled();
}

void SRigSpacePickerBakeWidget::CloseDialog()
{
	if ( DialogWindow.IsValid() )
	{
		DialogWindow.Pin()->RequestDestroyWindow();
		DialogWindow.Reset();
	}
}

#undef LOCTEXT_NAMESPACE
