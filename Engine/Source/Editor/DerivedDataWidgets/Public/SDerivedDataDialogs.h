// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"
#include "Types/SlateEnums.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"

class SWidget;

class DERIVEDDATAWIDGETS_API SDerivedDataRemoteStoreDialog : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SDerivedDataRemoteStoreDialog) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:

	TSharedRef<SWidget> GetGridPanel();

	EActiveTimerReturnType UpdateGridPanels(double InCurrentTime, float InDeltaTime);

	SVerticalBox::FSlot* GridSlot = nullptr;
};

class DERIVEDDATAWIDGETS_API SDerivedDataCacheStatisticsDialog : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SDerivedDataCacheStatisticsDialog) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:

	TSharedRef<SWidget> GetGridPanel();
	
	EActiveTimerReturnType UpdateGridPanels(double InCurrentTime, float InDeltaTime);

	SVerticalBox::FSlot* GridSlot = nullptr;
};

class DERIVEDDATAWIDGETS_API SDerivedDataResourceUsageDialog : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SDerivedDataResourceUsageDialog) {}
	SLATE_END_ARGS()

		void Construct(const FArguments& InArgs);

private:

	TSharedRef<SWidget> GetGridPanel();
	
	EActiveTimerReturnType UpdateGridPanels(double InCurrentTime, float InDeltaTime);

	SVerticalBox::FSlot* GridSlot = nullptr;
};
