// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GenericPlatform/ICursor.h"
#include "Math/IntRect.h"

class FAndroidCursor : public ICursor
{
public:

	FAndroidCursor();

	virtual ~FAndroidCursor()
	{
	}

	virtual FVector2D GetPosition() const override
	{
		return CurrentPosition;
	}

	virtual void SetPosition(const int32 X, const int32 Y) override;

	virtual void SetType(const EMouseCursor::Type InNewCursor) override;

	virtual EMouseCursor::Type GetType() const override
	{
		return CurrentType;
	}

	virtual void GetSize(int32& Width, int32& Height) const override;

	virtual void Show(bool bInShow) override;

	virtual void Lock(const RECT* const Bounds) override;

	virtual void SetTypeShape(EMouseCursor::Type InCursorType, void* CursorHandle) override { }

private:
	bool UpdateCursorClipping(FVector2D& CursorPosition);

	EMouseCursor::Type CurrentType;
	FVector2D CurrentPosition;
	FIntRect CursorClipRect;
	bool bShow;
};
