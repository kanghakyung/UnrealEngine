// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "CoreMinimal.h"
#include "HAL/Platform.h"
#include "Math/Color.h"
#include "Misc/Optional.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "UObject/SoftObjectPtr.h"
#include "UObject/UObjectGlobals.h"

#include "CurveEditorSettings.generated.h"

class UClass;

/** Defines visibility states for the tangents in the curve editor. */
UENUM()
enum class ECurveEditorTangentVisibility : uint8
{
	/** All tangents should be visible. */
	AllTangents,
	/** Only tangents from selected keys should be visible. */
	SelectedKeys,
	/** Don't display tangents. */
	NoTangents
};

/** Defines the position to center the zoom about in the curve editor. */
UENUM()
enum class ECurveEditorZoomPosition : uint8
{
	/** Playhead. */
	CurrentTime UMETA(DisplayName = "Playhead"),

	/** Mouse Position. */
	MousePosition UMETA(DisplayName = "Mouse Position"),
};

/** Defines the axis to snap to when dragging. */
UENUM()
enum class ECurveEditorSnapAxis : uint8
{
	/** Don't snap to any axis when dragging. */
	CESA_None UMETA(DisplayName = "None"),
	/* Snap to the x axis when dragging. */
	CESA_X UMETA(DisplayName = "X Only"),
	/* Snap to the y axis when dragging. */
	CESA_Y UMETA(DisplayName = "Y Only")
};

/** Custom Color Object*/
USTRUCT()
struct FCustomColorForChannel
{
	GENERATED_BODY()

	FCustomColorForChannel() : Object(nullptr), Color(1.0f, 1.0f, 1.0f, 1.0f) {};

	UPROPERTY(EditAnywhere, Category = "Custom Colors")
	TSoftClassPtr<UObject>	Object;
	UPROPERTY(EditAnywhere, Category = "Custom Colors")
	FString PropertyName;
	UPROPERTY(EditAnywhere, Category = "Custom Colors")
	FLinearColor Color;


};

/** Custom Color Object*/
USTRUCT()
struct FCustomColorForSpaceSwitch
{
	GENERATED_BODY()

	FCustomColorForSpaceSwitch() : Color(1.0f, 1.0f, 1.0f, 1.0f) {};

	UPROPERTY(EditAnywhere, Category = "Space Switch Colors")
	FString ControlName;
	UPROPERTY(EditAnywhere, Category = "Space Switch Colors")
	FLinearColor Color;

};

/** Serializable options for curve editor. */
UCLASS(config=EditorPerProjectUserSettings)
class CURVEEDITOR_API UCurveEditorSettings : public UObject
{
public:
	GENERATED_BODY()

	DECLARE_MULTICAST_DELEGATE(FOnCustomColorsChanged);
	DECLARE_MULTICAST_DELEGATE(FOnAxisSnappingChanged);
	DECLARE_MULTICAST_DELEGATE(FOnShowValueIndicatorsChanged);

	UCurveEditorSettings();

	/** Gets whether or not the curve editor auto frames the selected curves. */
	bool GetAutoFrameCurveEditor() const;
	/** Sets whether or not the curve editor auto frames the selected curves. */
	void SetAutoFrameCurveEditor(bool InbAutoFrameCurveEditor);

	/** Gets whether or not the curve editor shows key bar style curves, like for constraints and spaces. */
	bool GetShowBars() const;
	/** Sets whether or not the curve editor shows key bar style curves, like for constraints and spaces. */
	void SetShowBars(bool InShowBars);

	/** Gets the number of pixels to pad input framing */
	int32 GetFrameInputPadding() const;
	/** Sets the number of pixels to pad input framing */
	void SetFrameInputPadding(int32 InFrameInputPadding);

	/** Gets the number of pixels to pad output framing */
	int32 GetFrameOutputPadding() const;
	/** Sets the number of pixels to pad output framing */
	void SetFrameOutputPadding(int32 InFrameOutputPadding);

	/** Gets whether or not to show buffered curves in the curve editor. */
	bool GetShowBufferedCurves() const;
	/** Sets whether or not to show buffered curves in the curve editor. */
	void SetShowBufferedCurves(bool InbShowBufferedCurves);

	/** Gets whether or not to show curve tool tips in the curve editor. */
	bool GetShowCurveEditorCurveToolTips() const;
	/** Sets whether or not to show curve tool tips in the curve editor. */
	void SetShowCurveEditorCurveToolTips(bool InbShowCurveEditorCurveToolTips);

	/** Gets the current tangent visibility. */
	ECurveEditorTangentVisibility GetTangentVisibility() const;
	/** Sets the current tangent visibility. */
	void SetTangentVisibility(ECurveEditorTangentVisibility InTangentVisibility);

	/** Get zoom in/out position (mouse position or current time). */
	ECurveEditorZoomPosition GetZoomPosition() const;
	/** Set zoom in/out position (mouse position or current time). */
	void SetZoomPosition(ECurveEditorZoomPosition InZoomPosition);

	/** Get snap axis. */
	ECurveEditorSnapAxis GetSnapAxis() const;
	/** Set snap axis. */
	void SetSnapAxis(ECurveEditorSnapAxis InSnapAxis);

	/** Get whether to snap the time to the currently selected key. */
	bool GetSnapTimeToSelection() const;
	/** Set whether to snap the time to the currently selected key. */
	void SetSnapTimeToSelection(bool bInSnapTimeToSelection);

	/** Set the selection color. */
	void SetSelectionColor(const FLinearColor& InColor);
	/** Get the selection color. */
	FLinearColor GetSelectionColor() const;

	/** Get custom color for object and property if it exists, if it doesn't the optional won't be set */
	TOptional<FLinearColor> GetCustomColor(UClass* InClass, const FString& InPropertyName) const;
	/** Set Custom Color for the specified parameters. */
	void SetCustomColor(UClass* InClass, const FString& InPropertyName, FLinearColor InColor);
	/** Delete Custom Color for the specified parameters. */
	void DeleteCustomColor(UClass* InClass, const FString& InPropertyName);
	
	/** Gets the multicast delegate which is run whenever custom colors have changed. */
	FOnCustomColorsChanged& GetOnCustomColorsChanged() { return OnCustomColorsChangedEvent; }
	/** Gets the multicast delegate which is run whenever axis snapping has changed. */
	FOnAxisSnappingChanged& GetOnAxisSnappingChanged() { return OnAxisSnappingChangedEvent; }
	/** Gets the multicast delegate which is run whenever showing the value indicator lines has changed. */
	FOnShowValueIndicatorsChanged& OnShowValueIndicatorsChanged() { return OnShowValueIndicatorsChangedEvent; }

	/** Get custom color for space name. Parent and World are reserved names and will be used instead of the specified control name. */
	TOptional<FLinearColor> GetSpaceSwitchColor(const FString& InControlName) const;
	/** Set Custom Space SwitchColor for the specified control name. */
	void SetSpaceSwitchColor(const FString& InControlName, FLinearColor InColor);
	/** Delete Custom Space Switch Color for the specified control name. */
	void DeleteSpaceSwitchColor(const FString& InControlName);

	/** Helper function to get next random linear color*/
	static FLinearColor GetNextRandomColor();

	/** Gets the tree view width percentage */
	float GetTreeViewWidth() const { return TreeViewWidth; }
	/** Sets the tree view width percentage */
	void SetTreeViewWidth(float InTreeViewWidth);

	/** Gets how sensitive the selection marquee should be when selecting points. */
	float GetMarqueePointSensitivity() const { return FMath::Clamp(MarqueePointSensitivity, 0.f, 1.f); }
	/** Sets how sensitive the selection marquee should be when selecting points. */
	void SetMarqueePointSensitivity(float InMarqueePointSensitivity);

	/** @return Whether to draw a value indicator line for the minimum and maximum key in the selected key range. */
	bool GetShowValueIndicators() const { return bShowValueIndicators; }
	/** Sets whether to draw a value indicator line for the minimum and maximum key in the selected key range. */
	void SetShowValueIndicators(bool bValue);

protected:

	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	UPROPERTY( config, EditAnywhere, Category="Curve Editor" )
	bool bAutoFrameCurveEditor;

	UPROPERTY(config, EditAnywhere, Category = "Curve Editor")
	bool bShowBars;

	/* Number of pixels to add as padding in the input axis when framing curve keys */
	UPROPERTY( config, EditAnywhere, Category="Curve Editor", meta=(ClampMin=0) )
	int32 FrameInputPadding;

	/* Number of pixels to add as padding in the output axis when framing curve keys */
	UPROPERTY( config, EditAnywhere, Category="Curve Editor", meta=(ClampMin=0) )
	int32 FrameOutputPadding;

	UPROPERTY( config, EditAnywhere, Category="Curve Editor" )
	bool bShowBufferedCurves;

	UPROPERTY( config, EditAnywhere, Category="Curve Editor" )
	bool bShowCurveEditorCurveToolTips;

	UPROPERTY( config, EditAnywhere, Category="Curve Editor" )
	ECurveEditorTangentVisibility TangentVisibility;

	UPROPERTY(config, EditAnywhere, Category = "Curve Editor")
	ECurveEditorZoomPosition ZoomPosition;

	UPROPERTY(config, EditAnywhere, Category = "Curve Editor")
	ECurveEditorSnapAxis SnapAxis;

	UPROPERTY( config, EditAnywhere, Category="Curve Editor")
	bool bSnapTimeToSelection;

	UPROPERTY(config, EditAnywhere, Category = "Curve Editor")
	FLinearColor SelectionColor;

	UPROPERTY(config, EditAnywhere, Category="Curve Editor")
	TArray<FCustomColorForChannel> CustomColors;

	UPROPERTY(config, EditAnywhere, Category = "Curve Editor")
	FLinearColor ParentSpaceCustomColor;

	UPROPERTY(config, EditAnywhere, Category = "Curve Editor")
	FLinearColor WorldSpaceCustomColor;

	UPROPERTY(config, EditAnywhere, Category = "Curve Editor")
	TArray<FCustomColorForSpaceSwitch> ControlSpaceCustomColors;

	UPROPERTY(config, EditAnywhere, Category = "Curve Editor")
	float TreeViewWidth;

	/**
	 * Determines how close you must move the selection marque to the center of a point in order to select it.
	 * 
	 * This is the percentage of point's center to the point's widget border that must be overlapped with the marquee in order for the point to be selected.
	 * 1.0 means as soon as marquee overlaps any portion of the point.
	 * 0.0 means you must touch the point's center to select it.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Curve Editor", meta = (ClampMin = 0.0, ClampMax = 1.0))
	float MarqueePointSensitivity = 0.45f;

	/** When you select a single curve, whether to draw a dotted line for the minimum and maximum key in the selected key range. */
	UPROPERTY(config, EditAnywhere, Category = "Curve Editor")
	bool bShowValueIndicators = true;

private:
	FOnCustomColorsChanged OnCustomColorsChangedEvent;
	FOnAxisSnappingChanged OnAxisSnappingChangedEvent;
	FOnShowValueIndicatorsChanged OnShowValueIndicatorsChangedEvent;
};
