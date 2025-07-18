// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class IControlRigBaseEditor;
class UControlRigBlueprint;
class UControlRig;

class SAnimAttributeView;


class SControlRigAnimAttributeView : public SCompoundWidget
{
	SLATE_BEGIN_ARGS( SControlRigAnimAttributeView )
	{}
	
	SLATE_END_ARGS()

	~SControlRigAnimAttributeView();
	
	void Construct( const FArguments& InArgs, TSharedRef<IControlRigBaseEditor> InControlRigEditor);

	void HandleSetObjectBeingDebugged(UObject* InObject);
	
	void StartObservingNewControlRig(UControlRig* InControlRig);
	void StopObservingCurrentControlRig();

	void HandleControlRigPostForwardSolve(UControlRig* InControlRig, const FName& InEventName) const;
	
	TWeakPtr<IControlRigBaseEditor> ControlRigEditor;
	TWeakObjectPtr<UControlRigBlueprint> ControlRigBlueprint;
	TWeakObjectPtr<UControlRig> ControlRigBeingDebuggedPtr;
	TSharedPtr<SAnimAttributeView> AttributeView;
};
