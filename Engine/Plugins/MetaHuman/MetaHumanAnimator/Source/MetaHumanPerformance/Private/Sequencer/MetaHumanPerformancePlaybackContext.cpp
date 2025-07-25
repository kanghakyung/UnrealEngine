// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanPerformancePlaybackContext.h"
#include "Editor.h"

UObject* FMetaHumanPerformancePlaybackContext::GetPlaybackContext() const
{
	UWorld* Context = WeakCurrentContext.Get();
	if (Context != nullptr)
	{
		return Context;
	}

	Context = ComputePlaybackContext();
	check(Context);
	WeakCurrentContext = Context;
	return Context;
}

UWorld* FMetaHumanPerformancePlaybackContext::ComputePlaybackContext() const
{
	UWorld* EditorWorld = nullptr;

	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (Context.WorldType == EWorldType::PIE)
		{
			UWorld* ThisWorld = Context.World();
			if (ThisWorld)
			{
				EditorWorld = ThisWorld;
				break;
			}
		}
		else if (Context.WorldType == EWorldType::Editor)
		{
			EditorWorld = Context.World();
		}
	}

	check(EditorWorld);
	return EditorWorld;
}
