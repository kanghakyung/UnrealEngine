// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SGraphNodeComment.h"

class UEdGraphNode_Comment;

class RIGVMEDITOR_API SRigVMGraphNodeComment : public SGraphNodeComment
{
public:

	SRigVMGraphNodeComment();

	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void EndUserInteraction() const override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual void MoveTo(const FVector2f& NewPosition, FNodeSet& NodeFilter, bool bMarkDirty = true) override;

protected:

	virtual bool IsNodeUnderComment(UEdGraphNode_Comment* InCommentNode, const TSharedRef<SGraphNode> InNodeWidget) const override;
	bool IsNodeUnderComment(UEdGraphNode* InNode) const;
	bool IsNodeUnderComment(const FVector2f& InNodePosition) const;
	bool IsNodeUnderComment(const FVector2f& InCommentPosition, const FVector2f& InNodePosition) const;

private:

	FLinearColor CachedNodeCommentColor;

	int8 CachedColorBubble;
};
