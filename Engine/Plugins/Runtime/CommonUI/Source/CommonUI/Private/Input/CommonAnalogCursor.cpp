// Copyright Epic Games, Inc. All Rights Reserved.

#include "Input/CommonAnalogCursor.h"
#include "CommonInputTypeEnum.h"
#include "CommonUIPrivate.h"
#include "Input/CommonUIActionRouterBase.h"
#include "CommonInputSubsystem.h"
#include "Framework/Application/SlateUser.h"
#include "Input/CommonUIInputSettings.h"
#include "Engine/Console.h"
#include "Widgets/SViewport.h"
#include "Engine/GameViewportClient.h"

#include "Components/ListView.h"
#include "Components/ScrollBar.h"
#include "Components/ScrollBox.h"
#include "Slate/SGameLayerManager.h"
#include "UnrealClient.h"

#define LOCTEXT_NAMESPACE "CommonAnalogCursor"

const float AnalogScrollUpdatePeriod = 0.1f;
const float ScrollDeadZone = 0.2f;

static const TAutoConsoleVariable<bool> CVarShouldVirtualAcceptSimulateMouseButton(
	TEXT("CommonUI.ShouldVirtualAcceptSimulateMouseButton"),
	true,
	TEXT("Controls if virtual_accept key events will be converted to left mouse button events."));

bool IsEligibleFakeKeyPointerEvent(const FPointerEvent& PointerEvent)
{
	FKey EffectingButton = PointerEvent.GetEffectingButton();
	return EffectingButton.IsMouseButton() 
		&& EffectingButton != EKeys::LeftMouseButton
		&& EffectingButton != EKeys::RightMouseButton
		&& EffectingButton != EKeys::MiddleMouseButton;
}

FCommonAnalogCursor::FCommonAnalogCursor(const UCommonUIActionRouterBase& InActionRouter)
	: ActionRouter(InActionRouter)
	, ActiveInputMethod(ECommonInputType::MouseAndKeyboard)
{}

void FCommonAnalogCursor::Initialize()
{
	RefreshCursorSettings();

	PointerButtonDownKeys = FSlateApplication::Get().GetPressedMouseButtons();
	PointerButtonDownKeys.Remove(EKeys::LeftMouseButton);
	PointerButtonDownKeys.Remove(EKeys::RightMouseButton);
	PointerButtonDownKeys.Remove(EKeys::MiddleMouseButton);

	UCommonInputSubsystem& InputSubsystem = ActionRouter.GetInputSubsystem();
	InputSubsystem.OnInputMethodChangedNative.AddSP(this, &FCommonAnalogCursor::HandleInputMethodChanged);
	HandleInputMethodChanged(InputSubsystem.GetCurrentInputType());
}

#if WITH_EDITOR
extern bool IsViewportWindowInFocusPath(const UCommonUIActionRouterBase& Router);
#endif

void FCommonAnalogCursor::Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor)
{
	// Refreshing visibility per tick to address multiplayer p2 cursor visibility getting stuck
	RefreshCursorVisibility();

	// Don't bother trying to do anything while the game viewport has capture
	if (IsUsingGamepad() && IsGameViewportInFocusPathWithoutCapture())
	{
		// The game viewport can't have been focused without a user, so we're quite safe to assume/enforce validity of the user here
		const TSharedRef<FSlateUser> SlateUser = SlateApp.GetUser(GetOwnerUserIndex()).ToSharedRef();

#if WITH_EDITOR
		// Instantly acknowledge any changes to our settings when we're in the editor
		RefreshCursorSettings();
		if (!IsViewportWindowInFocusPath(ActionRouter))
		{
			LastCursorTarget.Reset();
			return;
		}
#endif
		if (bIsAnalogMovementEnabled)
		{
			LastCursorTarget.Reset();
			const FVector2D NewPosition = CalculateTickedCursorPosition(DeltaTime, SlateApp, SlateUser);

			UCommonInputSubsystem& InputSubsystem = ActionRouter.GetInputSubsystem();
			InputSubsystem.SetCursorPosition(NewPosition, false);
		}
		else if (UCommonUIInputSettings::Get().ShouldLinkCursorToGamepadFocus())
		{
			TSharedPtr<SWidget> PinnedLastCursorTarget = LastCursorTarget.Pin();

			// By default the cursor target is the focused widget itself, unless we're working with a list view
			TSharedPtr<SWidget> CursorTarget = SlateUser->GetFocusedWidget();
			if (TSharedPtr<ITableViewMetadata> TableViewMetadata = CursorTarget ? CursorTarget->GetMetaData<ITableViewMetadata>() : nullptr)
			{
				// A list view is currently focused, so we actually want to make sure we are centering the cursor over the currently selected row instead
				TArray<TSharedPtr<ITableRow>> SelectedRows = TableViewMetadata->GatherSelectedRows();
				if (SelectedRows.Num() > 0 && ensure(SelectedRows[0].IsValid()))
				{
					// Just pick the first selected entry in the list - it's awfully rare to have anything other than single-selection when using gamepad
					CursorTarget = SelectedRows[0]->AsWidget();
				}
			}
			
			FGeometry TargetGeometry;
			if (CursorTarget)
			{
				if (CursorTarget == GetViewportClient()->GetGameViewportWidget())
				{
					// When the target is the game viewport as a whole, we don't want to center blindly - we want to center in the geometry of our owner's widget host layer
					TSharedPtr<IGameLayerManager> GameLayerManager = GetViewportClient()->GetGameLayerManager();
					if (ensure(GameLayerManager))
					{
						TargetGeometry = GameLayerManager->GetPlayerWidgetHostGeometry(ActionRouter.GetLocalPlayerChecked());
					}
				}
				else
				{
					TargetGeometry = CursorTarget->GetTickSpaceGeometry();
				}
			}

			// We want to try to update the cursor position when focus changes or the focused widget moves at all
			if (CursorTarget != PinnedLastCursorTarget || (CursorTarget && TargetGeometry.GetAccumulatedRenderTransform() != LastCursorTargetTransform))
			{
#if !UE_BUILD_SHIPPING
				if (CursorTarget != PinnedLastCursorTarget)
				{
					UE_LOG(LogCommonUI, Verbose, TEXT("User[%d] cursor target changed to [%s]"), GetOwnerUserIndex(), *FReflectionMetaData::GetWidgetDebugInfo(CursorTarget.Get()));
				}
#endif

				// Release capture unless the focused widget is the captor
				if (PinnedLastCursorTarget != CursorTarget && SlateUser->HasCursorCapture() && !SlateUser->DoesWidgetHaveAnyCapture(CursorTarget))
				{
					UE_LOG(LogCommonUI, Log, TEXT("User[%d] focus changed while the cursor is captured - releasing now before moving cursor to focused widget."), GetOwnerUserIndex());
					SlateUser->ReleaseCursorCapture();
				}

				LastCursorTarget = CursorTarget;

				bool bHasValidCursorTarget = false;
				if (CursorTarget)
				{
					if (TargetGeometry.GetLocalSize().X > UE_SMALL_NUMBER && TargetGeometry.GetLocalSize().Y > UE_SMALL_NUMBER)
					{
						LastCursorTargetTransform = TargetGeometry.GetAccumulatedRenderTransform();

						bHasValidCursorTarget = true;
						
						const FVector2D AbsoluteWidgetCenter = TargetGeometry.GetAbsolutePositionAtCoordinates(FVector2D(0.5f, 0.5f));
						SlateUser->SetCursorPosition(AbsoluteWidgetCenter);

						UE_LOG(LogCommonUI, Verbose, TEXT("User[%d] moving cursor to target [%s] @ (%d, %d)"), GetOwnerUserIndex(), *FReflectionMetaData::GetWidgetDebugInfo(CursorTarget.Get()), (int32)AbsoluteWidgetCenter.X, (int32)AbsoluteWidgetCenter.Y);
					}
				}

				if (!bHasValidCursorTarget)
				{
					SetNormalizedCursorPosition(FVector2D::ZeroVector);
				}
			}
		}

		if (bShouldHandleRightAnalog)
		{
			TimeUntilScrollUpdate -= DeltaTime;
			if (TimeUntilScrollUpdate <= 0.0f && GetAnalogValues(EAnalogStick::Right).SizeSquared() > FMath::Square(ScrollDeadZone))
			{
				// Generate mouse wheel events over all widgets currently registered as scroll recipients
				const TArray<const UWidget*>& AnalogScrollRecipients = ActionRouter.GatherActiveAnalogScrollRecipients();
				if (AnalogScrollRecipients.Num() > 0)
				{
					const FCommonAnalogCursorSettings& CursorSettings = UCommonUIInputSettings::Get().GetAnalogCursorSettings();
					const auto GetScrollAmountFunc = [&CursorSettings](float AnalogValue)
					{
						const float AmountBeyondDeadZone = FMath::Abs(AnalogValue) - CursorSettings.ScrollDeadZone;
						if (AmountBeyondDeadZone <= 0.f)
						{
							return 0.f;
						}
						return (AmountBeyondDeadZone / (1.f - CursorSettings.ScrollDeadZone)) * -FMath::Sign(AnalogValue) * CursorSettings.ScrollMultiplier;
					};

					const FVector2D& RightStickValues = GetAnalogValues(EAnalogStick::Right);
					const FVector2D ScrollAmounts(GetScrollAmountFunc(RightStickValues.X), GetScrollAmountFunc(RightStickValues.Y));

					for (const UWidget* ScrollRecipient : AnalogScrollRecipients)
					{
						check(ScrollRecipient);
						if (ScrollRecipient->GetCachedWidget())
						{
							const EOrientation Orientation = DetermineScrollOrientation(*ScrollRecipient);
							const float ScrollAmount = Orientation == Orient_Vertical ? ScrollAmounts.Y : ScrollAmounts.X;
							if (FMath::Abs(ScrollAmount) > SMALL_NUMBER)
							{
								const FVector2D WidgetCenter = ScrollRecipient->GetCachedGeometry().GetAbsolutePositionAtCoordinates(FVector2D(.5f, .5f));
								if (IsInViewport(WidgetCenter))
								{
									FPointerEvent MouseEvent(
										SlateUser->GetUserIndex(),
										FSlateApplication::CursorPointerIndex,
										WidgetCenter,
										WidgetCenter,
										TSet<FKey>(),
										EKeys::MouseWheelAxis,
										ScrollAmount,
										FModifierKeysState());

									UCommonInputSubsystem& InputSubsytem = ActionRouter.GetInputSubsystem();
									InputSubsytem.SetIsGamepadSimulatedClick(true);
									SlateApp.ProcessMouseWheelOrGestureEvent(MouseEvent, nullptr);
									InputSubsytem.SetIsGamepadSimulatedClick(false);
								}
							}
						}
					}
				}
			}
		}
	}
	else
	{
		// Since we're not processing cursor target this frame, the cursor position may change externally and therefore invalidate our cache
		LastCursorTarget.Reset();
	}
}

bool FCommonAnalogCursor::HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent)
{
	if (IsRelevantInput(InKeyEvent))
	{
		const ULocalPlayer& LocalPlayer = *ActionRouter.GetLocalPlayerChecked();
		if (LocalPlayer.ViewportClient && LocalPlayer.ViewportClient->ViewportConsole && LocalPlayer.ViewportClient->ViewportConsole->ConsoleActive())
		{
			// Let everything through when the console is open
			return false;
		}

#if !UE_BUILD_SHIPPING
		const FKey& PressedKey = InKeyEvent.GetKey();
		if (PressedKey == EKeys::Gamepad_LeftShoulder) { ShoulderButtonStatus |= EShoulderButtonFlags::LeftShoulder; }
		if (PressedKey == EKeys::Gamepad_RightShoulder) { ShoulderButtonStatus |= EShoulderButtonFlags::RightShoulder; }
		if (PressedKey == EKeys::Gamepad_LeftTrigger) { ShoulderButtonStatus |= EShoulderButtonFlags::LeftTrigger; }
		if (PressedKey == EKeys::Gamepad_RightTrigger) { ShoulderButtonStatus |= EShoulderButtonFlags::RightTrigger; }

		if (ShoulderButtonStatus == EShoulderButtonFlags::All)
		{
			ShoulderButtonStatus = EShoulderButtonFlags::None;
			bIsAnalogMovementEnabled = !bIsAnalogMovementEnabled;
			//RefreshCursorVisibility();
		}
#endif

		// We support binding actions to the virtual accept key, so it's a special flower that gets processed right now
		const bool bIsVirtualAccept = InKeyEvent.GetKey() == EKeys::Virtual_Accept;
		const EInputEvent InputEventType = InKeyEvent.IsRepeat() ? IE_Repeat : IE_Pressed;
		if (bIsVirtualAccept && ActionRouter.ProcessInput(InKeyEvent.GetKey(), InputEventType) == ERouteUIInputResult::Handled)
		{
			return true;
		}
		else if (!bIsVirtualAccept || ShouldVirtualAcceptSimulateMouseButton(InKeyEvent, IE_Pressed))
		{
			// There is no awareness on a mouse event of whether it's real or not, so mark that here.
			UCommonInputSubsystem& InputSubsytem = ActionRouter.GetInputSubsystem();
			InputSubsytem.SetIsGamepadSimulatedClick(bIsVirtualAccept);
			bool bReturnValue = FAnalogCursor::HandleKeyDownEvent(SlateApp, InKeyEvent);
			InputSubsytem.SetIsGamepadSimulatedClick(false);

			return bReturnValue;
		}
	}
	return false;
}

bool FCommonAnalogCursor::HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent)
{
	if (IsRelevantInput(InKeyEvent))
	{
#if !UE_BUILD_SHIPPING
		const FKey& PressedKey = InKeyEvent.GetKey();
		if (PressedKey == EKeys::Gamepad_LeftShoulder) { ShoulderButtonStatus ^= EShoulderButtonFlags::LeftShoulder; }
		if (PressedKey == EKeys::Gamepad_RightShoulder) { ShoulderButtonStatus ^= EShoulderButtonFlags::RightShoulder; }
		if (PressedKey == EKeys::Gamepad_LeftTrigger) { ShoulderButtonStatus ^= EShoulderButtonFlags::LeftTrigger; }
		if (PressedKey == EKeys::Gamepad_RightTrigger) { ShoulderButtonStatus ^= EShoulderButtonFlags::RightTrigger; }
#endif

		// We support binding actions to the virtual accept key, so it's a special flower that gets processed right now
		const bool bIsVirtualAccept = InKeyEvent.GetKey() == EKeys::Virtual_Accept;
		if (bIsVirtualAccept && ActionRouter.ProcessInput(InKeyEvent.GetKey(), IE_Released) == ERouteUIInputResult::Handled)
		{
			return true;
		}
		else if (!bIsVirtualAccept || ShouldVirtualAcceptSimulateMouseButton(InKeyEvent, IE_Released))
		{
			return FAnalogCursor::HandleKeyUpEvent(SlateApp, InKeyEvent);
		}
	}
	return false;
}

bool FCommonAnalogCursor::CanReleaseMouseCapture() const
{
	EMouseCaptureMode MouseCapture = ActionRouter.GetActiveMouseCaptureMode();
	return MouseCapture == EMouseCaptureMode::CaptureDuringMouseDown || MouseCapture == EMouseCaptureMode::CapturePermanently_IncludingInitialMouseDown;
}

bool FCommonAnalogCursor::HandleAnalogInputEvent(FSlateApplication& SlateApp, const FAnalogInputEvent& InAnalogInputEvent)
{
	if (IsRelevantInput(InAnalogInputEvent))
	{
		bool bParentHandled = FAnalogCursor::HandleAnalogInputEvent(SlateApp, InAnalogInputEvent);
		if (bIsAnalogMovementEnabled)
		{
			return bParentHandled;
		}
	}
	
	return false;
}

bool FCommonAnalogCursor::HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
#if WITH_EDITOR
	// We can leave editor cursor visibility in a bad state if the engine stops ticking to debug
	if (GIntraFrameDebuggingGameThread)
	{
		SlateApp.SetPlatformCursorVisibility(true);
		TSharedPtr<FSlateUser> SlateUser = FSlateApplication::Get().GetUser(GetOwnerUserIndex());
		if (SlateUser)
		{
			SlateUser->SetCursorVisibility(true);
		}
	}
#endif // WITH_EDITOR

	return FAnalogCursor::HandleMouseMoveEvent(SlateApp, MouseEvent);
}

bool FCommonAnalogCursor::HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& PointerEvent)
{
	if (FAnalogCursor::IsRelevantInput(PointerEvent))
	{
#if UE_COMMONUI_PLATFORM_REQUIRES_CURSOR_HIDDEN_FOR_TOUCH	
		// Some platforms don't register as switching its input type, so detect touch input here to hide the cursor.
		if (PointerEvent.IsTouchEvent() && ShouldHideCursor())
		{
			//ClearCenterWidget();
			HideCursor();
		}
#endif 

		// Mouse buttons other than the two primaries are fair game for binding as if they were normal keys
		const FKey EffectingButton = PointerEvent.GetEffectingButton();
		if (EffectingButton.IsMouseButton()
			&& EffectingButton != EKeys::LeftMouseButton
			&& EffectingButton != EKeys::RightMouseButton
			&& EffectingButton != EKeys::MiddleMouseButton)
		{
			UGameViewportClient* ViewportClient = GetViewportClient();
			if (TSharedPtr<SWidget> ViewportWidget = ViewportClient ? ViewportClient->GetGameViewportWidget() : nullptr)
			{
				const FWidgetPath WidgetsUnderCursor = SlateApp.LocateWindowUnderMouse(PointerEvent.GetScreenSpacePosition(), SlateApp.GetInteractiveTopLevelWindows());
				if (WidgetsUnderCursor.ContainsWidget(ViewportWidget.Get()))
				{
					FKeyEvent MouseKeyEvent(EffectingButton, PointerEvent.GetModifierKeys(), PointerEvent.GetUserIndex(), false, 0, 0);
					if (SlateApp.ProcessKeyDownEvent(MouseKeyEvent))
					{
						PointerButtonDownKeys.Add(EffectingButton);
						return true;
					}
				}
			}
		}
	}
	return false;
}

bool FCommonAnalogCursor::HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& PointerEvent)
{
	if (FAnalogCursor::IsRelevantInput(PointerEvent))
	{
		const bool bHadKeyDown = PointerButtonDownKeys.Remove(PointerEvent.GetEffectingButton()) > 0;
		if (bHadKeyDown
			|| (IsEligibleFakeKeyPointerEvent(PointerEvent) && !SlateApp.HasUserMouseCapture(PointerEvent.GetUserIndex())))
		{
			// Reprocess as a key if there was no mouse capture or it was previously pressed
			FKeyEvent MouseKeyEvent(PointerEvent.GetEffectingButton(), PointerEvent.GetModifierKeys(), PointerEvent.GetUserIndex(), false, 0, 0);
			bool bHandled = SlateApp.ProcessKeyUpEvent(MouseKeyEvent);
			if (bHadKeyDown)
			{
				// Only block the mouse up if the mouse down was also blocked
				return bHandled;
			}
		}
	}

	return false;
}

int32 FCommonAnalogCursor::GetOwnerUserIndex() const
{
	return ActionRouter.GetLocalPlayerIndex();
}

void FCommonAnalogCursor::ShouldHandleRightAnalog(bool bInShouldHandleRightAnalog)
{
	bShouldHandleRightAnalog = bInShouldHandleRightAnalog;
}

bool FCommonAnalogCursor::ShouldVirtualAcceptSimulateMouseButton(const FKeyEvent& InKeyEvent, EInputEvent InputEvent) const
{
	return CVarShouldVirtualAcceptSimulateMouseButton.GetValueOnGameThread();
}

//void FCommonAnalogCursor::SetCursorMovementStick(EAnalogStick InCursorMovementStick)
//{
//	const EAnalogStick NewStick = InCursorMovementStick == EAnalogStick::Max ? EAnalogStick::Left : InCursorMovementStick;
//	if (NewStick != CursorMovementStick)
//	{
//		ClearAnalogValues();
//		CursorMovementStick = NewStick;
//	}
//}

EOrientation FCommonAnalogCursor::DetermineScrollOrientation(const UWidget& Widget) const
{
	if (const UListView* AsListView = Cast<const UListView>(&Widget))
	{
		return AsListView->GetOrientation();
	}
	else if (const UScrollBar* AsScrollBar = Cast<const UScrollBar>(&Widget))
	{
		return AsScrollBar->GetOrientation();
	}
	else if (const UScrollBox* AsScrollBox = Cast<const UScrollBox>(&Widget))
	{
		return AsScrollBox->GetOrientation();
	}
	return EOrientation::Orient_Vertical;
}

bool FCommonAnalogCursor::IsRelevantInput(const FKeyEvent& KeyEvent) const
{
	return IsUsingGamepad() && FAnalogCursor::IsRelevantInput(KeyEvent) && (IsGameViewportInFocusPathWithoutCapture() || (KeyEvent.GetKey() == EKeys::Virtual_Accept && CanReleaseMouseCapture()));
}

bool FCommonAnalogCursor::IsRelevantInput(const FAnalogInputEvent& AnalogInputEvent) const
{
	return IsUsingGamepad() && FAnalogCursor::IsRelevantInput(AnalogInputEvent) && IsGameViewportInFocusPathWithoutCapture();
}

//EAnalogStick FCommonAnalogCursor::GetScrollStick() const
//{
//	// Scroll is on the right stick unless it conflicts with the cursor movement stick
//	return CursorMovementStick == EAnalogStick::Right ? EAnalogStick::Left : EAnalogStick::Right;
//}

UGameViewportClient* FCommonAnalogCursor::GetViewportClient() const
{
	return ActionRouter.GetLocalPlayerChecked()->ViewportClient;
}

bool FCommonAnalogCursor::IsGameViewportInFocusPathWithoutCapture() const
{
	if (const UGameViewportClient* ViewportClient = GetViewportClient())
	{
		if (TSharedPtr<SViewport> GameViewportWidget = ViewportClient->GetGameViewportWidget())
		{
			TSharedPtr<FSlateUser> SlateUser = FSlateApplication::Get().GetUser(GetOwnerUserIndex());
			if (SlateUser && !SlateUser->DoesWidgetHaveCursorCapture(GameViewportWidget))
			{
#if PLATFORM_DESKTOP
				// Not captured - is it in the focus path?
				return SlateUser->IsWidgetInFocusPath(GameViewportWidget);
#else
				// If we're not on desktop, focus on the viewport is irrelevant, as there aren't other windows around to care about
				return true;
#endif
			}
		}
	}
	return false;
}

void FCommonAnalogCursor::HandleInputMethodChanged(ECommonInputType NewInputMethod)
{
	ActiveInputMethod = NewInputMethod;
	if (IsUsingGamepad())
	{
		LastCursorTarget.Reset();
	}
	//RefreshCursorVisibility();
}

void FCommonAnalogCursor::RefreshCursorSettings()
{
	const FCommonAnalogCursorSettings& CursorSettings = UCommonUIInputSettings::Get().GetAnalogCursorSettings();
	Acceleration = CursorSettings.CursorAcceleration;
	MaxSpeed = CursorSettings.CursorMaxSpeed;
	DeadZone = CursorSettings.CursorDeadZone;
	StickySlowdown = CursorSettings.HoverSlowdownFactor;
	Mode = CursorSettings.bEnableCursorAcceleration ? AnalogCursorMode::Accelerated : AnalogCursorMode::Direct;
}

void FCommonAnalogCursor::RefreshCursorVisibility()
{
	FSlateApplication& SlateApp = FSlateApplication::Get();
	if (TSharedPtr<FSlateUser> SlateUser = SlateApp.GetUser(GetOwnerUserIndex()))
	{
		const bool bShowCursor = bIsAnalogMovementEnabled || ActionRouter.ShouldAlwaysShowCursor() || ActiveInputMethod == ECommonInputType::MouseAndKeyboard;

		if (!bShowCursor)
		{
			SlateApp.SetPlatformCursorVisibility(false);
		}
		SlateUser->SetCursorVisibility(bShowCursor);
	}
}

bool FCommonAnalogCursor::IsUsingGamepad() const
{
	return ActiveInputMethod == ECommonInputType::Gamepad;
}

bool FCommonAnalogCursor::ShouldHideCursor() const
{
	bool bUsingMouseForTouch = FSlateApplication::Get().IsFakingTouchEvents();
	const ULocalPlayer& LocalPlayer = *ActionRouter.GetLocalPlayerChecked();
	if (UGameViewportClient* GameViewportClient = LocalPlayer.ViewportClient)
	{
		bUsingMouseForTouch |= GameViewportClient->GetUseMouseForTouch();
	}

	return !bUsingMouseForTouch;
}

void FCommonAnalogCursor::HideCursor()
{
	const TSharedPtr<FSlateUser> SlateUser = FSlateApplication::Get().GetUser(GetOwnerUserIndex());
	const UWorld* World = ActionRouter.GetWorld();
	if (SlateUser && World && World->IsGameWorld())
	{
		UGameViewportClient* GameViewport = World->GetGameViewport();
		if (GameViewport && GameViewport->GetWindow().IsValid() && GameViewport->Viewport)
		{
			const FVector2D TopLeftPos = GameViewport->Viewport->ViewportToVirtualDesktopPixel(FVector2D(0.025f, 0.025f));
			SlateUser->SetCursorPosition(TopLeftPos);
			SlateUser->SetCursorVisibility(false);
		}
	}
}

void FCommonAnalogCursor::SetNormalizedCursorPosition(const FVector2D& RelativeNewPosition)
{
	TSharedPtr<FSlateUser> SlateUser = FSlateApplication::Get().GetUser(GetOwnerUserIndex());
	if (SlateUser)
	{
		const UGameViewportClient* ViewportClient = GetViewportClient();
		if (TSharedPtr<SViewport> ViewportWidget = ViewportClient ? ViewportClient->GetGameViewportWidget() : nullptr)
		{
			const FVector2D ClampedNewPosition(FMath::Clamp(RelativeNewPosition.X, 0.0f, 1.0f), FMath::Clamp(RelativeNewPosition.Y, 0.0f, 1.0f));
			const FVector2D AbsolutePosition = ViewportWidget->GetCachedGeometry().GetAbsolutePositionAtCoordinates(ClampedNewPosition);
			SlateUser->SetCursorPosition(AbsolutePosition);
		}
	}
}

bool FCommonAnalogCursor::IsInViewport(const FVector2D& Position) const
{
	if (const UGameViewportClient* ViewportClient = GetViewportClient())
	{
		TSharedPtr<SViewport> ViewportWidget = ViewportClient->GetGameViewportWidget();
		return ViewportWidget && ViewportWidget->GetCachedGeometry().GetLayoutBoundingRect().ContainsPoint(Position);
	}
	return false;
}

FVector2D FCommonAnalogCursor::ClampPositionToViewport(const FVector2D& InPosition) const
{
	const UGameViewportClient* ViewportClient = GetViewportClient();
	if (TSharedPtr<SViewport> ViewportWidget = ViewportClient ? ViewportClient->GetGameViewportWidget() : nullptr)
	{
		const FGeometry& ViewportGeometry = ViewportWidget->GetCachedGeometry();
		FVector2D LocalPosition = ViewportGeometry.AbsoluteToLocal(InPosition);
		LocalPosition.X = FMath::Clamp(LocalPosition.X, 1.0f, ViewportGeometry.GetLocalSize().X - 1.0f);
		LocalPosition.Y = FMath::Clamp(LocalPosition.Y, 1.0f, ViewportGeometry.GetLocalSize().Y - 1.0f);
		
		return ViewportGeometry.LocalToAbsolute(LocalPosition);
	}

	return InPosition;
}

#undef LOCTEXT_NAMESPACE
