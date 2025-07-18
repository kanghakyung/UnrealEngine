// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Internationalization/InternationalizationManifest.h"
#include "Templates/SharedPointer.h"
#include "UObject/NameTypes.h"

#define UE_API LOCALIZATION_API

class FInternationalizationManifest;

struct FStructuredEntry
{
public:
	FStructuredEntry( const FString& InNamespace )
		: Namespace( InNamespace )
		, SubNamespaces()
	{
	}

	const FString Namespace;
	TArray< TSharedPtr< FStructuredEntry > > SubNamespaces;
	TArray< TSharedPtr< class FManifestEntry > > ManifestEntries;
};


class FJsonObject;


class FJsonInternationalizationManifestSerializer
{
public:

	/**
	 * Deserializes a Internationalization manifest from a JSON string.
	 *
	 * @param InStr The JSON string to serialize from.
	 * @param Manifest The populated Internationalization manifest.
	 * @return true if deserialization was successful, false otherwise.
	 */
	static UE_API bool DeserializeManifest( const FString& InStr, TSharedRef< FInternationalizationManifest > Manifest, const FName PlatformName = FName() );

	/**
	 * Deserializes a Internationalization manifest from a JSON object.
	 *
	 * @param InJsonObj The JSON object to serialize from.
	 * @param Manifest The populated Internationalization manifest.
	 * @return true if deserialization was successful, false otherwise.
	 */
	static UE_API bool DeserializeManifest( TSharedRef< FJsonObject > InJsonObj, TSharedRef< FInternationalizationManifest > Manifest, const FName PlatformName = FName() );

	/**
	 * Deserializes a Internationalization manifest from a JSON file.
	 *
	 * @param InJsonFile The path to the JSON file to serialize from.
	 * @param Manifest The populated Internationalization manifest.
	 * @return true if deserialization was successful, false otherwise.
	 */
	static UE_API bool DeserializeManifestFromFile( const FString& InJsonFile, TSharedRef< FInternationalizationManifest > Manifest, const FName PlatformName = FName() );

	/**
	 * Serializes a Internationalization manifest to a JSON string.
	 *
	 * @param Manifest The Internationalization manifest data to serialize.
	 * @param Str The string to serialize into.
	 * @return true if serialization was successful, false otherwise.
	 */
	static UE_API bool SerializeManifest( TSharedRef< const FInternationalizationManifest > Manifest, FString& Str );

	/**
	 * Serializes a Internationalization manifest to a JSON object.
	 *
	 * @param Manifest The Internationalization manifest data to serialize.
	 * @param JsonObj The JSON object to serialize into.
	 * @return true if serialization was successful, false otherwise.
	 */
	static UE_API bool SerializeManifest( TSharedRef< const FInternationalizationManifest > Manifest, TSharedRef< FJsonObject > JsonObj );

	/**
	 * Serializes a Internationalization manifest to a JSON file.
	 *
	 * @param Manifest The Internationalization manifest data to serialize.
	 * @param InJsonFile The path to the JSON file to serialize to.
	 * @return true if serialization was successful, false otherwise.
	 */
	static UE_API bool SerializeManifestToFile( TSharedRef< const FInternationalizationManifest > Manifest, const FString& InJsonFile );

protected:

	/**
	 * Convert a JSON object to a Internationalization manifest.
	 *
	 * @param InJsonObj The JSON object to serialize from.
	 * @param Manifest The Internationalization manifest that will store the data.
	 *
	 * @return true if deserialization was successful, false otherwise.
	 */
	static UE_API bool DeserializeInternal( const TSharedRef< FJsonObject > InJsonObj, TSharedRef< FInternationalizationManifest > Manifest, const FName PlatformName );

	/**
	 * Convert a Internationalization manifest to a JSON object.
	 *
	 * @param InManifest The Internationalization manifest object to serialize from.
	 * @param JsonObj The Json object that will store the data.
	 *
	 * @return true if serialization was successful, false otherwise.
	 */
	static UE_API bool SerializeInternal( TSharedRef< const FInternationalizationManifest > InManifest, TSharedRef< FJsonObject > JsonObj );

	/**
	 * Recursive function that will traverse the JSON object and populate a Internationalization manifest.
	 *
	 * @param InJsonObj The JSON object.
	 * @param InNamespace The namespace of the parent JSON object.
	 * @param Manifest The Internationalization manifest that will store the data.
	 *
	 * @return true if successful, false otherwise.
	 */
	static UE_API bool JsonObjToManifest( TSharedRef< FJsonObject > InJsonObj, FString InNamespace, TSharedRef< FInternationalizationManifest > Manifest, const FName PlatformName );

	/**
	 * Takes a Internationalization manifest and arranges the data into a hierarchy based on namespace.
	 *
	 * @param InManifest The Internationalization manifest.
	 * @param RootElement The root element of the structured data.
	 */
	static UE_API void GenerateStructuredData( TSharedRef< const FInternationalizationManifest > InManifest, TSharedPtr< FStructuredEntry > RootElement );

	/**
	 * Goes through the structured, hierarchy based, manifest data and does a non-culture specific sort on namespaces, default text, and key.
	 *
	 * @param RootElement The root element of the structured data.
	 */
	static UE_API void SortStructuredData( TSharedPtr< FStructuredEntry > InElement );

	/**
	 * Populates a JSON object from Internationalization manifest data that has been structured based on namespace.
	 *
	 * @param InElement Internationalization manifest data structured based on namespace.
	 * @param JsonObj JSON object to be populated.
	 */
	static UE_API void StructuredDataToJsonObj( TSharedPtr< const FStructuredEntry> InElement, TSharedRef< FJsonObject > JsonObj );

public:

	static UE_API const FString TAG_FORMATVERSION;
	static UE_API const FString TAG_NAMESPACE;
	static UE_API const FString TAG_CHILDREN;
	static UE_API const FString TAG_SUBNAMESPACES;
	static UE_API const FString TAG_PATH;
	static UE_API const FString TAG_OPTIONAL;
	static UE_API const FString TAG_KEYCOLLECTION;
	static UE_API const FString TAG_KEY;
	static UE_API const FString TAG_DEPRECATED_DEFAULTTEXT;
	static UE_API const FString TAG_SOURCE;
	static UE_API const FString TAG_SOURCE_TEXT;
	static UE_API const FString TAG_PLATFORM_NAME;
	static UE_API const FString TAG_METADATA;
	static UE_API const FString TAG_METADATA_INFO;
	static UE_API const FString TAG_METADATA_KEY;
	static UE_API const FString NAMESPACE_DELIMITER;
};

#undef UE_API
