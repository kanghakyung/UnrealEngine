// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"

class FString;


/**
 * IConnectionBasedMessagingModule interface allows adding/removing of connections.
 */
class ITcpMessagingModule
	: public IModuleInterface
{
public:

	virtual void AddOutgoingConnection(const FString& Endpoint) = 0;
	virtual void RemoveOutgoingConnection(const FString& Endpoint) = 0;
};
