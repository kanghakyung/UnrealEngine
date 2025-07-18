// Copyright Epic Games, Inc. All Rights Reserved. 

#pragma once

#include "InterchangeFactoryBase.h"

class AActor;

class UInterchangeActorFactoryNode;
class UInterchangeAssetUserData;
class UInterchangeBaseNodeContainer;
class UInterchangeFactoryBaseNode;
class UInterchangeMeshActorFactoryNode;
class UInterchangeSceneImportAsset;

class UMeshComponent;
class UWorld;

namespace UE::Interchange::ActorHelper
{
	/**
	 * Returns the parent actor of the actor described by the FactoryNode using the node hierarchy.
	 */
	INTERCHANGEIMPORT_API AActor* GetSpawnedParentActor(const UInterchangeBaseNodeContainer* NodeContainer, const UInterchangeActorFactoryNode* FactoryNode);

	/**
	 * Spawns and return a AAActor using the given SceneObjectsParam.
	 * The actor's parent will be determine by the node hierarchy and its type by the factory node's ObjectClass.
	 */
	INTERCHANGEIMPORT_API AActor* SpawnFactoryActor(const UInterchangeFactoryBase::FImportSceneObjectsParams& CreateSceneObjectsParams);
	
	/**
	 * Returns the factory node of the asset instanced by ActorFactoryNode.
	 */
	INTERCHANGEIMPORT_API const UInterchangeFactoryBaseNode* FindAssetInstanceFactoryNode(const UInterchangeBaseNodeContainer* NodeContainer, const UInterchangeFactoryBaseNode* ActorFactoryNode);

	/**
	 * Applies material slot dependencies stored in ActorFactoryNode to MeshComponent.
	 */
	INTERCHANGEIMPORT_API void ApplySlotMaterialDependencies(const UInterchangeBaseNodeContainer& NodeContainer, const UInterchangeMeshActorFactoryNode& ActorFactoryNode, UMeshComponent& MeshComponent);

	/**
	 * Applies custom attributes from to ObjectToUpdate based on content of CreateSceneObjectsParams
	 * Reimport policies will be applied if applicable
	 */
	INTERCHANGEIMPORT_API void ApplyAllCustomAttributes(const UInterchangeFactoryBase::FImportSceneObjectsParams& CreateSceneObjectsParams, UObject& ObjectToUpdate);

	/**
	 * Adds metadata to Actor via UInterchangeAssetUserData
	 */
	INTERCHANGEIMPORT_API void AddInterchangeAssetUserDataToActor(AActor* Actor, const UInterchangeSceneImportAsset* SceneImportAsset, const UInterchangeFactoryBaseNode* FactoryNode);

	/**
	 * Adds metadata to Actor via UInterchangeAssetUserData
	 */
	INTERCHANGEIMPORT_API void AddInterchangeLevelAssetUserDataToWorld(UWorld* World, const UInterchangeSceneImportAsset* SceneImportAsset);
}
