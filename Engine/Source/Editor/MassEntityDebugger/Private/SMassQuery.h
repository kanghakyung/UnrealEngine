// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Widgets/SCompoundWidget.h"

struct FMassDebuggerModel;
struct FMassDebuggerQueryData;

class SMassQuery : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SMassQuery){}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedPtr<FMassDebuggerQueryData> InQueryData, TSharedRef<FMassDebuggerModel> InDebuggerModel);

protected:
	TSharedPtr<FMassDebuggerQueryData> QueryData;
};
