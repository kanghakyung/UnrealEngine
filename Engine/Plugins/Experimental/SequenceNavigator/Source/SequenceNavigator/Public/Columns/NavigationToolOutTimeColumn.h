// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Columns/NavigationToolColumn.h"

namespace UE::SequenceNavigator
{

class INavigationToolView;

class FNavigationToolOutTimeColumn : public FNavigationToolColumn
{
public:
	UE_NAVIGATIONTOOL_INHERITS(FNavigationToolOutTimeColumn, FNavigationToolColumn);

	static FName StaticColumnId() { return TEXT("OutTime"); }

protected:
	//~ Begin INavigationToolColumn
	virtual FName GetColumnId() const override { return StaticColumnId(); }
	SEQUENCENAVIGATOR_API virtual FText GetColumnDisplayNameText() const override;
	SEQUENCENAVIGATOR_API virtual const FSlateBrush* GetIconBrush() const override;
	virtual bool ShouldShowColumnByDefault() const override { return true; }
	virtual float GetFillWidth() const override { return 5.f; }
	SEQUENCENAVIGATOR_API virtual SHeaderRow::FColumn::FArguments ConstructHeaderRowColumn(const TSharedRef<INavigationToolView>& InView
		, const float InFillSize) override;
	SEQUENCENAVIGATOR_API virtual TSharedRef<SWidget> ConstructRowWidget(const FNavigationToolItemRef& InItem
		, const TSharedRef<INavigationToolView>& InView
		, const TSharedRef<SNavigationToolTreeRow>& InRow) override;
	//~ End INavigationToolColumn
};

} // namespace UE::SequenceNavigator
