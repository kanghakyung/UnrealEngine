// Copyright Epic Games, Inc. All Rights Reserved.

#include "DNAUtils.h"

#include "CoreMinimal.h"
#include "Misc/FileHelper.h"
#include "HAL/LowLevelMemTracker.h"
#include "DNACommon.h"
#include "DNAAsset.h"
#include "DNAReaderAdapter.h"
#include "FMemoryResource.h"
#include "RigLogicMemoryStream.h"

#include "riglogic/RigLogic.h"


dna::DataLayer CalculateDNADataLayerBitmask(EDNADataLayer Layer)
{
	dna::DataLayer mask = static_cast<dna::DataLayer>(Layer);
	if ((Layer & EDNADataLayer::RBFBehavior) == EDNADataLayer::RBFBehavior) {
		mask = mask | dna::DataLayer::JointBehaviorMetadata;
		mask = mask | dna::DataLayer::TwistSwingBehavior;
	}
	return mask;
}

static TSharedPtr<IDNAReader> ReadDNAStream(rl4::ScopedPtr<dna::BinaryStreamReader> DNAStreamReader)
{
	DNAStreamReader->read();
	if (!rl4::Status::isOk())
	{
		UE_LOG(LogDNAReader, Error, TEXT("%s"), ANSI_TO_TCHAR(rl4::Status::get().message));
		return nullptr;
	}
	return MakeShared<FDNAReader<dna::BinaryStreamReader>>(DNAStreamReader.release());
}

TSharedPtr<IDNAReader> ReadDNAFromFile(const FString& Path, EDNADataLayer Layer, uint16_t MaxLOD)
{
	LLM_SCOPE_BYNAME(TEXT("Animation/RigLogic"));
	auto DNAFileStream = rl4::makeScoped<rl4::MemoryMappedFileStream>(TCHAR_TO_UTF8(*Path), rl4::MemoryMappedFileStream::AccessMode::Read, FMemoryResource::Instance());
	auto DNAStreamReader = rl4::makeScoped<dna::BinaryStreamReader>(DNAFileStream.get(), CalculateDNADataLayerBitmask(Layer), dna::UnknownLayerPolicy::Preserve, MaxLOD, FMemoryResource::Instance());
	return ReadDNAStream(MoveTemp(DNAStreamReader));
}

TSharedPtr<IDNAReader> ReadDNAFromFile(const FString& Path, EDNADataLayer Layer, TArrayView<uint16_t> LODs)
{
	LLM_SCOPE_BYNAME(TEXT("Animation/RigLogic"));
	auto DNAFileStream = rl4::makeScoped<rl4::MemoryMappedFileStream>(TCHAR_TO_UTF8(*Path), rl4::MemoryMappedFileStream::AccessMode::Read, FMemoryResource::Instance());
	auto DNAStreamReader = rl4::makeScoped<dna::BinaryStreamReader>(DNAFileStream.get(), CalculateDNADataLayerBitmask(Layer), dna::UnknownLayerPolicy::Preserve, LODs.GetData(), static_cast<uint16>(LODs.Num()), FMemoryResource::Instance());
	return ReadDNAStream(MoveTemp(DNAStreamReader));
}

TSharedPtr<IDNAReader> ReadDNAFromBuffer(TArray<uint8>* DNABuffer, EDNADataLayer Layer, uint16_t MaxLOD)
{
	LLM_SCOPE_BYNAME(TEXT("Animation/RigLogic"));
	FRigLogicMemoryStream DNAMemoryStream(DNABuffer);
	auto DNAStreamReader = rl4::makeScoped<dna::BinaryStreamReader>(&DNAMemoryStream, CalculateDNADataLayerBitmask(Layer), dna::UnknownLayerPolicy::Preserve, MaxLOD, FMemoryResource::Instance());
	return ReadDNAStream(MoveTemp(DNAStreamReader));
}

TSharedPtr<IDNAReader> ReadDNAFromBuffer(TArray<uint8>* DNABuffer, EDNADataLayer Layer, TArrayView<uint16_t> LODs)
{
	LLM_SCOPE_BYNAME(TEXT("Animation/RigLogic"));
	FRigLogicMemoryStream DNAMemoryStream(DNABuffer);
	auto DNAStreamReader = rl4::makeScoped<dna::BinaryStreamReader>(&DNAMemoryStream, CalculateDNADataLayerBitmask(Layer), dna::UnknownLayerPolicy::Preserve, LODs.GetData(), static_cast<uint16>(LODs.Num()), FMemoryResource::Instance());
	return ReadDNAStream(MoveTemp(DNAStreamReader));
}

TArray<uint8> ReadStreamFromDNA(const IDNAReader* Reader, EDNADataLayer Layer)
{
	LLM_SCOPE_BYNAME(TEXT("Animation/RigLogic"));
	TArray<char> DNABuffer;
	auto DeltaDnaStream = rl4::makeScoped<trio::MemoryStream>();
	auto DNAStreamWriter = rl4::makeScoped<dna::BinaryStreamWriter>(DeltaDnaStream.get(), FMemoryResource::Instance());
	DNAStreamWriter->setFrom(Reader->Unwrap(), CalculateDNADataLayerBitmask(Layer), dna::UnknownLayerPolicy::Preserve, FMemoryResource::Instance());
	DNAStreamWriter->write();
	DNABuffer.AddZeroed(DeltaDnaStream->size());
	DeltaDnaStream->read(DNABuffer.GetData(), DeltaDnaStream->size());
	return TArray<uint8>(DNABuffer);
}

void WriteDNAToFile(const IDNAReader* Reader, EDNADataLayer Layer, const FString& Path)
{
	LLM_SCOPE_BYNAME(TEXT("Animation/RigLogic"));
	auto DNAFileStream = rl4::makeScoped<rl4::FileStream>(TCHAR_TO_UTF8(*Path), rl4::FileStream::AccessMode::Write, rl4::FileStream::OpenMode::Binary, FMemoryResource::Instance());
	auto DNAStreamWriter = rl4::makeScoped<dna::BinaryStreamWriter>(DNAFileStream.get(), FMemoryResource::Instance());
	DNAStreamWriter->setFrom(Reader->Unwrap(), CalculateDNADataLayerBitmask(Layer), dna::UnknownLayerPolicy::Preserve, FMemoryResource::Instance());
	DNAStreamWriter->write();
}

TObjectPtr<class UDNAAsset> GetDNAAssetFromFile(const FString& InFilePath, UObject* InOuter, EDNADataLayer InLayer)
{
	UDNAAsset* DNAAsset = nullptr;
	TArray<uint8> DNADataAsBuffer;
	if (FFileHelper::LoadFileToArray(DNADataAsBuffer, *InFilePath))
	{
		if (TSharedPtr<IDNAReader> DNAReader = ReadDNAFromBuffer(&DNADataAsBuffer, InLayer))
		{
			DNAAsset = NewObject<UDNAAsset>(InOuter);
			DNAAsset->SetBehaviorReader(DNAReader);
			DNAAsset->SetGeometryReader(DNAReader);
		}
	}

	return DNAAsset;
}
