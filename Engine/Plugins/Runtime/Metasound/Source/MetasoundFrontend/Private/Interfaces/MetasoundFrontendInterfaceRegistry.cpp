// Copyright Epic Games, Inc. All Rights Reserved.
#include "Interfaces/MetasoundFrontendInterfaceRegistry.h"

#include "AudioParameter.h"
#include "HAL/PlatformTime.h"
#include "Interfaces/MetasoundFrontendInterfaceBindingRegistry.h"
#include "MetasoundFrontendInterfaceRegistryPrivate.h"
#include "MetasoundFrontendRegistryTransaction.h"
#include "MetasoundTrace.h"


#ifndef UE_METASOUND_ENABLE_INTERFACE_VALIDATION
	#define UE_METASOUND_ENABLE_INTERFACE_VALIDATION 0
#endif // !define UE_METASOUND_ENABLE_INTERFACE_VALIDATION

namespace Metasound::Frontend
{
	FInterfaceRegistry::FInterfaceRegistry()
	: TransactionBuffer(MakeShared<TTransactionBuffer<FInterfaceRegistryTransaction>>())
	{
	}

	bool FInterfaceRegistry::RegisterInterface(TUniquePtr<IInterfaceRegistryEntry>&& InEntry)
	{
		METASOUND_LLM_SCOPE;

		FInterfaceRegistryTransaction::FTimeType TransactionTime = FPlatformTime::Cycles64();
		if (InEntry.IsValid())
		{
			const FInterfaceRegistryKey Key = GetInterfaceRegistryKey(InEntry->GetInterface());
			if (IsValidInterfaceRegistryKey(Key))
			{
				if (const IInterfaceRegistryEntry* Entry = FindInterfaceRegistryEntry(Key))
				{
					UE_LOG(LogMetaSound, Warning, TEXT("Registration of interface overwriting previously registered interface [RegistryKey: %s]"), *Key);
						
					FInterfaceRegistryTransaction Transaction{FInterfaceRegistryTransaction::ETransactionType::InterfaceUnregistration, Key, Entry->GetInterface().Metadata.Version, TransactionTime};
					TransactionBuffer->AddTransaction(MoveTemp(Transaction));
				}

#if UE_METASOUND_ENABLE_INTERFACE_VALIDATION
				// Don't run vertex name validation warning for deprecated registry entries. Some of these may be
				// versioning schema that have a subsequent version or versions fixing the very problem this log is reporting.
				if (!InEntry->IsDeprecated())
				{
					const FName InterfaceNamespace = InEntry->GetInterface().Version.Name;
					auto LogIfMismatch = [this, &InterfaceNamespace](FName VertexName)
					{
						FName VertexNamespace;
						if (!IsInterfaceVertexNameValid(InterfaceNamespace, VertexName, &VertexNamespace))
						{
							UE_LOG(LogMetaSound, Warning, TEXT("Interface '%s' contains vertex '%s' with mismatched namespace '%s': "
								"All interface-defined vertices' must start with matching interface namespace (See AUDIO_PARAMETER_INTERFACE_MEMBER_DEFINE/AUDIO_PARAMETER_INTERFACE_NAMESPACE macro to ensure convention is followed). "
								"Failing to fix relationship via interface versioning will fail validation/cook in future builds."), *InterfaceNamespace.ToString(), *VertexName.ToString(), *VertexNamespace.ToString());
						}
					};

					for (const FMetasoundFrontendClassVertex& Vertex : InEntry->GetInterface().Inputs)
					{
						LogIfMismatch(Vertex.Name);
					}

					for (const FMetasoundFrontendClassVertex& Vertex : InEntry->GetInterface().Outputs)
					{
						LogIfMismatch(Vertex.Name);
					}
				}
#endif // UE_METASOUND_ENABLE_INTERFACE_VALIDATION

				FInterfaceRegistryTransaction Transaction{FInterfaceRegistryTransaction::ETransactionType::InterfaceRegistration, Key, InEntry->GetInterface().Metadata.Version, TransactionTime};
				TransactionBuffer->AddTransaction(MoveTemp(Transaction));
				Entries.Add(Key, MoveTemp(InEntry));
				return true;
			}
		}

		return false;
	}

	const IInterfaceRegistryEntry* FInterfaceRegistry::FindInterfaceRegistryEntry(const FInterfaceRegistryKey& InKey) const
	{
		if (const TUniquePtr<IInterfaceRegistryEntry>* Entry = Entries.Find(InKey))
		{
			return Entry->Get();
		}
		return nullptr;
	}

	bool FInterfaceRegistry::FindInterface(const FInterfaceRegistryKey& InKey, FMetasoundFrontendInterface& OutInterface) const
	{
		if (const IInterfaceRegistryEntry* Entry = FindInterfaceRegistryEntry(InKey))
		{
			OutInterface = Entry->GetInterface();
			return true;
		}

		return false;
	}

	TUniquePtr<FInterfaceTransactionStream> FInterfaceRegistry::CreateTransactionStream()
	{
		return MakeUnique<FInterfaceTransactionStream>(TransactionBuffer);
	}

	bool IsValidInterfaceRegistryKey(const FInterfaceRegistryKey& InKey)
	{
		return !InKey.IsEmpty();
	}

	FInterfaceRegistryKey GetInterfaceRegistryKey(const FMetasoundFrontendVersion& InInterfaceVersion)
	{
		return FString::Format(TEXT("{0}_{1}.{2}"), { InInterfaceVersion.Name.ToString(), InInterfaceVersion.Number.Major, InInterfaceVersion.Number.Minor });

	}

	FInterfaceRegistryKey GetInterfaceRegistryKey(const FMetasoundFrontendInterface& InInterface)
	{
		return GetInterfaceRegistryKey(InInterface.Metadata.Version);
	}

	FInterfaceRegistryTransaction::FInterfaceRegistryTransaction(ETransactionType InType, const FInterfaceRegistryKey& InKey, const FMetasoundFrontendVersion& InInterfaceVersion, FInterfaceRegistryTransaction::FTimeType InTimestamp)
	: Type(InType)
	, Key(InKey)
	, InterfaceVersion(InInterfaceVersion)
	, Timestamp(InTimestamp)
	{
	}

	FInterfaceRegistryTransaction::ETransactionType FInterfaceRegistryTransaction::GetTransactionType() const
	{
		return Type;
	}

	const FMetasoundFrontendVersion& FInterfaceRegistryTransaction::GetInterfaceVersion() const
	{
		return InterfaceVersion;
	}

	const FInterfaceRegistryKey& FInterfaceRegistryTransaction::GetInterfaceRegistryKey() const
	{
		return Key;
	}

	FInterfaceRegistryTransaction::FTimeType FInterfaceRegistryTransaction::GetTimestamp() const
	{
		return Timestamp;
	}

	FInterfaceRegistry& FInterfaceRegistry::Get()
	{
		static FInterfaceRegistry Registry;
		return Registry;
	}

	bool FInterfaceRegistry::IsInterfaceVertexNameValid(FName InterfaceNamespace, FName FullVertexName, FName* VertexNamespace) const
	{
		FName Namespace;
		FName Name;
		Audio::FParameterPath::SplitName(FullVertexName, Namespace, Name);
		if (VertexNamespace)
		{
			*VertexNamespace = Namespace;
		}
		return InterfaceNamespace == Namespace;
	}

	IInterfaceRegistry& IInterfaceRegistry::Get()
	{
		return FInterfaceRegistry::Get();
	}
} // namespace Metasound::Frontend
