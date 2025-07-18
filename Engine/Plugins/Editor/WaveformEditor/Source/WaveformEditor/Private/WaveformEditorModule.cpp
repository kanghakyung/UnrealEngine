// Copyright Epic Games, Inc. All Rights Reserved.

#include "WaveformEditorModule.h"

#include "Editor.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "TransformedWaveformViewFactory.h"
#include "WaveformEditorCommands.h"
#include "WaveformEditorInstantiator.h"
#include "WaveformEditorLog.h"

DEFINE_LOG_CATEGORY(LogWaveformEditor);


void FWaveformEditorModule::StartupModule()
{
	FWaveformEditorCommands::Register();
	FTransformedWaveformViewFactory::Create();

	WaveformEditorInstantiator = MakeShared<FWaveformEditorInstantiator>();
	RegisterContentBrowserExtensions(WaveformEditorInstantiator.Get());

	// TODO: Re register WaveformEditor as default soundwave editor with: WaveformEditorInstantiator->RegisterAsSoundwaveEditor();
}

void FWaveformEditorModule::ShutdownModule()
{
	FWaveformEditorCommands::Unregister();
}

void FWaveformEditorModule::RegisterContentBrowserExtensions(IWaveformEditorInstantiator* Instantiator)
{
	WaveformEditorInstantiator->ExtendContentBrowserSelectionMenu();
}

IMPLEMENT_MODULE(FWaveformEditorModule, WaveformEditor);
