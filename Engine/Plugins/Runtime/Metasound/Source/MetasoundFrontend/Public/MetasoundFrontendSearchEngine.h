// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once


#include "Interfaces/MetasoundFrontendInterfaceRegistry.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundNodeInterface.h"

#define UE_API METASOUNDFRONTEND_API


namespace Metasound::Frontend
{
	/** Interface for frontend search engine. A frontend search engine provides
		* a simple interface for common frontend queries. It also serves as an
		* opportunity to cache queries to reduce CPU load. 
		*/
	class ISearchEngine
	{
		public:
			/** Return an instance of a search engine. */
			static UE_API ISearchEngine& Get();

			virtual ~ISearchEngine() = default;

			/** Updates internal state to speed up queries. */
			virtual void Prime() = 0;

			/** Find the class with the given ClassName & Major Version. Returns false if not found, true if found. */
			virtual bool FindClassWithHighestMinorVersion(const FMetasoundFrontendClassName& InName, int32 InMajorVersion, FMetasoundFrontendClass& OutClass) = 0;

			/** Finds all registered interfaces with the given name. */
			virtual TArray<FMetasoundFrontendVersion> FindAllRegisteredInterfacesWithName(FName InInterfaceName) = 0;

			/** Finds the registered interface with the highest version of the given name. Returns true if found, false if not. */
			virtual bool FindInterfaceWithHighestVersion(FName InInterfaceName, FMetasoundFrontendInterface& OutInterface) = 0;

			UE_DEPRECATED(5.3, "Use ISearchEngine::FindUClassDefaultInterfaceVersions using TopLevelAssetPath instead.")
			virtual TArray<FMetasoundFrontendInterface> FindUClassDefaultInterfaces(FName InUClassName) = 0;

			/** Returns all interfaces that are to be added to a given document when it is initialized on an object with the given class**/
			virtual TArray<FMetasoundFrontendVersion> FindUClassDefaultInterfaceVersions(const FTopLevelAssetPath& InUClassPath) = 0;

#if WITH_EDITORONLY_DATA
			/** Find all FMetasoundFrontendClasses.
				* (Optional) Include all versions (i.e. deprecated classes and versions of classes that are not the highest major version).
				*/
			virtual TArray<FMetasoundFrontendClass> FindAllClasses(bool bInIncludeAllVersions) = 0;

			/** Find all classes with the given ClassName.
				* (Optional) Sort matches based on version.
				*/
			virtual TArray<FMetasoundFrontendClass> FindClassesWithName(const FMetasoundFrontendClassName& InName, bool bInSortByVersion) = 0;

			/** Find the highest version of a class with the given ClassName. Returns false if not found, true if found. */
			virtual bool FindClassWithHighestVersion(const FMetasoundFrontendClassName& InName, FMetasoundFrontendClass& OutClass) = 0;

			/** Returns array with all registered interfaces. Optionally, include interface versions that are not the highest version. */
			virtual TArray<FMetasoundFrontendInterface> FindAllInterfaces(bool bInIncludeAllVersions = false) = 0;
#endif // WITH_EDITORONLY_DATA

		protected:
			ISearchEngine() = default;
	};
} // namespace Metasound::Frontend

#undef UE_API
