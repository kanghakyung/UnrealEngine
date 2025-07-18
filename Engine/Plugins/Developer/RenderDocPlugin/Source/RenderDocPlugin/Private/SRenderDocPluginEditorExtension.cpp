// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_EDITOR

#include "SRenderDocPluginEditorExtension.h"
#include "SRenderDocPluginHelpWindow.h"

#include "RenderDocPluginCommands.h"
#include "RenderDocPluginModule.h"
#include "RenderDocPluginSettings.h"
#include "RenderDocPluginStyle.h"

#include "Editor/EditorEngine.h"
#include "SEditorViewportToolBarMenu.h"
#include "SViewportToolBarComboMenu.h"
#include "Kismet2/DebuggerCommands.h"
#include "Styling/AppStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/MultiBox/MultiBoxDefs.h"
#include "Interfaces/IMainFrameModule.h"
#include "LevelEditor.h"
#include "RHI.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "ViewportToolbar/UnrealEdViewportToolbar.h"
#include "ToolMenus.h"

extern UNREALED_API UEditorEngine* GEditor;

class SRenderDocCaptureButton : public SViewportToolBar
{
public:
	SLATE_BEGIN_ARGS(SRenderDocCaptureButton) { }
	SLATE_END_ARGS()

	/** Widget constructor */
	void Construct(const FArguments& Args)
	{
		FSlateIcon IconBrush = FSlateIcon(FRenderDocPluginStyle::Get()->GetStyleSetName(), "RenderDocPlugin.CaptureFrame");
		
		ChildSlot
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Bottom)
			.ContentPadding(FMargin(1.0f))
			.ToolTipText(FRenderDocPluginCommands::Get().CaptureFrame->GetDescription())
			.OnClicked_Lambda([this]() 
				{ 
					FPlayWorldCommands::GlobalPlayWorldActions->GetActionForCommand(FRenderDocPluginCommands::Get().CaptureFrame)->Execute();
					return(FReply::Handled()); 
				})
			[
				SNew(SImage)
				.Image(IconBrush.GetIcon())
			]
		];
	}
};

FRenderDocPluginEditorExtension::FRenderDocPluginEditorExtension(FRenderDocPluginModule* ThePlugin)
	: ToolbarExtension()
	, ExtensionManager()
	, ToolbarExtender()
{
	Initialize(ThePlugin);
}

FRenderDocPluginEditorExtension::~FRenderDocPluginEditorExtension()
{
	if (ExtensionManager.IsValid())
	{
		FRenderDocPluginStyle::Shutdown();
		FRenderDocPluginCommands::Unregister();

		ToolbarExtender->RemoveExtension(ToolbarExtension.ToSharedRef());

		ExtensionManager->RemoveExtender(ToolbarExtender);
	}
	else
	{
		ExtensionManager.Reset();
	}
	
	UToolMenus::UnregisterOwner(this);
}

void FRenderDocPluginEditorExtension::Initialize(FRenderDocPluginModule* ThePlugin)
{
	if (GUsingNullRHI)
	{
		UE_LOG(RenderDocPlugin, Display, TEXT("RenderDoc Plugin will not be loaded because a Null RHI (Cook Server, perhaps) is being used."));
		return;
	}

	// The LoadModule request below will crash if running as an editor commandlet!
	check(!IsRunningCommandlet());

	FRenderDocPluginStyle::Initialize();
	FRenderDocPluginCommands::Register();

#if WITH_EDITOR
	if (!IsRunningGame())
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
		TSharedRef<FUICommandList> CommandBindings = LevelEditorModule.GetGlobalLevelEditorActions();
		ExtensionManager = LevelEditorModule.GetToolBarExtensibilityManager();
		ToolbarExtender = MakeShareable(new FExtender);
		ToolbarExtension = ToolbarExtender->AddToolBarExtension("CameraSpeed", EExtensionHook::After, CommandBindings,
			FToolBarExtensionDelegate::CreateLambda([this, ThePlugin](FToolBarBuilder& ToolbarBuilder)
				{
					AddToolbarExtension(ToolbarBuilder, ThePlugin); 
				})
		);
		ExtensionManager->AddExtender(ToolbarExtender);
		
		ExtendToolbar();
	}
#endif // WITH_EDITOR

	// Would be nice to use the preprocessor definition WITH_EDITOR instead, but the user may launch a standalone the game through the editor...
	if (GEditor != nullptr)
	{
		check(FPlayWorldCommands::GlobalPlayWorldActions.IsValid());

		//Register the editor hotkeys
		FPlayWorldCommands::GlobalPlayWorldActions->MapAction(FRenderDocPluginCommands::Get().CaptureFrame,
			FExecuteAction::CreateLambda([]()
		{
			FRenderDocPluginModule& PluginModule = FModuleManager::GetModuleChecked<FRenderDocPluginModule>("RenderDocPlugin");
			PluginModule.CaptureFrame(nullptr, IRenderCaptureProvider::ECaptureFlags_Launch, FString());
		}),
			FCanExecuteAction());

		const URenderDocPluginSettings* Settings = GetDefault<URenderDocPluginSettings>();
		if (Settings->bShowHelpOnStartup)
		{
			GEditor->EditorAddModalWindow(SNew(SRenderDocPluginHelpWindow));
		}
	}
}

void FRenderDocPluginEditorExtension::ExtendToolbar()
{
	FToolMenuOwnerScoped ScopedOwner(this);

	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.ViewportToolbar");
	
	FToolMenuSection& RightSection = Menu->FindOrAddSection("Right");
	FToolMenuEntry& Entry = RightSection.AddMenuEntry(FRenderDocPluginCommands::Get().CaptureFrame);
	Entry.ToolBarData.LabelOverride = FText::GetEmpty();
	Entry.InsertPosition.Position = EToolMenuInsertType::First;
}

void FRenderDocPluginEditorExtension::AddToolbarExtension(FToolBarBuilder& ToolbarBuilder, FRenderDocPluginModule* ThePlugin)
{
#define LOCTEXT_NAMESPACE "LevelEditorToolBar"

	UE_LOG(RenderDocPlugin, Verbose, TEXT("Attaching toolbar extension..."));
	ToolbarBuilder.BeginSection("RenderdocPlugin", false);
	
	TAttribute<EVisibility> Visibility;
	Visibility.BindLambda([]
	{
		return UE::UnrealEd::ShowOldViewportToolbars() ? EVisibility::Visible : EVisibility::Collapsed;
	});
	
	ToolbarBuilder.AddSeparator(NAME_None, Visibility);
	
	ToolbarBuilder.AddWidget(
		SNew(SRenderDocCaptureButton),
		NAME_None,
		true,
		HAlign_Fill,
		FNewMenuDelegate(),
		Visibility
	);
	ToolbarBuilder.EndSection();

#undef LOCTEXT_NAMESPACE
}

#endif //WITH_EDITOR
