// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SearchQuery.h"

class ISearchProvider
{
public:
	virtual ~ISearchProvider() { }
	virtual void Search(FSearchQueryPtr SearchQuery) = 0;
};
