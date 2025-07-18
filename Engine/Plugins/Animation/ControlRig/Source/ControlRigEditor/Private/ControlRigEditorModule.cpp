// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigEditorModule.h"

#include "AnimDetails/Customizations/AnimDetailsProxyDetails.h"
#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeSpawner.h"
#include "PropertyEditorModule.h"
#include "ControlRig.h"
#include "ControlRigComponent.h"
#include "GraphEditorActions.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ISequencerModule.h"
#include "IAssetTools.h"
#include "ControlRigEditorStyle.h"
#include "Editor/RigVMEditorStyle.h"
#include "Framework/Docking/LayoutExtender.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/SlateStyle.h"
#include "Modules/ModuleManager.h"
#include "EditorModeRegistry.h"
#include "EditMode/ControlRigEditMode.h"
#include "EditorModeManager.h"
#include "Sequencer/MovieSceneControlRigParameterSection.h"
#include "Sequencer/MovieSceneControlRigSectionDetailsCustomization.h"
#include "EditMode/ControlRigEditModeCommands.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "ToolMenus.h"
#include "ContentBrowserMenuContexts.h"
#include "Components/SkeletalMeshComponent.h"
#include "ContentBrowserModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor/ControlRigEditor.h"
#include "ControlRigBlueprintActions.h"
#include "RigVMBlueprintGeneratedClass.h"
#include "ControlRigGizmoLibraryActions.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetDebugUtilities.h"
#include "EdGraphUtilities.h"
#include "Graph/ControlRigGraphPanelPinFactory.h"
#include <Editor/ControlRigEditorCommands.h>
#include "ControlRigHierarchyCommands.h"
#include "ControlRigModularRigCommands.h"
#include "Animation/AnimSequence.h"
#include "Editor/ControlRigEditorEditMode.h"
#include "ControlRigElementDetails.h"
#include "ControlRigModuleDetails.h"
#include "ControlRigCompilerDetails.h"
#include "ControlRigDrawingDetails.h"
#include "ControlRigAnimGraphDetails.h"
#include "ControlRigBlueprintDetails.h"
#include "ControlRigOverrideDetails.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Sequencer/ControlRigParameterTrackEditor.h"
#include "ActorFactories/ActorFactorySkeletalMesh.h"
#include "ControlRigThumbnailRenderer.h"
#include "RigVMModel/RigVMController.h"
#include "ControlRigBlueprint.h"
#include "Dialogs/Dialogs.h"
#include "Settings/ControlRigSettings.h"
#include "IPersonaToolkit.h"
#include "LevelSequence.h"
#include "AnimSequenceLevelSequenceLink.h"
#include "LevelSequenceActor.h"
#include "ISequencer.h"
#include "ILevelSequenceEditorToolkit.h"
#include "ClassViewerModule.h"
#include "Editor.h"
#include "LevelEditorViewport.h"
#include "Animation/SkeletalMeshActor.h"
#include "Sequencer/MovieSceneControlRigParameterTrack.h"
#include "ControlRigObjectBinding.h"
#include "LevelSequenceAnimSequenceLink.h"
#include "Rigs/FKControlRig.h"
#include "SBakeToControlRigDialog.h"
#include "Units/Execution/RigUnit_InverseExecution.h"
#include "Widgets/SRigVMGraphPinVariableBinding.h"
#include "ControlRigPythonLogDetails.h"
#include "Dialog/SCustomDialog.h"
#include "Sequencer/MovieSceneControlRigSpaceChannel.h"
#include "SequencerChannelInterface.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "ICurveEditorModule.h"
#include "Channels/SCurveEditorKeyBarView.h"
#include "ControlRigSpaceChannelCurveModel.h"
#include "ControlRigSpaceChannelEditors.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"
#include "UObject/FieldIterator.h"
#include "AnimationToolMenuContext.h"
#include "ControlConstraintChannelInterface.h"
#include "RigVMModel/Nodes/RigVMAggregateNode.h"
#include "RigVMUserWorkflowRegistry.h"
#include "Units/ControlRigNodeWorkflow.h"
#include "RigVMFunctions/Math/RigVMMathLibrary.h"
#include "Constraints/ControlRigTransformableHandle.h"
#include "Constraints/TransformConstraintChannelInterface.h"
#include "Async/TaskGraphInterfaces.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "SequencerUtilities.h"
#include "Bindings/MovieSceneSpawnableActorBinding.h"
#if WITH_RIGVMLEGACYEDITOR
#include "Editor/ControlRigLegacyEditor.h"
#endif
#include "Editor/ControlRigNewEditor.h"

#if WITH_RIGVMLEGACYEDITOR
#include "SKismetInspector.h"
#else
#include "Editor/SRigVMDetailsInspector.h"
#endif

#define LOCTEXT_NAMESPACE "ControlRigEditorModule"

DEFINE_LOG_CATEGORY(LogControlRigEditor);

void FControlRigEditorModule::StartupModule()
{
	using namespace UE::ControlRigEditor;

	FControlRigEditModeCommands::Register();
	FControlRigEditorCommands::Register();
	FControlRigHierarchyCommands::Register();
	FControlRigModularRigCommands::Register();
	FControlRigEditorStyle::Get();

	EdGraphPanelPinFactory = MakeShared<FControlRigGraphPanelPinFactory>();
	FEdGraphUtilities::RegisterVisualPinFactory(EdGraphPanelPinFactory);

	StartupModuleCommon();

	// Register details customizations for animation controller nodes
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	ClassesToUnregisterOnShutdown.Reset();

	ClassesToUnregisterOnShutdown.Add(UMovieSceneControlRigParameterSection::StaticClass()->GetFName());
	PropertyEditorModule.RegisterCustomClassLayout(ClassesToUnregisterOnShutdown.Last(), FOnGetDetailCustomizationInstance::CreateStatic(&FMovieSceneControlRigSectionDetailsCustomization::MakeInstance));

	ClassesToUnregisterOnShutdown.Add(UControlRigBlueprint::StaticClass()->GetFName());
	PropertyEditorModule.RegisterCustomClassLayout(ClassesToUnregisterOnShutdown.Last(), FOnGetDetailCustomizationInstance::CreateStatic(&FControlRigBlueprintDetails::MakeInstance));

	ClassesToUnregisterOnShutdown.Add(UControlRig::StaticClass()->GetFName());
	PropertyEditorModule.RegisterCustomClassLayout(ClassesToUnregisterOnShutdown.Last(), FOnGetDetailCustomizationInstance::CreateStatic(&FRigModuleInstanceDetails::MakeInstance));

	ClassesToUnregisterOnShutdown.Add(UControlRig::StaticClass()->GetFName());

	const FName PropertyEditorModuleName("PropertyEditor");
	FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>(PropertyEditorModuleName);

	// same as ClassesToUnregisterOnShutdown but for properties, there is none right now
	PropertiesToUnregisterOnShutdown.Reset();
	PropertiesToUnregisterOnShutdown.Add(FRigVMCompileSettings::StaticStruct()->GetFName());
	PropertyEditorModule.RegisterCustomPropertyTypeLayout(PropertiesToUnregisterOnShutdown.Last(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FRigVMCompileSettingsDetails::MakeInstance));

	PropertiesToUnregisterOnShutdown.Add(FRigVMPythonSettings::StaticStruct()->GetFName());
	PropertyEditorModule.RegisterCustomPropertyTypeLayout(PropertiesToUnregisterOnShutdown.Last(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FControlRigPythonLogDetails::MakeInstance));

	PropertiesToUnregisterOnShutdown.Add(FRigVMDrawContainer::StaticStruct()->GetFName());
	PropertyEditorModule.RegisterCustomPropertyTypeLayout(PropertiesToUnregisterOnShutdown.Last(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FControlRigDrawContainerDetails::MakeInstance));

	PropertiesToUnregisterOnShutdown.Add(FRigElementKey::StaticStruct()->GetFName());
	PropertyEditorModule.RegisterCustomPropertyTypeLayout(PropertiesToUnregisterOnShutdown.Last(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FRigElementKeyDetails::MakeInstance));

	PropertiesToUnregisterOnShutdown.Add(FRigComponentKey::StaticStruct()->GetFName());
	PropertyEditorModule.RegisterCustomPropertyTypeLayout(PropertiesToUnregisterOnShutdown.Last(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FRigComponentKeyDetails::MakeInstance));

	PropertiesToUnregisterOnShutdown.Add(FRigComputedTransform::StaticStruct()->GetFName());
	PropertyEditorModule.RegisterCustomPropertyTypeLayout(PropertiesToUnregisterOnShutdown.Last(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FRigComputedTransformDetails::MakeInstance));

	PropertiesToUnregisterOnShutdown.Add(FControlRigAnimNodeEventName::StaticStruct()->GetFName());
	PropertyEditorModule.RegisterCustomPropertyTypeLayout(PropertiesToUnregisterOnShutdown.Last(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FControlRigAnimNodeEventNameDetails::MakeInstance));

	PropertiesToUnregisterOnShutdown.Add(StaticEnum<ERigControlTransformChannel>()->GetFName());
	PropertyEditorModule.RegisterCustomPropertyTypeLayout(PropertiesToUnregisterOnShutdown.Last(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FRigControlTransformChannelDetails::MakeInstance));

	PropertiesToUnregisterOnShutdown.Add(FRigConnectionRuleStash::StaticStruct()->GetFName());
	PropertyEditorModule.RegisterCustomPropertyTypeLayout(PropertiesToUnregisterOnShutdown.Last(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FRigConnectionRuleDetails::MakeInstance));

	FRigBaseElementDetails::RegisterSectionMappings(PropertyEditorModule);

	PropertiesToUnregisterOnShutdown.Add(FControlRigOverrideContainer::StaticStruct()->GetFName());
	PropertyEditorModule.RegisterCustomPropertyTypeLayout(PropertiesToUnregisterOnShutdown.Last(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FControlRigOverrideDetails::MakeInstance));

	// Register asset tools
	auto RegisterAssetTypeAction = [this](const TSharedRef<IAssetTypeActions>& InAssetTypeAction)
	{
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		RegisteredAssetTypeActions.Add(InAssetTypeAction);
		AssetTools.RegisterAssetTypeActions(InAssetTypeAction);
	};

	RegisterAssetTypeAction(MakeShareable(new FControlRigBlueprintActions()));
	RegisterAssetTypeAction(MakeShareable(new FControlRigShapeLibraryActions()));
	
	// Register sequencer track editor
	ISequencerModule& SequencerModule = FModuleManager::Get().LoadModuleChecked<ISequencerModule>("Sequencer");
	SequencerModule.RegisterChannelInterface<FMovieSceneControlRigSpaceChannel>();
	ControlRigParameterTrackCreateEditorHandle = SequencerModule.RegisterTrackEditor(FOnCreateTrackEditor::CreateStatic(&FControlRigParameterTrackEditor::CreateTrackEditor));

	// register UTransformableControlHandle animatable interface
	FConstraintChannelInterfaceRegistry& ConstraintChannelInterfaceRegistry = FConstraintChannelInterfaceRegistry::Get();
	ConstraintChannelInterfaceRegistry.RegisterConstraintChannelInterface<UTransformableControlHandle>(MakeUnique<FControlConstraintChannelInterface>());
	
	AddControlRigExtenderToToolMenu("AssetEditor.AnimationEditor.ToolBar");

	FEditorModeRegistry::Get().RegisterMode<FControlRigEditMode>(
		FControlRigEditMode::ModeName,
		NSLOCTEXT("AnimationModeToolkit", "DisplayName", "Animation"),
		FSlateIcon(FRigVMEditorStyle::Get().GetStyleSetName(), "RigVMEditMode", "RigVMEditMode.Small"),
		true,
		8000);

	FEditorModeRegistry::Get().RegisterMode<FControlRigEditorEditMode>(
		FControlRigEditorEditMode::ModeName,
		NSLOCTEXT("RiggingModeToolkit", "DisplayName", "Rigging"),
		FSlateIcon(FRigVMEditorStyle::Get().GetStyleSetName(), "RigVMEditMode", "RigVMEditMode.Small"),
		false,
		8500);

	FEditorModeRegistry::Get().RegisterMode<FModularRigEditorEditMode>(
		FModularRigEditorEditMode::ModeName,
		NSLOCTEXT("RiggingModeToolkit", "DisplayName", "Rigging"),
		FSlateIcon(FRigVMEditorStyle::Get().GetStyleSetName(), "RigVMEditMode", "RigVMEditMode.Small"),
		false,
		9000);

	ICurveEditorModule& CurveEditorModule = FModuleManager::LoadModuleChecked<ICurveEditorModule>("CurveEditor");
	FControlRigSpaceChannelCurveModel::ViewID = CurveEditorModule.RegisterView(FOnCreateCurveEditorView::CreateStatic(
		[](TWeakPtr<FCurveEditor> WeakCurveEditor) -> TSharedRef<SCurveEditorView>
		{
			return SNew(SCurveEditorKeyBarView, WeakCurveEditor);
		}
	));

	FControlRigBlueprintActions::ExtendSketalMeshToolMenu();
	ExtendAnimSequenceMenu();

	UActorFactorySkeletalMesh::RegisterDelegatesForAssetClass(
		UControlRigBlueprint::StaticClass(),
		FGetSkeletalMeshFromAssetDelegate::CreateStatic(&FControlRigBlueprintActions::GetSkeletalMeshFromControlRigBlueprint),
		FPostSkeletalMeshActorSpawnedDelegate::CreateStatic(&FControlRigBlueprintActions::PostSpawningSkeletalMeshActor)
	);

	UThumbnailManager::Get().RegisterCustomRenderer(UControlRigBlueprint::StaticClass(), UControlRigThumbnailRenderer::StaticClass());
	//UThumbnailManager::Get().RegisterCustomRenderer(UControlRigPoseAsset::StaticClass(), UControlRigPoseThumbnailRenderer::StaticClass());

	bFilterAssetBySkeleton = true;

	URigVMUserWorkflowRegistry* WorkflowRegistry = URigVMUserWorkflowRegistry::Get();

	// register the workflow provider for ANY node
	FRigVMUserWorkflowProvider Provider;
	Provider.BindUFunction(UControlRigTransformWorkflowOptions::StaticClass()->GetDefaultObject(), TEXT("ProvideWorkflows"));
	WorkflowHandles.Add(WorkflowRegistry->RegisterProvider(nullptr, Provider));
}

void FControlRigEditorModule::ShutdownModule()
{
	if (ICurveEditorModule* CurveEditorModule = FModuleManager::GetModulePtr<ICurveEditorModule>("CurveEditor"))
	{
		CurveEditorModule->UnregisterView(FControlRigSpaceChannelCurveModel::ViewID);
	}

	ShutdownModuleCommon();

	//UThumbnailManager::Get().UnregisterCustomRenderer(UControlRigBlueprint::StaticClass());
	//UActorFactorySkeletalMesh::UnregisterDelegatesForAssetClass(UControlRigBlueprint::StaticClass());

	FEditorModeRegistry::Get().UnregisterMode(FModularRigEditorEditMode::ModeName);
	FEditorModeRegistry::Get().UnregisterMode(FControlRigEditorEditMode::ModeName);
	FEditorModeRegistry::Get().UnregisterMode(FControlRigEditMode::ModeName);

	FEdGraphUtilities::UnregisterVisualPinFactory(EdGraphPanelPinFactory);

	ISequencerModule* SequencerModule = FModuleManager::GetModulePtr<ISequencerModule>("Sequencer");
	if (SequencerModule)
	{
		SequencerModule->UnRegisterTrackEditor(ControlRigParameterTrackCreateEditorHandle);
	}

	FAssetToolsModule* AssetToolsModule = FModuleManager::GetModulePtr<FAssetToolsModule>("AssetTools");
	if (AssetToolsModule)
	{
		for (TSharedRef<IAssetTypeActions> RegisteredAssetTypeAction : RegisteredAssetTypeActions)
		{
			AssetToolsModule->Get().UnregisterAssetTypeActions(RegisteredAssetTypeAction);
		}
	}

	FPropertyEditorModule* PropertyEditorModule = FModuleManager::GetModulePtr<FPropertyEditorModule>("PropertyEditor");
	if (PropertyEditorModule)
	{
		for (int32 Index = 0; Index < ClassesToUnregisterOnShutdown.Num(); ++Index)
		{
			PropertyEditorModule->UnregisterCustomClassLayout(ClassesToUnregisterOnShutdown[Index]);
		}

		for (int32 Index = 0; Index < PropertiesToUnregisterOnShutdown.Num(); ++Index)
		{
			PropertyEditorModule->UnregisterCustomPropertyTypeLayout(PropertiesToUnregisterOnShutdown[Index]);
		}
	}

	if (UObjectInitialized())
	{
		for(const int32 WorkflowHandle : WorkflowHandles)
		{
			if(URigVMUserWorkflowRegistry::StaticClass()->GetDefaultObject(false) != nullptr)
			{
				URigVMUserWorkflowRegistry::Get()->UnregisterProvider(WorkflowHandle);
			}
		}
	}
	WorkflowHandles.Reset();

	UnregisterPropertySectionMappings();
}

UClass* FControlRigEditorModule::GetRigVMBlueprintClass() const
{
	return UControlRigBlueprint::StaticClass();
}

TSharedRef<FPropertySection> FControlRigEditorModule::RegisterPropertySection(FPropertyEditorModule& PropertyModule, FName ClassName, FName SectionName, FText DisplayName)
{
	TSharedRef<FPropertySection> PropertySection = PropertyModule.FindOrCreateSection(ClassName, SectionName, DisplayName);
	RegisteredPropertySections.Add(ClassName, SectionName);

	return PropertySection;
}

void FControlRigEditorModule::UnregisterPropertySectionMappings()
{
	const FName PropertyEditorModuleName("PropertyEditor");
	FPropertyEditorModule* PropertyModule = FModuleManager::GetModulePtr<FPropertyEditorModule>(PropertyEditorModuleName);

	if (!PropertyModule)
	{
		return;
	}

	for (TMultiMap<FName, FName>::TIterator PropertySectionIterator = RegisteredPropertySections.CreateIterator(); PropertySectionIterator; ++PropertySectionIterator)
	{
		PropertyModule->RemoveSection(PropertySectionIterator->Key, PropertySectionIterator->Value);
		PropertySectionIterator.RemoveCurrent();
	}

	RegisteredPropertySections.Empty();
}

void FControlRigEditorModule::GetNodeContextMenuActions(IRigVMClientHost* RigVMClientHost,
	const URigVMEdGraphNode* EdGraphNode, URigVMNode* ModelNode, UToolMenu* Menu) const
{
	FRigVMEditorModule::GetNodeContextMenuActions(RigVMClientHost, EdGraphNode, ModelNode, Menu);

	UControlRigBlueprint* ControlRigBlueprint = Cast<UControlRigBlueprint>(RigVMClientHost);
	if(ControlRigBlueprint == nullptr)
	{
		return;
	}

	URigVMGraph* Model = RigVMClientHost->GetRigVMClient()->GetModel(EdGraphNode->GetGraph());
	URigVMController* Controller = RigVMClientHost->GetRigVMClient()->GetController(Model);

	TArray<FName> SelectedNodeNames = Model->GetSelectNodes();
	SelectedNodeNames.AddUnique(ModelNode->GetFName());

	URigHierarchy* TemporaryHierarchy = NewObject<URigHierarchy>();
	TemporaryHierarchy->CopyHierarchy(ControlRigBlueprint->Hierarchy);

	TArray<FRigElementKey> RigElementsToSelect;
	TMap<const URigVMPin*, FRigElementKey> PinToKey;

	for(const FName& SelectedNodeName : SelectedNodeNames)
	{
		if (URigVMNode* FoundNode = Model->FindNodeByName(SelectedNodeName))
		{
			TSharedPtr<FStructOnScope> StructOnScope;
			FRigVMStruct* StructMemory = nullptr;
			UScriptStruct* ScriptStruct = nullptr;
			if (URigVMUnitNode* UnitNode = Cast<URigVMUnitNode>(FoundNode))
			{
				ScriptStruct = UnitNode->GetScriptStruct();
				if(ScriptStruct)
				{
					StructOnScope = UnitNode->ConstructStructInstance(false);
					if(StructOnScope->GetStruct()->IsChildOf(FRigVMStruct::StaticStruct()))
					{
						StructMemory = (FRigVMStruct*)StructOnScope->GetStructMemory();
						StructMemory->Execute();
					}
				}
			}

			const TArray<URigVMPin*> AllPins = FoundNode->GetAllPinsRecursively();
			for (const URigVMPin* Pin : AllPins)
			{
				if (Pin->GetCPPType() == TEXT("FName"))
				{
					FRigElementKey Key;
					if (Pin->GetCustomWidgetName() == TEXT("BoneName"))
					{
						Key = FRigElementKey(*Pin->GetDefaultValue(), ERigElementType::Bone);
					}
					else if (Pin->GetCustomWidgetName() == TEXT("ControlName"))
					{
						Key = FRigElementKey(*Pin->GetDefaultValue(), ERigElementType::Control);
					}
					else if (Pin->GetCustomWidgetName() == TEXT("SpaceName"))
					{
						Key = FRigElementKey(*Pin->GetDefaultValue(), ERigElementType::Null);
					}
					else if (Pin->GetCustomWidgetName() == TEXT("CurveName"))
					{
						Key = FRigElementKey(*Pin->GetDefaultValue(), ERigElementType::Curve);
					}
					else
					{
						continue;
					}

					RigElementsToSelect.AddUnique(Key);
					PinToKey.Add(Pin, Key);
				}
				else if (Pin->GetCPPTypeObject() == FRigElementKey::StaticStruct() && !Pin->IsArray())
				{
					if (StructMemory == nullptr)
					{
						FString DefaultValue = Pin->GetDefaultValue();
						if (!DefaultValue.IsEmpty())
						{
							FRigElementKey Key;
							FRigElementKey::StaticStruct()->ImportText(*DefaultValue, &Key, nullptr, EPropertyPortFlags::PPF_None, nullptr, FRigElementKey::StaticStruct()->GetName(), true);
							if (Key.IsValid())
							{
								RigElementsToSelect.AddUnique(Key);
								if (URigVMPin* NamePin = Pin->FindSubPin(TEXT("Name")))
								{
									PinToKey.Add(NamePin, Key);
								}
							}
						}
					}
					else
					{
						check(ScriptStruct);

						TArray<FString> PropertyNames; 
						if(!URigVMPin::SplitPinPath(Pin->GetSegmentPath(true), PropertyNames))
						{
							PropertyNames.Add(Pin->GetName());
						}

						UScriptStruct* Struct = ScriptStruct;
						uint8* Memory = (uint8*)StructMemory; 

						while(!PropertyNames.IsEmpty())
						{
							FString PropertyName;
							PropertyNames.HeapPop(PropertyName);
							
							const FProperty* Property = ScriptStruct->FindPropertyByName(*PropertyName);
							if(Property == nullptr)
							{
								Memory = nullptr;
								break;
							}

							Memory = Property->ContainerPtrToValuePtr<uint8>(Memory);
							
							if(PropertyNames.IsEmpty())
							{
								continue;
							}
							
							if(const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
							{
								PropertyNames.HeapPop(PropertyName);

								int32 ArrayIndex = FCString::Atoi(*PropertyName);
								FScriptArrayHelper Helper(ArrayProperty, Memory);
								if(!Helper.IsValidIndex(ArrayIndex))
								{
									Memory = nullptr;
									break;
								}

								Memory = Helper.GetRawPtr(ArrayIndex);
								Property = ArrayProperty->Inner;
							}

							if(const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
							{
								Struct = StructProperty->Struct;
							}
						}

						if(Memory)
						{
							const FRigElementKey& Key = *(const FRigElementKey*)Memory;
							if (Key.IsValid())
							{
								RigElementsToSelect.AddUnique(Key);

								if (URigVMPin* NamePin = Pin->FindSubPin(TEXT("Name")))
								{
									PinToKey.Add(NamePin, Key);
								}
							}
						}
					}
				}
				else if (Pin->GetCPPTypeObject() == FRigElementKeyCollection::StaticStruct() && Pin->GetDirection() == ERigVMPinDirection::Output)
				{
					if (StructMemory == nullptr)
					{
						// not supported for now
					}
					else
					{
						check(ScriptStruct);
						if (const FProperty* Property = ScriptStruct->FindPropertyByName(Pin->GetFName()))
						{
							const FRigElementKeyCollection& Collection = *Property->ContainerPtrToValuePtr<FRigElementKeyCollection>(StructMemory);

							if (Collection.Num() > 0)
							{
								RigElementsToSelect.Reset();
								for (const FRigElementKey& Item : Collection)
								{
									RigElementsToSelect.AddUnique(Item);
								}
								break;
							}
						}
					}
				}
			}
		}
	}

	GetDirectManipulationMenuActions(RigVMClientHost, ModelNode, nullptr, Menu);

	if (RigElementsToSelect.Num() > 0)
	{
		FToolMenuSection& Section = Menu->AddSection("RigVMEditorContextMenuHierarchy", LOCTEXT("HierarchyHeader", "Hierarchy"));
		Section.AddMenuEntry(
			"SelectRigElements",
			LOCTEXT("SelectRigElements", "Select Rig Elements"),
			LOCTEXT("SelectRigElements_Tooltip", "Selects the bone, controls or nulls associated with this node."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([ControlRigBlueprint, RigElementsToSelect]() {

				ControlRigBlueprint->GetHierarchyController()->SetSelection(RigElementsToSelect);
				
			})
		));
	}

	if (RigElementsToSelect.Num() > 0)
	{
		FToolMenuSection& Section = Menu->AddSection("RigVMEditorContextMenuHierarchy", LOCTEXT("ToolsHeader", "Tools"));
		Section.AddMenuEntry(
			"SearchAndReplaceNames",
			LOCTEXT("SearchAndReplaceNames", "Search & Replace / Mirror"),
			LOCTEXT("SearchAndReplaceNames_Tooltip", "Searches within all names and replaces with a different text."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([ControlRigBlueprint, Controller, PinToKey]() {

				FRigVMMirrorSettings Settings;
				TSharedPtr<FStructOnScope> StructToDisplay = MakeShareable(new FStructOnScope(FRigVMMirrorSettings::StaticStruct(), (uint8*)&Settings));
#if WITH_RIGVMLEGACYEDITOR
				TSharedRef<SKismetInspector> KismetInspector = SNew(SKismetInspector);
#else
				TSharedRef<SRigVMDetailsInspector> KismetInspector = SNew(SRigVMDetailsInspector);
#endif
				KismetInspector->ShowSingleStruct(StructToDisplay);

				TSharedRef<SCustomDialog> MirrorDialog = SNew(SCustomDialog)
					.Title(FText(LOCTEXT("ControlRigHierarchyMirror", "Mirror Graph")))
					.Content()
					[
						KismetInspector
					]
					.Buttons({
						SCustomDialog::FButton(LOCTEXT("OK", "OK")),
						SCustomDialog::FButton(LOCTEXT("Cancel", "Cancel"))
				});
				if (MirrorDialog->ShowModal() == 0)
				{
					Controller->OpenUndoBracket(TEXT("Mirroring Graph"));
					int32 ReplacedNames = 0;
					TArray<FString> UnchangedItems;

					for (const TPair<const URigVMPin*, FRigElementKey>& Pair : PinToKey)
					{
						const URigVMPin* Pin = Pair.Key;
						FRigElementKey Key = Pair.Value;

						if (Key.Name.IsNone())
						{
							continue;
						}

						FString OldNameStr = Key.Name.ToString();
						FString NewNameStr = OldNameStr.Replace(*Settings.SearchString, *Settings.ReplaceString, ESearchCase::CaseSensitive);
						if(NewNameStr != OldNameStr)
						{
							Key.Name = *NewNameStr;
							if(ControlRigBlueprint->Hierarchy->GetIndex(Key) != INDEX_NONE)
							{
								Controller->SetPinDefaultValue(Pin->GetPinPath(), NewNameStr, false, true, false, true);
								ReplacedNames++;
							}
							else
							{
								// save the names of the items that we skipped during this search & replace
								UnchangedItems.AddUnique(OldNameStr);
							} 
						}
					}

					if (UnchangedItems.Num() > 0)
					{
						FString ListOfUnchangedItems;
						for (int Index = 0; Index < UnchangedItems.Num(); Index++)
						{
							// construct the string "item1, item2, item3"
							ListOfUnchangedItems += UnchangedItems[Index];
							if (Index != UnchangedItems.Num() - 1)
							{
								ListOfUnchangedItems += TEXT(", ");
							}
						}
						
						// inform the user that some items were skipped due to invalid new names
						Controller->ReportAndNotifyError(FString::Printf(TEXT("Invalid Names after Search & Replace, action skipped for %s"), *ListOfUnchangedItems));
					}

					if (ReplacedNames > 0)
					{ 
						Controller->CloseUndoBracket();
					}
					else
					{
						Controller->CancelUndoBracket();
					}
				}
			})
		));
	}

	if (URigVMUnitNode* UnitNode = Cast<URigVMUnitNode>(EdGraphNode->GetModelNode()))
	{
		FToolMenuSection& SettingsSection = Menu->AddSection("RigVMEditorContextMenuSettings", LOCTEXT("SettingsHeader", "Settings"));
		SettingsSection.AddMenuEntry(
			"Save Default Expansion State",
			LOCTEXT("SaveDefaultExpansionState", "Save Default Expansion State"),
			LOCTEXT("SaveDefaultExpansionState_Tooltip", "Saves the expansion state of all pins of the node as the default."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([UnitNode]() {

#if WITH_EDITORONLY_DATA

				FScopedTransaction Transaction(LOCTEXT("RigUnitDefaultExpansionStateChanged", "Changed Rig Unit Default Expansion State"));
				UControlRigEditorSettings::Get()->Modify();

				FControlRigSettingsPerPinBool& ExpansionMap = UControlRigEditorSettings::Get()->RigUnitPinExpansion.FindOrAdd(UnitNode->GetScriptStruct()->GetName());
				ExpansionMap.Values.Empty();

				TArray<URigVMPin*> Pins = UnitNode->GetAllPinsRecursively();
				for (URigVMPin* Pin : Pins)
				{
					if (Pin->GetSubPins().Num() == 0)
					{
						continue;
					}

					FString PinPath = Pin->GetPinPath();
					FString NodeName, RemainingPath;
					URigVMPin::SplitPinPathAtStart(PinPath, NodeName, RemainingPath);
					ExpansionMap.Values.FindOrAdd(RemainingPath) = Pin->IsExpanded();
				}
#endif
			})
		));
	}
}

void FControlRigEditorModule::GetPinContextMenuActions(IRigVMClientHost* RigVMClientHost, const UEdGraphPin* EdGraphPin, URigVMPin* ModelPin, UToolMenu* Menu) const
{
	FRigVMEditorModule::GetPinContextMenuActions(RigVMClientHost, EdGraphPin, ModelPin, Menu);
	GetDirectManipulationMenuActions(RigVMClientHost, ModelPin->GetNode(), ModelPin, Menu);
}

bool FControlRigEditorModule::AssetsPublicFunctionsAllowed(const FAssetData& InAssetData) const
{
	// Looking for public functions in cooked assets only happens in UEFN
	// Make sure we allow only ControlRig/ControlRigSpline/ControlRigModules functions
	// (to avoid adding actions for internal rigs public functions)
	const FString AssetClassPath = InAssetData.AssetClassPath.ToString();
	if (AssetClassPath.Contains(TEXT("ControlRigBlueprintGeneratedClass"))
		|| AssetClassPath.Contains(TEXT("RigVMBlueprintGeneratedClass")))
	{
		const FString PathString = InAssetData.PackagePath.ToString();
		if (!PathString.StartsWith(TEXT("/ControlRig/"))
			&& !PathString.StartsWith(TEXT("/ControlRigSpline/"))
			&& !PathString.StartsWith(TEXT("/ControlRigModules/")))
		{
			return false;
		}
	}
	
	return IControlRigEditorModule::AssetsPublicFunctionsAllowed(InAssetData);
}

void FControlRigEditorModule::GetDirectManipulationMenuActions(IRigVMClientHost* RigVMClientHost, URigVMNode* InNode, URigVMPin* ModelPin, UToolMenu* Menu) const
{
    // Add direct manipulation context menu entries
	if(UControlRigBlueprint* ControlRigBlueprint = Cast<UControlRigBlueprint>(RigVMClientHost))
	{
		UControlRig* DebuggedRig = Cast<UControlRig>(ControlRigBlueprint->GetObjectBeingDebugged());
		if(DebuggedRig == nullptr)
		{
			return;
		}
		
		if(const URigVMUnitNode* UnitNode = Cast<URigVMUnitNode>(InNode))
		{
			if(!UnitNode->IsPartOfRuntime(DebuggedRig))
			{
				return;
			}
			
			const UScriptStruct* ScriptStruct = UnitNode->GetScriptStruct();
			if(ScriptStruct == nullptr)
			{
				return;
			}

			TSharedPtr<FStructOnScope> NodeInstance = UnitNode->ConstructStructInstance(false);
			if(!NodeInstance.IsValid() || !NodeInstance->IsValid())
			{
				return;
			}

			const FRigUnit* UnitInstance = UControlRig::GetRigUnitInstanceFromScope(NodeInstance);
			TArray<FRigDirectManipulationTarget> Targets;
			if(UnitInstance->GetDirectManipulationTargets(UnitNode, NodeInstance, DebuggedRig->GetHierarchy(), Targets, nullptr))
			{
				if(ModelPin)
				{
					Targets.RemoveAll([ModelPin, UnitNode, UnitInstance](const FRigDirectManipulationTarget& Target) -> bool
					{
						const TArray<const URigVMPin*> AffectedPins = UnitInstance->GetPinsForDirectManipulation(UnitNode, Target);
						return !AffectedPins.Contains(ModelPin);
					});
				}

				if(!Targets.IsEmpty())
				{
					FToolMenuSection& Section = Menu->AddSection("RigVMEditorContextMenuControlNode", LOCTEXT("ControlNodeDirectManipulation", "Direct Manipulation"));

					bool bHasPosition = false;
					bool bHasRotation = false;
					bool bHasScale = false;
					
					for(const FRigDirectManipulationTarget& Target : Targets)
					{
						auto IsSliced = [UnitNode]() -> bool
						{
							return UnitNode->IsWithinLoop();
						};
						
						auto HasNoUnconstrainedAffectedPin = [NodeInstance, UnitNode, Target]() -> bool
						{
							const FRigUnit* UnitInstance = UControlRig::GetRigUnitInstanceFromScope(NodeInstance);
							const TArray<const URigVMPin*> AffectedPins = UnitInstance->GetPinsForDirectManipulation(UnitNode, Target);

							int32 NumAffectedPinsWithRootLinks = 0;
							for(const URigVMPin* AffectedPin : AffectedPins)
							{
								if(!AffectedPin->GetRootPin()->GetSourceLinks().IsEmpty())
								{
									NumAffectedPinsWithRootLinks++;
								}
							}
							return NumAffectedPinsWithRootLinks == AffectedPins.Num();
						};

						FText Suffix;
						static const FText SuffixPosition = LOCTEXT("DirectManipulationPosition", " (W)"); 
						static const FText SuffixRotation = LOCTEXT("DirectManipulationRotation", " (E)"); 
						static const FText SuffixScale = LOCTEXT("DirectManipulationScale", " (R)"); 
						TSharedPtr< const FUICommandInfo > CommandInfo;
						if(!CommandInfo.IsValid() && !bHasPosition)
						{
							if(Target.ControlType == ERigControlType::EulerTransform || Target.ControlType == ERigControlType::Position)
							{
								CommandInfo = FControlRigEditorCommands::Get().RequestDirectManipulationPosition;
								Suffix = SuffixPosition;
								bHasPosition = true;
							}
						}
						if(!CommandInfo.IsValid() && !bHasRotation)
						{
							if(Target.ControlType == ERigControlType::EulerTransform || Target.ControlType == ERigControlType::Rotator)
							{
								CommandInfo = FControlRigEditorCommands::Get().RequestDirectManipulationRotation;
								Suffix = SuffixRotation;
								bHasRotation = true;
							}
						}
						if(!CommandInfo.IsValid() && !bHasScale)
						{
							if(Target.ControlType == ERigControlType::EulerTransform)
							{
								CommandInfo = FControlRigEditorCommands::Get().RequestDirectManipulationScale;
								Suffix = SuffixScale;
								bHasScale = true;
							}
						}

						const FText Label = FText::Format(LOCTEXT("ControlNodeLabelFormat", "Manipulate {0}{1}"), FText::FromString(Target.Name), Suffix);
						TAttribute<FText> ToolTipAttribute = TAttribute<FText>::CreateLambda([HasNoUnconstrainedAffectedPin, IsSliced, Target]() -> FText
						{
							if(HasNoUnconstrainedAffectedPin())
							{
								return FText::Format(LOCTEXT("ControlNodeLabelFormat_Tooltip_FullyConstrained", "The value of {0} cannot be manipulated, its pins have links fully constraining it."), FText::FromString(Target.Name));
							}
							if(IsSliced())
							{
								return FText::Format(LOCTEXT("ControlNodeLabelFormat_Tooltip_Sliced", "The value of {0} cannot be manipulated, the node is linked to a loop."), FText::FromString(Target.Name));
							}
							return FText::Format(LOCTEXT("ControlNodeLabelFormat_Tooltip", "Manipulate the value of {0} interactively"), FText::FromString(Target.Name));
						});

						FToolMenuEntry& MenuEntry = Section.AddMenuEntry(
							*Target.Name,
							Label,
							ToolTipAttribute,
							FSlateIcon(),
							FUIAction(FExecuteAction::CreateLambda([ControlRigBlueprint, UnitNode, Target]() {

								// disable literal folding for the moment
								if(ControlRigBlueprint->VMCompileSettings.ASTSettings.bFoldLiterals)
								{
									ControlRigBlueprint->VMCompileSettings.ASTSettings.bFoldLiterals = false;
									ControlRigBlueprint->RecompileVM();
								}

								// run the task after a bit so that the rig has the opportunity to run first
								FFunctionGraphTask::CreateAndDispatchWhenReady([ControlRigBlueprint, UnitNode, Target]()
								{
									ControlRigBlueprint->AddTransientControl(UnitNode, Target);
								}, TStatId(), NULL, ENamedThreads::GameThread);
							}),
							FCanExecuteAction::CreateLambda([HasNoUnconstrainedAffectedPin, IsSliced]() -> bool
							{
								if(HasNoUnconstrainedAffectedPin() || IsSliced())
								{
									return false;
								}
								return true;
							})
						));
					}
				}
			}
		}
	}
}

TSharedRef< SWidget > FControlRigEditorModule::GenerateAnimationMenu(TWeakPtr<IAnimationEditor> InAnimationEditor)
{
	const bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, nullptr);
	
	if(InAnimationEditor.IsValid())
	{
		TSharedRef<IAnimationEditor> AnimationEditor = InAnimationEditor.Pin().ToSharedRef();
		USkeleton* Skeleton = AnimationEditor->GetPersonaToolkit()->GetSkeleton();
		USkeletalMesh* SkeletalMesh = AnimationEditor->GetPersonaToolkit()->GetPreviewMesh();
		if (!SkeletalMesh) //if no preview mesh just get normal mesh
		{
			SkeletalMesh = AnimationEditor->GetPersonaToolkit()->GetMesh();
		}
		
		UAnimSequence* AnimSequence = Cast<UAnimSequence>(AnimationEditor->GetPersonaToolkit()->GetAnimationAsset());
		if (Skeleton && SkeletalMesh && AnimSequence)
		{
			FUIAction EditWithFKControlRig(
				FExecuteAction::CreateRaw(this, &FControlRigEditorModule::EditWithFKControlRig, AnimSequence, SkeletalMesh, Skeleton));

			FUIAction OpenIt(
				FExecuteAction::CreateStatic(&FControlRigEditorModule::OpenLevelSequence, AnimSequence),
				FCanExecuteAction::CreateLambda([AnimSequence]()
					{
						if (AnimSequence)
						{
							if (IInterface_AssetUserData* AnimAssetUserData = Cast< IInterface_AssetUserData >(AnimSequence))
							{
								UAnimSequenceLevelSequenceLink* AnimLevelLink = AnimAssetUserData->GetAssetUserData< UAnimSequenceLevelSequenceLink >();
								if (AnimLevelLink)
								{
									ULevelSequence* LevelSequence = AnimLevelLink->ResolveLevelSequence();
									if (LevelSequence)
									{
										return true;
									}
								}
							}
						}
						return false;
					}
				)

			);

			FUIAction UnLinkIt(
				FExecuteAction::CreateStatic(&FControlRigEditorModule::UnLinkLevelSequence, AnimSequence),
				FCanExecuteAction::CreateLambda([AnimSequence]()
					{
						if (AnimSequence)
						{
							if (IInterface_AssetUserData* AnimAssetUserData = Cast< IInterface_AssetUserData >(AnimSequence))
							{
								UAnimSequenceLevelSequenceLink* AnimLevelLink = AnimAssetUserData->GetAssetUserData< UAnimSequenceLevelSequenceLink >();
								if (AnimLevelLink)
								{
									ULevelSequence* LevelSequence = AnimLevelLink->ResolveLevelSequence();
									if (LevelSequence)
									{
										return true;
									}
								}
							}
						}
						return false;
					}
				)

			);

			FUIAction ToggleFilterAssetBySkeleton(
				FExecuteAction::CreateLambda([this]()
					{
						bFilterAssetBySkeleton = bFilterAssetBySkeleton ? false : true;
					}
				),
				FCanExecuteAction(),
						FIsActionChecked::CreateLambda([this]()
							{
								return bFilterAssetBySkeleton;
							}
						)
						);
			if (Skeleton)
			{
				MenuBuilder.BeginSection("Control Rig", LOCTEXT("ControlRig", "Control Rig"));
				{
					MenuBuilder.AddMenuEntry(LOCTEXT("EditWithFKControlRig", "Edit With FK Control Rig"),
						FText(), FSlateIcon(), EditWithFKControlRig, NAME_None, EUserInterfaceActionType::Button);


					MenuBuilder.AddMenuEntry(
						LOCTEXT("FilterAssetBySkeleton", "Filter Asset By Skeleton"),
						LOCTEXT("FilterAssetBySkeletonTooltip", "Filters Control Rig Assets To Match Current Skeleton"),
						FSlateIcon(),
						ToggleFilterAssetBySkeleton,
						NAME_None,
						EUserInterfaceActionType::ToggleButton);

					MenuBuilder.AddSubMenu(
						LOCTEXT("BakeToControlRig", "Bake To Control Rig"), NSLOCTEXT("AnimationModeToolkit", "BakeToControlRigTooltip", "This Control Rig will Drive This Animation."),
						FNewMenuDelegate::CreateLambda([this, AnimSequence, SkeletalMesh, Skeleton](FMenuBuilder& InSubMenuBuilder)
							{
								FClassViewerInitializationOptions Options;
								Options.bShowUnloadedBlueprints = true;
								Options.NameTypeToDisplay = EClassViewerNameTypeToDisplay::DisplayName;

								TSharedPtr<FControlRigClassFilter> ClassFilter = MakeShareable(new FControlRigClassFilter(bFilterAssetBySkeleton, false, true, Skeleton));
								Options.ClassFilters.Add(ClassFilter.ToSharedRef());
								Options.bShowNoneOption = false;

								FClassViewerModule& ClassViewerModule = FModuleManager::LoadModuleChecked<FClassViewerModule>("ClassViewer");

								TSharedRef<SWidget> ClassViewer = ClassViewerModule.CreateClassViewer(Options, FOnClassPicked::CreateRaw(this, &FControlRigEditorModule::BakeToControlRig, AnimSequence, SkeletalMesh, Skeleton));
								InSubMenuBuilder.AddWidget(ClassViewer, FText::GetEmpty(), true);
							})
					);
				}
				MenuBuilder.EndSection();

			}

			MenuBuilder.AddMenuEntry(LOCTEXT("OpenLevelSequence", "Open Level Sequence"),
				FText(), FSlateIcon(), OpenIt, NAME_None, EUserInterfaceActionType::Button);

			MenuBuilder.AddMenuEntry(LOCTEXT("UnlinkLevelSequence", "Unlink Level Sequence"),
				FText(), FSlateIcon(), UnLinkIt, NAME_None, EUserInterfaceActionType::Button);

		}
	}
	return MenuBuilder.MakeWidget();

}


void FControlRigEditorModule::ToggleIsDrivenByLevelSequence(UAnimSequence* AnimSequence) const
{

//todo what?
}

bool FControlRigEditorModule::IsDrivenByLevelSequence(UAnimSequence* AnimSequence) const
{
	if (AnimSequence->GetClass()->ImplementsInterface(UInterface_AssetUserData::StaticClass()))
	{
		if (IInterface_AssetUserData* AnimAssetUserData = Cast< IInterface_AssetUserData >(AnimSequence))
		{
			UAnimSequenceLevelSequenceLink* AnimLevelLink = AnimAssetUserData->GetAssetUserData< UAnimSequenceLevelSequenceLink >();
			return (AnimLevelLink != nullptr) ? true : false;
		}
	}
	return false;
}

void FControlRigEditorModule::EditWithFKControlRig(UAnimSequence* AnimSequence, USkeletalMesh* SkelMesh, USkeleton* InSkeleton)
{
	BakeToControlRig(UFKControlRig::StaticClass(),AnimSequence, SkelMesh, InSkeleton);
}


void FControlRigEditorModule::BakeToControlRig(UClass* ControlRigClass, UAnimSequence* AnimSequence, USkeletalMesh* SkelMesh, USkeleton* InSkeleton)
{
	FSlateApplication::Get().DismissAllMenus();

	UWorld* World = GCurrentLevelEditingViewportClient ? GCurrentLevelEditingViewportClient->GetWorld() : nullptr;

	if (World)
	{
		FControlRigEditorModule::UnLinkLevelSequence(AnimSequence);

		FString SequenceName = FString::Printf(TEXT("Driving_%s"), *AnimSequence->GetName());
		FString PackagePath = AnimSequence->GetOutermost()->GetName();
		
		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		FString UniquePackageName;
		FString UniqueAssetName;
		AssetToolsModule.Get().CreateUniqueAssetName(PackagePath / SequenceName, TEXT(""), UniquePackageName, UniqueAssetName);

		UPackage* Package = CreatePackage(*UniquePackageName);
		ULevelSequence* LevelSequence = NewObject<ULevelSequence>(Package, *UniqueAssetName, RF_Public | RF_Standalone);
					
		FAssetRegistryModule::AssetCreated(LevelSequence);

		LevelSequence->Initialize(); //creates movie scene
		LevelSequence->MarkPackageDirty();
		UMovieScene* MovieScene = LevelSequence->GetMovieScene();

		FFrameRate TickResolution = MovieScene->GetTickResolution();
		float Duration = AnimSequence->GetPlayLength();
		MovieScene->SetPlaybackRange(0, (Duration * TickResolution).FloorToFrame().Value);
		FFrameRate SequenceFrameRate = AnimSequence->GetSamplingFrameRate();
		MovieScene->SetDisplayRate(SequenceFrameRate);


		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(LevelSequence);

		IAssetEditorInstance* AssetEditor = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorForAsset(LevelSequence, false);
		ILevelSequenceEditorToolkit* LevelSequenceEditor = static_cast<ILevelSequenceEditorToolkit*>(AssetEditor);
		TWeakPtr<ISequencer> WeakSequencer = LevelSequenceEditor ? LevelSequenceEditor->GetSequencer() : nullptr;

		if (WeakSequencer.IsValid())
		{
			TSharedPtr<ISequencer> SequencerPtr = WeakSequencer.Pin();
			ASkeletalMeshActor* MeshActor = World->SpawnActor<ASkeletalMeshActor>(ASkeletalMeshActor::StaticClass(), FTransform::Identity);
			MeshActor->SetActorLabel(AnimSequence->GetName());

			FString StringName = MeshActor->GetActorLabel();
			FString AnimName = AnimSequence->GetName();
			StringName = StringName + FString(TEXT(" --> ")) + AnimName;
			MeshActor->SetActorLabel(StringName);
			if (SkelMesh)
			{
				MeshActor->GetSkeletalMeshComponent()->SetSkeletalMesh(SkelMesh);
			}
			MeshActor->RegisterAllComponents();
			TArray<TWeakObjectPtr<AActor> > ActorsToAdd;
			ActorsToAdd.Add(MeshActor);
			TArray<FGuid> ActorTracks = SequencerPtr->AddActors(ActorsToAdd, false);
			FGuid ActorTrackGuid = ActorTracks[0];

			// By default, convert this to a spawnable and delete the existing actor. If for some reason, 
			// the spawnable couldn't be generated, use the existing actor as a possessable (this could 
			// eventually be an option)

			if (FMovieScenePossessable* Possessable = FSequencerUtilities::ConvertToCustomBinding(SequencerPtr.ToSharedRef(), ActorTrackGuid, UMovieSceneSpawnableActorBinding::StaticClass(), 0))
			{ 
				ActorTrackGuid = Possessable->GetGuid();

				UObject* SpawnedMesh = SequencerPtr->FindSpawnedObjectOrTemplate(ActorTrackGuid);

				if (SpawnedMesh)
				{
					GCurrentLevelEditingViewportClient->GetWorld()->EditorDestroyActor(MeshActor, true);
					MeshActor = Cast<ASkeletalMeshActor>(SpawnedMesh);
					if (SkelMesh)
					{
						MeshActor->GetSkeletalMeshComponent()->SetSkeletalMesh(SkelMesh);
					}
					MeshActor->RegisterAllComponents();
				}
			}

			//Delete binding from default animating rig
			//if we have skel mesh component binding we can just delete that
			FGuid CompGuid = SequencerPtr->FindObjectId(*(MeshActor->GetSkeletalMeshComponent()), SequencerPtr->GetFocusedTemplateID());
			if (CompGuid.IsValid())
			{
				if (!MovieScene->RemovePossessable(CompGuid))
				{
					MovieScene->RemoveSpawnable(CompGuid);
				}
			}
			else //otherwise if not delete the track
			{
				if (UMovieSceneTrack* ExistingTrack = MovieScene->FindTrack<UMovieSceneControlRigParameterTrack>(ActorTrackGuid))
				{
					MovieScene->RemoveTrack(*ExistingTrack);
				}
			}

			UMovieSceneControlRigParameterTrack* Track = MovieScene->AddTrack<UMovieSceneControlRigParameterTrack>(ActorTrackGuid);
			if (Track)
			{
				USkeletalMeshComponent* SkelMeshComp = MeshActor->GetSkeletalMeshComponent();
				USkeletalMesh* SkeletalMesh = SkelMeshComp->GetSkeletalMeshAsset();

				FString ObjectName = (ControlRigClass->GetName());
				ObjectName.RemoveFromEnd(TEXT("_C"));

				UControlRig* ControlRig = NewObject<UControlRig>(Track, ControlRigClass, FName(*ObjectName), RF_Transactional);
				ControlRig->SetObjectBinding(MakeShared<FControlRigObjectBinding>());
				ControlRig->GetObjectBinding()->BindToObject(MeshActor);
				ControlRig->GetDataSourceRegistry()->RegisterDataSource(UControlRig::OwnerComponent, ControlRig->GetObjectBinding()->GetBoundObject());
				ControlRig->Initialize();
				ControlRig->Evaluate_AnyThread();

				SequencerPtr->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);

				Track->Modify();
				UMovieSceneSection* NewSection = Track->CreateControlRigSection(0, ControlRig, true);
				//mz todo need to have multiple rigs with same class
				Track->SetTrackName(FName(*ObjectName));
				Track->SetDisplayName(FText::FromString(ObjectName));
				UMovieSceneControlRigParameterSection* ParamSection = Cast<UMovieSceneControlRigParameterSection>(NewSection);
				FBakeToControlDelegate BakeCallback = FBakeToControlDelegate::CreateLambda([this, LevelSequence,
					AnimSequence, MovieScene, ControlRig, ParamSection,ActorTrackGuid, SkelMeshComp]
				(bool bKeyReduce, float KeyReduceTolerance, FFrameRate BakeFrameRate, bool bResetControls)
				{
					IAssetEditorInstance* AssetEditor = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorForAsset(LevelSequence, false);
					ILevelSequenceEditorToolkit* LevelSequenceEditor = static_cast<ILevelSequenceEditorToolkit*>(AssetEditor);
					TWeakPtr<ISequencer> WeakSequencer = LevelSequenceEditor ? LevelSequenceEditor->GetSequencer() : nullptr;
					if (WeakSequencer.IsValid())
					{
						TSharedPtr<ISequencer> SequencerPtr = WeakSequencer.Pin();
						if (ParamSection)
						{
							FSmartReduceParams SmartReduce;
							SmartReduce.TolerancePercentage = KeyReduceTolerance;
							SmartReduce.SampleRate = BakeFrameRate;
							TOptional<TRange<FFrameNumber>> AnimFrameRamge; //use whole range there
							const bool bOntoSelectedControls = false;
							FControlRigParameterTrackEditor::LoadAnimationIntoSection(SequencerPtr, AnimSequence, SkelMeshComp, FFrameNumber(0),
								bKeyReduce, SmartReduce, bResetControls, AnimFrameRamge, bOntoSelectedControls, ParamSection);
						}
						SequencerPtr->EmptySelection();
						SequencerPtr->SelectSection(ParamSection);
						SequencerPtr->ThrobSectionSelection();
						SequencerPtr->ObjectImplicitlyAdded(ControlRig);
						SequencerPtr->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);
						FControlRigEditMode* ControlRigEditMode = static_cast<FControlRigEditMode*>(GLevelEditorModeTools().GetActiveMode(FControlRigEditMode::ModeName));
						if (!ControlRigEditMode)
						{
							GLevelEditorModeTools().ActivateMode(FControlRigEditMode::ModeName);
							ControlRigEditMode = static_cast<FControlRigEditMode*>(GLevelEditorModeTools().GetActiveMode(FControlRigEditMode::ModeName));
						}
						if (ControlRigEditMode)
						{
							ControlRigEditMode->AddControlRigObject(ControlRig, SequencerPtr);
						}

						//create soft links to each other
						if (IInterface_AssetUserData* AssetUserDataInterface = Cast< IInterface_AssetUserData >(LevelSequence))
						{
							ULevelSequenceAnimSequenceLink* LevelAnimLink = NewObject<ULevelSequenceAnimSequenceLink>(LevelSequence, NAME_None, RF_Public | RF_Transactional);
							FLevelSequenceAnimSequenceLinkItem LevelAnimLinkItem;
							LevelAnimLinkItem.SkelTrackGuid = ActorTrackGuid;
							LevelAnimLinkItem.PathToAnimSequence = FSoftObjectPath(AnimSequence);
							LevelAnimLinkItem.bExportMorphTargets = true;
							LevelAnimLinkItem.bExportAttributeCurves = true;
							LevelAnimLinkItem.Interpolation = EAnimInterpolationType::Linear;
							LevelAnimLinkItem.CurveInterpolation = ERichCurveInterpMode::RCIM_Linear;
							LevelAnimLinkItem.bExportMaterialCurves = true;
							LevelAnimLinkItem.bExportTransforms = true;
							LevelAnimLinkItem.bRecordInWorldSpace = false;
							LevelAnimLinkItem.bEvaluateAllSkeletalMeshComponents = true;
							LevelAnimLink->AnimSequenceLinks.Add(LevelAnimLinkItem);
							AssetUserDataInterface->AddAssetUserData(LevelAnimLink);
						}
						if (IInterface_AssetUserData* AnimAssetUserData = Cast< IInterface_AssetUserData >(AnimSequence))
						{
							UAnimSequenceLevelSequenceLink* AnimLevelLink = AnimAssetUserData->GetAssetUserData< UAnimSequenceLevelSequenceLink >();
							if (!AnimLevelLink)
							{
								AnimLevelLink = NewObject<UAnimSequenceLevelSequenceLink>(AnimSequence, NAME_None, RF_Public | RF_Transactional);
								AnimAssetUserData->AddAssetUserData(AnimLevelLink);
							}
							AnimLevelLink->SetLevelSequence(LevelSequence);
							AnimLevelLink->SkelTrackGuid = ActorTrackGuid;
						}
					}
				});

				FOnWindowClosed BakeClosedCallback = FOnWindowClosed::CreateLambda([](const TSharedRef<SWindow>&) { });

				BakeToControlRigDialog::GetBakeParams(BakeCallback, BakeClosedCallback);
			}
		}
	}
}

void FControlRigEditorModule::UnLinkLevelSequence(UAnimSequence* AnimSequence)
{
	if (IInterface_AssetUserData* AnimAssetUserData = Cast< IInterface_AssetUserData >(AnimSequence))
	{
		UAnimSequenceLevelSequenceLink* AnimLevelLink = AnimAssetUserData->GetAssetUserData< UAnimSequenceLevelSequenceLink >();
		if (AnimLevelLink)
		{
			ULevelSequence* LevelSequence = AnimLevelLink->ResolveLevelSequence();
			if (LevelSequence)
			{
				if (IInterface_AssetUserData* LevelSequenceUserDataInterface = Cast< IInterface_AssetUserData >(LevelSequence))
				{

					ULevelSequenceAnimSequenceLink* LevelAnimLink = LevelSequenceUserDataInterface->GetAssetUserData< ULevelSequenceAnimSequenceLink >();
					if (LevelAnimLink)
					{
						for (int32 Index = 0; Index < LevelAnimLink->AnimSequenceLinks.Num(); ++Index)
						{
							FLevelSequenceAnimSequenceLinkItem& LevelAnimLinkItem = LevelAnimLink->AnimSequenceLinks[Index];
							if (LevelAnimLinkItem.ResolveAnimSequence() == AnimSequence)
							{
								LevelAnimLink->AnimSequenceLinks.RemoveAtSwap(Index);
							
								const FText NotificationText = FText::Format(LOCTEXT("UnlinkLevelSequenceSuccess", "{0} unlinked from "), FText::FromString(AnimSequence->GetName()));
								FNotificationInfo Info(NotificationText);
								Info.ExpireDuration = 5.f;
								Info.Hyperlink = FSimpleDelegate::CreateLambda([=]()
								{
									TArray<UObject*> Assets;
									Assets.Add(LevelSequence);
									GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAssets(Assets);
								});
								Info.HyperlinkText = FText::Format(LOCTEXT("OpenUnlinkedLevelSequenceLink", "{0}"), FText::FromString(LevelSequence->GetName()));
								FSlateNotificationManager::Get().AddNotification(Info)->SetCompletionState(SNotificationItem::CS_Success);
														
								break;
							}
						}
						if (LevelAnimLink->AnimSequenceLinks.Num() <= 0)
						{
							LevelSequenceUserDataInterface->RemoveUserDataOfClass(ULevelSequenceAnimSequenceLink::StaticClass());
						}
					}

				}
			}
			AnimAssetUserData->RemoveUserDataOfClass(UAnimSequenceLevelSequenceLink::StaticClass());
		}
	}
}

void FControlRigEditorModule::OpenLevelSequence(UAnimSequence* AnimSequence) 
{
	if (IInterface_AssetUserData* AnimAssetUserData = Cast< IInterface_AssetUserData >(AnimSequence))
	{
		UAnimSequenceLevelSequenceLink* AnimLevelLink = AnimAssetUserData->GetAssetUserData< UAnimSequenceLevelSequenceLink >();
		if (AnimLevelLink)
		{
			ULevelSequence* LevelSequence = AnimLevelLink->ResolveLevelSequence();
			if (LevelSequence)
			{
				GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(LevelSequence);
			}
		}
	}
}

void FControlRigEditorModule::AddControlRigExtenderToToolMenu(FName InToolMenuName)
{
	FToolMenuOwnerScoped OwnerScoped(this);

	UToolMenu* ToolMenu = UToolMenus::Get()->ExtendMenu(InToolMenuName);

	FToolUIAction UIAction;
	UIAction.IsActionVisibleDelegate.BindLambda([](const FToolMenuContext& Context) -> bool
	{
		if (const UAnimationToolMenuContext* MenuContext = Context.FindContext<UAnimationToolMenuContext>())
		{					
			if (const UAnimationAsset* AnimationAsset = MenuContext->AnimationEditor.Pin()->GetPersonaToolkit()->GetAnimationAsset())
			{
				return AnimationAsset->GetClass() == UAnimSequence::StaticClass();
			}
		}
		return false;
	});
	
	ToolMenu->AddMenuEntry("Sequencer",
		FToolMenuEntry::InitComboButton(
			"EditInSequencer",
			FToolUIActionChoice(UIAction),
			FNewToolMenuChoice(
				FNewToolMenuDelegate::CreateLambda([this](UToolMenu* InNewToolMenu)
				{
					if (UAnimationToolMenuContext* MenuContext = InNewToolMenu->FindContext<UAnimationToolMenuContext>())
					{
						InNewToolMenu->AddMenuEntry(
							"EditInSequencer", 
							FToolMenuEntry::InitWidget(
								"EditInSequencerMenu", 
								GenerateAnimationMenu(MenuContext->AnimationEditor),
								FText::GetEmpty(),
								true, false, true
							)
						);
					}
				})
			),
			LOCTEXT("EditInSequencer", "Edit in Sequencer"),
			LOCTEXT("EditInSequencer_Tooltip", "Edit this Anim Sequence In Sequencer."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Persona.EditInSequencer")
		)
	);
}

static void ExecuteOpenLevelSequence(const FToolMenuContext& InContext)
{
	if (const UContentBrowserAssetContextMenuContext* CBContext = InContext.FindContext<UContentBrowserAssetContextMenuContext>())
	{
		if (UAnimSequence* AnimSequence = CBContext->LoadFirstSelectedObject<UAnimSequence>())
        {
       		FControlRigEditorModule::OpenLevelSequence(AnimSequence);
        }
	}
}

static bool CanExecuteOpenLevelSequence(const FToolMenuContext& InContext)
{
	if (const UContentBrowserAssetContextMenuContext* CBContext = InContext.FindContext<UContentBrowserAssetContextMenuContext>())
	{
		if (CBContext->SelectedAssets.Num() != 1)
		{
			return false;
		}

		const FAssetData& SelectedAnimSequence = CBContext->SelectedAssets[0];

		FString PathToLevelSequence;
		if (SelectedAnimSequence.GetTagValue<FString>(GET_MEMBER_NAME_CHECKED(UAnimSequenceLevelSequenceLink, PathToLevelSequence), PathToLevelSequence))
		{
			if (!FSoftObjectPath(PathToLevelSequence).IsNull())
			{
				return true;
			}
		}
	}

	return false;
}

void FControlRigEditorModule::ExtendAnimSequenceMenu()
{
	UToolMenu* Menu = UE::ContentBrowser::ExtendToolMenu_AssetContextMenu(UAnimSequence::StaticClass());

	FToolMenuSection& Section = Menu->FindOrAddSection("GetAssetActions");
	Section.AddDynamicEntry(NAME_None, FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
		{
			{
				const TAttribute<FText> Label = LOCTEXT("OpenLevelSequence", "Open Level Sequence");
				const TAttribute<FText> ToolTip =LOCTEXT("CreateControlRig_ToolTip", "Opens a Level Sequence if it is driving this Anim Sequence.");
				const FSlateIcon Icon = FSlateIcon(FAppStyle::GetAppStyleSetName(), "GenericCurveEditor.TabIcon");

				FToolUIAction UIAction;
				UIAction.ExecuteAction = FToolMenuExecuteAction::CreateStatic(&ExecuteOpenLevelSequence);
				UIAction.CanExecuteAction = FToolMenuCanExecuteAction::CreateStatic(&CanExecuteOpenLevelSequence);
				InSection.AddMenuEntry("OpenLevelSequence", Label, ToolTip, Icon, UIAction);
			}
		}));
}

TSharedRef<IControlRigEditor> FControlRigEditorModule::CreateControlRigEditor(const EToolkitMode::Type Mode, const TSharedPtr< IToolkitHost >& InitToolkitHost, class UControlRigBlueprint* InBlueprint)
{

#if WITH_RIGVMLEGACYEDITOR
	if (CVarRigVMUseDualEditor.GetValueOnAnyThread())
	{
		TSharedRef< FControlRigLegacyEditor > LegacyRigVMExampleEditor(new FControlRigLegacyEditor());
		LegacyRigVMExampleEditor->InitRigVMEditor(Mode, InitToolkitHost, InBlueprint);

		TSharedRef< FControlRigEditor > NewControlRigEditor(new FControlRigEditor());
		NewControlRigEditor->InitRigVMEditor(Mode, InitToolkitHost, InBlueprint);

		return NewControlRigEditor;
	}
	
	if(CVarRigVMUseNewEditor.GetValueOnAnyThread())
	{
		TSharedRef< FControlRigEditor > NewControlRigEditor(new FControlRigEditor());
		NewControlRigEditor->InitRigVMEditor(Mode, InitToolkitHost, InBlueprint);
		return NewControlRigEditor;
	}

	TSharedRef< FControlRigLegacyEditor > LegacyRigVMExampleEditor(new FControlRigLegacyEditor());
	LegacyRigVMExampleEditor->InitRigVMEditor(Mode, InitToolkitHost, InBlueprint);
	return LegacyRigVMExampleEditor;

#else
	
	TSharedRef< FControlRigEditor > NewControlRigEditor(new FControlRigEditor());
	NewControlRigEditor->InitRigVMEditor(Mode, InitToolkitHost, InBlueprint);
	return NewControlRigEditor;
#endif
	
}

FControlRigClassFilter::FControlRigClassFilter(bool bInCheckSkeleton, bool bInCheckAnimatable, bool bInCheckInversion, USkeleton* InSkeleton) :	bFilterAssetBySkeleton(bInCheckSkeleton),
	bFilterExposesAnimatableControls(bInCheckAnimatable),
	bFilterInversion(bInCheckInversion),
	Skeleton(InSkeleton),
	AssetRegistry(FModuleManager::GetModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get())
{
}

bool FControlRigClassFilter::MatchesFilter(const FAssetData& AssetData)
{
	const bool bExposesAnimatableControls = AssetData.GetTagValueRef<bool>(TEXT("bExposesAnimatableControls"));
	if (bFilterExposesAnimatableControls == true && bExposesAnimatableControls == false)
	{
		return false;
	}
	if (bFilterInversion)
	{		
		FAssetDataTagMapSharedView::FFindTagResult Tag = AssetData.TagsAndValues.FindTag(TEXT("SupportedEventNames"));
		if (Tag.IsSet())
		{
			bool bHasInversion = false;
			FString EventString = FRigUnit_InverseExecution::EventName.ToString();
			FString OldEventString = FString(TEXT("Inverse"));
			TArray<FString> SupportedEventNames;
			Tag.GetValue().ParseIntoArray(SupportedEventNames, TEXT(","), true);

			for (const FString& Name : SupportedEventNames)
			{
				if (Name.Contains(EventString) || Name.Contains(OldEventString))
				{
					bHasInversion = true;
					break;
				}
			}
			if (bHasInversion == false)
			{
				return false;
			}
		}
	}
	if (bFilterAssetBySkeleton)
	{
		FString SkeletonName;
		if (Skeleton)
		{
			SkeletonName = FAssetData(Skeleton).GetExportTextName();
		}
		FString PreviewSkeletalMesh = AssetData.GetTagValueRef<FString>(TEXT("PreviewSkeletalMesh"));
		if (PreviewSkeletalMesh.Len() > 0)
		{
			FAssetData SkelMeshData = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(PreviewSkeletalMesh));
			FString PreviewSkeleton = SkelMeshData.GetTagValueRef<FString>(TEXT("Skeleton"));
			if (PreviewSkeleton == SkeletonName)
			{
				return true;
			}
			else if(Skeleton)
			{
				if (Skeleton->IsCompatibleForEditor(PreviewSkeleton))
				{
					return true;
				}
			}
		}
		FString PreviewSkeleton = AssetData.GetTagValueRef<FString>(TEXT("PreviewSkeleton"));
		if (PreviewSkeleton == SkeletonName)
		{
			return true;
		}
		else if (Skeleton)
		{
			if (Skeleton->IsCompatibleForEditor(PreviewSkeleton))
			{
				return true;
			}
		}
		FString SourceHierarchyImport = AssetData.GetTagValueRef<FString>(TEXT("SourceHierarchyImport"));
		if (SourceHierarchyImport == SkeletonName)
		{
			return true;
		}
		else if (Skeleton)
		{
			if (Skeleton->IsCompatibleForEditor(SourceHierarchyImport))
			{
				return true;
			}
		}
		FString SourceCurveImport = AssetData.GetTagValueRef<FString>(TEXT("SourceCurveImport"));
		if (SourceCurveImport == SkeletonName)
		{
			return true;
		}
		else if (Skeleton)
		{
			if (Skeleton->IsCompatibleForEditor(SourceCurveImport))
			{
				return true;
			}
		}
		return false;
	}
	return true;
}

bool FControlRigClassFilter::IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs) 
{
	if(InClass)
	{
		const bool bChildOfObjectClass = InClass->IsChildOf(UControlRig::StaticClass());
		const bool bMatchesFlags = !InClass->HasAnyClassFlags(CLASS_Hidden | CLASS_HideDropDown | CLASS_Deprecated | CLASS_Abstract);
		const bool bNotNative = !InClass->IsNative();

		// Allow any class contained in the extra picker common classes array
		if (InInitOptions.ExtraPickerCommonClasses.Contains(InClass))
		{
			return true;
		}
			
		if (bChildOfObjectClass && bMatchesFlags && bNotNative)
		{
			const FAssetData AssetData(InClass);
			return MatchesFilter(AssetData);
		}
	}
	return false;
}

bool FControlRigClassFilter::IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const TSharedRef< const IUnloadedBlueprintData > InUnloadedClassData, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs)
{
	const bool bChildOfObjectClass = InUnloadedClassData->IsChildOf(UControlRig::StaticClass());
	const bool bMatchesFlags = !InUnloadedClassData->HasAnyClassFlags(CLASS_Hidden | CLASS_HideDropDown | CLASS_Deprecated | CLASS_Abstract);
	if (bChildOfObjectClass && bMatchesFlags)
	{
		const FString GeneratedClassPathString = InUnloadedClassData->GetClassPathName().ToString();
		const FString BlueprintPath = GeneratedClassPathString.LeftChop(2); // Chop off _C
		const FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(BlueprintPath));
		return MatchesFilter(AssetData);

	}
	return false;
}

IMPLEMENT_MODULE(FControlRigEditorModule, ControlRigEditor)

#undef LOCTEXT_NAMESPACE
