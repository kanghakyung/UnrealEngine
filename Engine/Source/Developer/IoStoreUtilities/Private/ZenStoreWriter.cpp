// Copyright Epic Games, Inc. All Rights Reserved.

#include "ZenStoreWriter.h"

#include "Algo/BinarySearch.h"
#include "Algo/IsSorted.h"
#include "Algo/Sort.h"
#include "AssetRegistry/AssetRegistryState.h"
#include "Async/Async.h"
#include "Containers/Queue.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformFileManager.h"
#include "Interfaces/ITargetPlatform.h"
#include "IO/IoDispatcher.h"
#include "IPAddress.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PathViews.h"
#include "Misc/StringBuilder.h"
#include "PackageStoreOptimizer.h"
#include "Serialization/ArrayReader.h"
#include "Serialization/CompactBinaryContainerSerialization.h"
#include "Serialization/CompactBinaryPackage.h"
#include "Serialization/CompactBinarySerialization.h"
#include "Serialization/CompactBinaryWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/LargeMemoryWriter.h" 
#include "SocketSubsystem.h"
#include "UObject/ICookInfo.h"
#include "UObject/SavePackage.h"
#include "ZenCookArtifactReader.h"
#include "ZenFileSystemManifest.h"
#include "ZenStoreHttpClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogZenStoreWriter, Log, All);

using namespace UE;

static FString GZenStoreWriterProjectIdOverride;
static FAutoConsoleVariableRef CVar_ZenStoreWriterProjectIdOverride(
	TEXT("ZenStoreWriter.ProjectIdOverride"),
	GZenStoreWriterProjectIdOverride,
	TEXT("Overrides the project ID otherwise taken from FApp/-DLCNAME. Format; '[<ParentId>/]ProjectId'")
);

static void GetZenStoreProjectId(FString& ProjectId, FString& ParentId)
{
	ParentId.Reset();

	if (GZenStoreWriterProjectIdOverride.IsEmpty())
	{
		FString DLCName;
		FParse::Value(FCommandLine::Get(), TEXT("DLCNAME="), DLCName);
		DLCName.ToLowerInline();

		ProjectId = FApp::GetZenStoreProjectId(DLCName);
		if (!DLCName.IsEmpty())
		{
			ParentId = FApp::GetZenStoreProjectId();
		}

		return;
	}

	FStringView Override = GZenStoreWriterProjectIdOverride;

	int32 SlashIndex;
	if (!Override.FindChar('/', SlashIndex))
	{
		ProjectId = Override;
		return;
	}

	ProjectId = Override.Left(SlashIndex);
	check(!ProjectId.IsEmpty());

	Override = Override.Mid(SlashIndex + 1);
	check(!Override.IsEmpty());
	ParentId = Override;
}

// Note that this is destructive - we yank out the buffer memory from the 
// IoBuffer into the FSharedBuffer
FSharedBuffer IoBufferToSharedBuffer(FIoBuffer& InBuffer)
{
	InBuffer.EnsureOwned();
	const uint64 DataSize = InBuffer.DataSize();
	uint8* DataPtr = InBuffer.Release().ValueOrDie();
	return FSharedBuffer{ FSharedBuffer::TakeOwnership(DataPtr, DataSize, FMemory::Free) };
};

FCbObjectId ToObjectId(const FIoChunkId& ChunkId)
{
	return FCbObjectId(MakeMemoryView(ChunkId.GetData(), ChunkId.GetSize()));
}

FMD5Hash IoHashToMD5(const FIoHash& IoHash)
{
	const FIoHash::ByteArray& Bytes = IoHash.GetBytes();
	
	FMD5 MD5Gen;
	MD5Gen.Update(Bytes, sizeof(FIoHash::ByteArray));
	
	FMD5Hash Hash;
	Hash.Set(MD5Gen);

	return Hash;
}

FZenStoreWriter::FPackageDataEntry::~FPackageDataEntry()
{

}

FZenStoreWriter::FPendingPackageState::~FPendingPackageState()
{

}

struct FZenStoreWriter::FZenCommitInfo
{
	IPackageWriter::FCommitPackageInfo CommitInfo;
	TUniquePtr<FPendingPackageState> PackageState;
};

class FZenStoreWriter::FCommitQueue
{
public:
	FCommitQueue()
		: NewCommitEvent(EEventMode::AutoReset) {}
	
	void Enqueue(FZenCommitInfo&& Commit)
	{
		bool TriggerEvent = false;
		{
			FScopeLock _(&QueueCriticalSection);
			TriggerEvent = Queue.IsEmpty();
			Queue.Enqueue(MoveTemp(Commit));
			
		}

		if (TriggerEvent)
		{
			NewCommitEvent->Trigger();
		}
	}

	bool BlockAndDequeue(FZenCommitInfo& OutCommit)
	{
		for (;;)
		{
			{
				FScopeLock _(&QueueCriticalSection);
				if (Queue.Dequeue(OutCommit))
				{
					return true;
				}
			}

			if (bCompleteAdding)
			{
				return false;
			}

			NewCommitEvent->Wait();
		}
	}

	void CompleteAdding()
	{
		bCompleteAdding = true;
		NewCommitEvent->Trigger();
	}

	void ResetAdding()
	{
		bCompleteAdding = false;
	}

private:
	FEventRef					NewCommitEvent;
	FCriticalSection			QueueCriticalSection;
	TQueue<FZenCommitInfo>		Queue;
	TAtomic<bool>				bCompleteAdding{false};
};

TArray<const UTF8CHAR*> FZenStoreWriter::ReservedOplogKeys;

void FZenStoreWriter::StaticInit()
{
	if (ReservedOplogKeys.Num() > 0)
	{
		return;
	}

	ReservedOplogKeys.Append({ UTF8TEXT("files"), UTF8TEXT("key"), UTF8TEXT("packagedata"), UTF8TEXT("bulkdata"), UTF8TEXT("packagestoreentry") });
	Algo::Sort(ReservedOplogKeys, [](const UTF8CHAR* A, const UTF8CHAR* B)
		{
			return FUtf8StringView(A).Compare(FUtf8StringView(B), ESearchCase::IgnoreCase) < 0;
		});;
}

FZenStoreWriter::FZenStoreWriter(
	const FString& InOutputPath, 
	const FString& InMetadataDirectoryPath, 
	const ITargetPlatform* InTargetPlatform,
	TSharedRef<ICookArtifactReader> InCookArtifactReader
)
	: CookArtifactReader(InCookArtifactReader)
	, TargetPlatform(*InTargetPlatform)
	, TargetPlatformFName(*InTargetPlatform->PlatformName())
	, OutputPath(InOutputPath)
	, MetadataDirectoryPath(InMetadataDirectoryPath)
	, PackageStoreOptimizer(new FPackageStoreOptimizer())
	, CookMode(ICookedPackageWriter::FCookInfo::CookByTheBookMode)
	, bInitialized(false)
	, bProvidePerPackageResults(false)
{
	StaticInit();

	FString ParentProjectId;
	GetZenStoreProjectId(ProjectId, ParentProjectId);

	if (FParse::Value(FCommandLine::Get(), TEXT("-ZenStorePlatform="), OplogId) == false)
	{
		OplogId = InTargetPlatform->PlatformName();
	}
	
	HttpClient = MakeUnique<UE::FZenStoreHttpClient>();

#if UE_WITH_ZEN
	IsLocalConnection = HttpClient->GetZenServiceInstance().IsServiceRunningLocally();
#endif

	FString RootDir = FPaths::RootDir();
	FString EngineDir = FPaths::EngineDir();
	FPaths::NormalizeDirectoryName(EngineDir);
	FString ProjectDir = FPaths::ProjectDir();
	FPaths::NormalizeDirectoryName(ProjectDir);
	FString ProjectPath = FPaths::GetProjectFilePath();
	FPaths::NormalizeFilename(ProjectPath);

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	FString AbsServerRoot = PlatformFile.ConvertToAbsolutePathForExternalAppForRead(*RootDir);
	FString AbsEngineDir = PlatformFile.ConvertToAbsolutePathForExternalAppForRead(*EngineDir);
	FString AbsProjectDir = PlatformFile.ConvertToAbsolutePathForExternalAppForRead(*ProjectDir);
	FString ProjectFilePath = PlatformFile.ConvertToAbsolutePathForExternalAppForRead(*ProjectPath);

	HttpClient->TryCreateProject(ProjectId, ParentProjectId, OplogId, AbsServerRoot, AbsEngineDir, AbsProjectDir, IsLocalConnection ? ProjectFilePath : FStringView());

	PackageStoreOptimizer->Initialize();

	ZenFileSystemManifest = MakeUnique<FZenFileSystemManifest>(TargetPlatform, OutputPath);
	
	CommitQueue = MakeUnique<FCommitQueue>();
	
	Compressor = FOodleDataCompression::ECompressor::Mermaid;
	CompressionLevel = FOodleDataCompression::ECompressionLevel::VeryFast;
}

FZenStoreWriter::~FZenStoreWriter()
{
	if (CommitThread.IsValid())
	{
		UE_LOG(LogZenStoreWriter, Display, TEXT("Aborted, flushing..."));
		CommitQueue->CompleteAdding();
		CommitThread.Wait();
	}

	FScopeLock _(&PackagesCriticalSection);

	if (PendingPackages.Num())
	{
		UE_LOG(LogZenStoreWriter, Warning, TEXT("Pending packages at shutdown!"));
	}
}

void FZenStoreWriter::WritePackageData(const FPackageInfo& Info, FLargeMemoryWriter& ExportsArchive, const TArray<FFileRegion>& FileRegions)
{
	check(Info.ChunkId.IsValid());
	FPendingPackageState& ExistingState = GetPendingPackage(Info.PackageName);
	FPackageDataEntry& Entry = ExistingState.PackageData.AddDefaulted_GetRef();

	TRACE_CPUPROFILER_EVENT_SCOPE(FZenStoreWriter::WritePackageData);

	FIoBuffer PackageBuffer;
	if (ExistingState.PreOptimizedPackage.IsValid())
	{
		// If we are writing output data after having done a diff operation, we may already have pre-optimized package data in memory and
		// we should use that instead of generating it again.
		Entry.OptimizedPackage = MoveTemp(ExistingState.PreOptimizedPackage);
		PackageBuffer = FIoBuffer(FIoBuffer::Clone, ExportsArchive.GetData(), Info.HeaderSize);
	}
	else
	{
		ExistingState.OriginalHeaderSize = Info.HeaderSize;

		int64 DataSize = ExportsArchive.TotalSize();
		FIoBuffer PackageData(FIoBuffer::AssumeOwnership, ExportsArchive.ReleaseOwnership(), DataSize);

		FIoBuffer CookedHeaderBuffer = FIoBuffer(PackageData.Data(), Info.HeaderSize, PackageData);
		FIoBuffer CookedExportsBuffer = FIoBuffer(PackageData.Data() + Info.HeaderSize, PackageData.DataSize() - Info.HeaderSize, PackageData);
		Entry.OptimizedPackage.Reset(PackageStoreOptimizer->CreatePackageFromCookedHeader(Info.PackageName, CookedHeaderBuffer));
		PackageBuffer = PackageStoreOptimizer->CreatePackageBuffer(Entry.OptimizedPackage.Get(), CookedExportsBuffer);
	}

	Entry.FileRegions = FileRegions;
	for (FFileRegion& Region : Entry.FileRegions)
	{
		// Adjust regions so they are relative to the start of the export bundle buffer
		Region.Offset -= ExistingState.OriginalHeaderSize;
		Region.Offset += Entry.OptimizedPackage->GetHeaderSize();
	}

	// Commit to Zen build store

	FCbObjectId ChunkOid = ToObjectId(Info.ChunkId);

	Entry.CompressedPayload = Async(EAsyncExecution::TaskGraph, [this, PackageBuffer]()
	{ 
		return FCompressedBuffer::Compress(FSharedBuffer::MakeView(PackageBuffer.GetView()), Compressor, CompressionLevel);
	});

	Entry.Info				= Info;
	Entry.ChunkId			= ChunkOid;
	Entry.IsValid			= true;
}

void FZenStoreWriter::WriteIoStorePackageData(const FPackageInfo& Info, const FIoBuffer& PackageData, const FPackageStoreEntryResource& PackageStoreEntry, const TArray<FFileRegion>& FileRegions)
{
	check(Info.ChunkId.IsValid());

	TRACE_CPUPROFILER_EVENT_SCOPE(WriteIoStorePackageData);

	//WriteFileRegions(*FPaths::ChangeExtension(Info.LooseFilePath, FString(".uexp") + FFileRegion::RegionsFileExtension), FileRegionsCopy);

	FCbObjectId ChunkOid = ToObjectId(Info.ChunkId);

	FPendingPackageState& ExistingState = GetPendingPackage(Info.PackageName);

	FPackageDataEntry& Entry = ExistingState.PackageData.AddDefaulted_GetRef();

	PackageData.EnsureOwned();

	Entry.CompressedPayload = Async(EAsyncExecution::TaskGraph, [this, PackageData]()
	{ 
		return FCompressedBuffer::Compress(FSharedBuffer::MakeView(PackageData.GetView()), Compressor, CompressionLevel);
	});

	Entry.Info				= Info;
	Entry.ChunkId			= ChunkOid;
	Entry.IsValid			= true;
}

void FZenStoreWriter::WriteBulkData(const FBulkDataInfo& Info, const FIoBuffer& BulkData, const TArray<FFileRegion>& FileRegions)
{
	check(Info.ChunkId.IsValid());

	FCbObjectId ChunkOid = ToObjectId(Info.ChunkId);

	FPendingPackageState& ExistingState = GetPendingPackage(Info.PackageName);

	FBulkDataEntry& BulkEntry = ExistingState.BulkData.AddDefaulted_GetRef(); 

	BulkData.EnsureOwned();

	BulkEntry.CompressedPayload = Async(EAsyncExecution::TaskGraph, [this, BulkData]()
	{ 
		return FCompressedBuffer::Compress(FSharedBuffer::MakeView(BulkData.GetView()), Compressor, CompressionLevel);
	});

	BulkEntry.Info		= Info;
	BulkEntry.ChunkId	= ChunkOid;
	BulkEntry.IsValid	= true;
	BulkEntry.FileRegions = FileRegions;
}

void FZenStoreWriter::WriteAdditionalFile(const FAdditionalFileInfo& Info, const FIoBuffer& FileData)
{
	FPendingPackageState& ExistingState = GetPendingPackage(Info.PackageName);

	FFileDataEntry& FileEntry = ExistingState.FileData.AddDefaulted_GetRef();
	
	FileData.EnsureOwned();

	auto WriteToFile = [](const FString& Filename, const FIoBuffer& FileData)
	{
		ON_SCOPE_EXIT
		{
			UE::SavePackageUtilities::DecrementOutstandingAsyncWrites();
		};

		IFileManager& FileManager = IFileManager::Get();
		int64 DataSize = IntCastChecked<int64>(FileData.DataSize());

		for (int Tries = 0; Tries < 3; ++Tries)
		{
			if (FArchive* Ar = FileManager.CreateFileWriter(*Filename))
			{
				Ar->Serialize(const_cast<uint8*>(FileData.GetData()), DataSize);
				bool bArchiveError = Ar->IsError();
				delete Ar;

				int64 ActualSize = FileManager.FileSize(*Filename);
				if (ActualSize != DataSize)
				{
					FileManager.Delete(*Filename);

					UE_LOG(LogZenStoreWriter, Fatal, TEXT("Could not save to %s! Tried to write %" INT64_FMT " bytes but resultant size was %" INT64_FMT ".%s"),
						*Filename, DataSize, ActualSize, bArchiveError ? TEXT(" Ar->Serialize failed.") : TEXT(""));
				}
				return;
			}
		}

		UE_LOG(LogZenStoreWriter, Fatal, TEXT("Could not write to %s!"), *Filename);
	};

	UE::SavePackageUtilities::IncrementOutstandingAsyncWrites();
	FileEntry.CompressedPayload = Async(EAsyncExecution::TaskGraph, [this, Info, FileData, WriteToFile]()
	{
		WriteToFile(Info.Filename, FileData);
		return FCompressedBuffer::Compress(FSharedBuffer::MakeView(FileData.GetView()), Compressor, CompressionLevel);
	});

	const FZenFileSystemManifestEntry& ManifestEntry = ZenFileSystemManifest->CreateManifestEntry(Info.Filename);
	FileEntry.Info					= Info;
	FileEntry.Info.ChunkId			= ManifestEntry.FileChunkId;
	FileEntry.ZenManifestServerPath = ManifestEntry.ServerPath;
	FileEntry.ZenManifestClientPath = ManifestEntry.ClientPath;

	if (bProvidePerPackageResults)
	{
		PackageAdditionalFiles.FindOrAdd(Info.PackageName).Add(Info.Filename);
	}
}

void FZenStoreWriter::WriteLinkerAdditionalData(const FLinkerAdditionalDataInfo& Info, const FIoBuffer& Data, const TArray<FFileRegion>& FileRegions)
{
	// LinkerAdditionalData is not yet implemented in this writer; it is only used for VirtualizedBulkData which is not used in cooked content
	checkNoEntry();
}

void FZenStoreWriter::WritePackageTrailer(const FPackageTrailerInfo& Info, const FIoBuffer& Data)
{
	// PackageTrailers are not yet implemented in this writer; it is only used for EditorBulkData which is not used in cooked content
	checkNoEntry();
}

void FZenStoreWriter::RegisterDeterminismHelper(UObject* SourceObject,
	const TRefCountPtr<UE::Cook::IDeterminismHelper>& DeterminismHelper)
{
	if (RegisterDeterminismHelperCallback)
	{
		RegisterDeterminismHelperCallback(SourceObject, DeterminismHelper);
	}
}

void FZenStoreWriter::Initialize(const FCookInfo& Info)
{
	CookMode = Info.CookMode;

	if (!bInitialized)
	{
		bool CleanBuild = Info.bFullBuild && !Info.bWorkerOnSharedSandbox;
		if (CleanBuild)
		{
			bool bOplogDeleted = HttpClient->TryDeleteOplog(ProjectId, OplogId);
			UE_CLOG(!bOplogDeleted, LogZenStoreWriter, Fatal, TEXT("Failed to delete oplog on the ZenServer"));

			UE_LOG(LogZenStoreWriter, Display, TEXT("Deleting %s..."), *OutputPath);
			const bool bRequireExists = false;
			const bool bTree = true;
			IFileManager::Get().DeleteDirectory(*OutputPath, bRequireExists, bTree);
		}

		FString OplogLifetimeMarkerPath = OutputPath / TEXT("ue.projectstore");
		TUniquePtr<FArchive> OplogMarker(IFileManager::Get().CreateFileWriter(*OplogLifetimeMarkerPath));

		bool bOplogEstablished = HttpClient->TryCreateOplog(ProjectId, OplogId, OplogLifetimeMarkerPath);

		if (bOplogEstablished && OplogMarker)
		{
			bool IsRunningLocally = false;
#if UE_WITH_ZEN
			IsRunningLocally = HttpClient->GetZenServiceInstance().IsServiceRunningLocally();
#endif
			TSharedRef<TJsonWriter<UTF8CHAR, TPrettyJsonPrintPolicy<UTF8CHAR>>> Writer = TJsonWriterFactory<UTF8CHAR, TPrettyJsonPrintPolicy<UTF8CHAR>>::Create(OplogMarker.Get());
			Writer->WriteObjectStart();
			Writer->WriteObjectStart(TEXT("zenserver"));
			Writer->WriteValue(TEXT("islocalhost"), IsRunningLocally);
			Writer->WriteValue(TEXT("hostname"), HttpClient->GetHostName());
			if (IsRunningLocally)
			{
				ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get();
				if (SocketSubsystem != nullptr)
				{
					TArray<TSharedPtr<FInternetAddr>> Addresses;
					if (SocketSubsystem->GetLocalAdapterAddresses(Addresses))
					{
						Writer->WriteArrayStart("remotehostnames");
						for (const TSharedPtr<FInternetAddr>& Address : Addresses)
						{
							Writer->WriteValue(Address->ToString(false));
						}
						FString MachineHostName;
						if (SocketSubsystem->GetHostName(MachineHostName))
						{
							// Try to acquire FQDN
							FAddressInfoResult GAIRequest = SocketSubsystem->GetAddressInfo(*MachineHostName, nullptr, EAddressInfoFlags::AllResultsWithMapping | EAddressInfoFlags::OnlyUsableAddresses | EAddressInfoFlags::AllResults | EAddressInfoFlags::FQDomainName, NAME_None);
							if (GAIRequest.ReturnCode == SE_NO_ERROR && !GAIRequest.CanonicalNameResult.IsEmpty())
							{
								MachineHostName = GAIRequest.CanonicalNameResult;
							}

							Writer->WriteValue(TEXT("hostname://") + MachineHostName);
						}
#if PLATFORM_MAC
						// Store the Bonjour hostname when on a mac so that the client can attempt to connect via USB.
						// This is done since the "Link Local" IP of the USB connection changes everytime
						// the cable is plugged in, so there is no stable IP to save.
						bool bAppleTarget = TargetPlatformFName == TEXT("IOS") || TargetPlatformFName == TEXT("TVOS");
						if (bAppleTarget)
						{
							FString MacBonjourName = FPlatformMisc::GetBonjourName();
							
							// If there is no Bonjour name, fallback to the mac's hostname
							if (MacBonjourName.IsEmpty())
							{
								MacBonjourName = FPlatformProcess::ComputerName();
							}
							
							if (!MacBonjourName.IsEmpty())
							{
								Writer->WriteValue(TEXT("macserver://") + MacBonjourName);
							}
						}
#endif
						Writer->WriteArrayEnd();
					}
				}
			}
			Writer->WriteValue(TEXT("hostport"), HttpClient->GetPort());
			Writer->WriteValue(TEXT("projectid"), ProjectId);
			Writer->WriteValue(TEXT("oplogid"), OplogId);
			Writer->WriteObjectEnd();
			Writer->WriteObjectEnd();
			Writer->Close();
		}

		OplogMarker.Reset();

		if (!bOplogEstablished && CleanBuild)
		{
			IFileManager::Get().Delete(*OplogLifetimeMarkerPath);
		}
		UE_CLOG(!bOplogEstablished, LogZenStoreWriter, Fatal, TEXT("Failed to establish oplog on the ZenServer"));

		if (!Info.bFullBuild)
		{
			UE_LOG(LogZenStoreWriter, Display, TEXT("Fetching oplog..."), *ProjectId, *OplogId);

			TFuture<FIoStatus> FutureOplogStatus = HttpClient->GetOplog(false /* bTrimByReferencedSet*/)
				.Next([this](TIoStatusOr<FCbObject> OplogStatus)
				{
					if (!OplogStatus.IsOk())
					{
						return OplogStatus.Status();
					}

					FCbObject Oplog = OplogStatus.ConsumeValueOrDie();

					if (Oplog["entries"])
					{
						FWriteScopeLock _(EntriesLock);
						for (FCbField& OplogEntry : Oplog["entries"].AsArray())
						{
							FCbObject OplogObj = OplogEntry.AsObject();

							if (OplogObj["packagestoreentry"])
							{
								FPackageStoreEntryResource Entry = FPackageStoreEntryResource::FromCbObject(OplogObj["packagestoreentry"].AsObject());
								const FName PackageName = Entry.PackageName;

								const int32 Index = PackageStoreEntries.Num();

								PackageStoreEntries.Add(MoveTemp(Entry));
								FOplogCookInfo& CookInfo = CookedPackagesInfo.Add_GetRef({ PackageName });
								PackageNameToIndex.Add(PackageName, Index);

								for (FCbFieldView Field : OplogObj)
								{
									FUtf8StringView FieldName = Field.GetName();
									if (IsReservedOplogKey(FieldName))
									{
										continue;
									}
									if (Field.IsHash())
									{
										const UTF8CHAR* AttachmentId = UE::FZenStoreHttpClient::FindOrAddAttachmentId(FieldName);
										CookInfo.Attachments.Add({ AttachmentId, Field.AsHash() });
									}
								}
								CookInfo.Attachments.Shrink();
								check(Algo::IsSorted(CookInfo.Attachments,
									[](const FOplogCookInfo::FAttachment& A, const FOplogCookInfo::FAttachment& B)
									{
										return FUtf8StringView(A.Key).Compare(FUtf8StringView(B.Key), ESearchCase::IgnoreCase) < 0;
									}));
							}
						}
					}

					return FIoStatus::Ok;
				});

			UE_LOG(LogZenStoreWriter, Display, TEXT("Fetching file manifest..."), *ProjectId, *OplogId);

			TIoStatusOr<FCbObject> FileStatus = HttpClient->GetFiles().Get();
			if (FileStatus.IsOk())
			{
				FCbObject FilesObj = FileStatus.ConsumeValueOrDie();
				for (FCbField& FileEntry : FilesObj["files"])
				{
					FCbObject FileObj = FileEntry.AsObject();
					FCbObjectId FileId = FileObj["id"].AsObjectId();
					FString ServerPath = FString(FileObj["serverpath"].AsString());
					FString ClientPath = FString(FileObj["clientpath"].AsString());

					if (!ServerPath.IsEmpty())
					{
						FIoChunkId FileChunkId;
						FileChunkId.Set(FileId.GetView());

						ZenFileSystemManifest->AddManifestEntry(FileChunkId, MoveTemp(ServerPath), MoveTemp(ClientPath));
					}
				}

				UE_LOG(LogZenStoreWriter, Display, TEXT("Fetched '%d' file(s) from oplog '%s/%s'"), ZenFileSystemManifest->NumEntries(), *ProjectId, *OplogId);
			}
			else
			{
				UE_LOG(LogZenStoreWriter, Warning, TEXT("Failed to fetch file(s) from oplog '%s/%s'"), *ProjectId, *OplogId);
			}

			if (FutureOplogStatus.Get().IsOk())
			{
				UE_LOG(LogZenStoreWriter, Display, TEXT("Fetched '%d' packages(s) from oplog '%s/%s'"), PackageStoreEntries.Num(), *ProjectId, *OplogId);
			}
			else
			{
				UE_LOG(LogZenStoreWriter, Warning, TEXT("Failed to fetch oplog '%s/%s'"), *ProjectId, *OplogId);
			}
		}
		bInitialized = true;
	}
	else
	{
		if (Info.bFullBuild)
		{
			RemoveCookedPackages();
		}
	}
}

void FZenStoreWriter::BeginCook(const FCookInfo& Info)
{
	if (Info.bWorkerOnSharedSandbox)
	{
		bProvidePerPackageResults = true;
	}
	AllPackageHashes.Empty();

	if (CookMode == ICookedPackageWriter::FCookInfo::CookOnTheFlyMode)
	{
		ZenFileSystemManifest->Generate();
		TIoStatusOr<uint64> Status = HttpClient->AppendOp(CreateProjectMetaDataOpPackage("CookOnTheFly"));
		UE_CLOG(!Status.IsOk(), LogZenStoreWriter, Fatal, TEXT("Failed to append OpLog"));
	}

	if (FPlatformProcess::SupportsMultithreading())
	{
		CommitQueue->ResetAdding();
		CommitThread = AsyncThread([this]()
		{ 
			for(;;)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(FZenStoreWriter::WaitingOnCooker);
				FZenCommitInfo Commit;
				if (!CommitQueue->BlockAndDequeue(Commit))
				{
					break;
				}
				CommitPackageInternal(MoveTemp(Commit));
			}
		});
	}
}

void FZenStoreWriter::EndCook(const FCookInfo& Info)
{
	UE_LOG(LogZenStoreWriter, Display, TEXT("Flushing..."));
	
	CommitQueue->CompleteAdding();
	CommitThread.Wait();
	CommitThread.Reset();

	if (!Info.bWorkerOnSharedSandbox)
	{
		ZenFileSystemManifest->Generate();
		if (FCbPackage ReferencedSetPackage = CreateReferencedSetOpPackage())
		{
			TIoStatusOr<uint64> Status = HttpClient->AppendOp(MoveTemp(ReferencedSetPackage));
			UE_CLOG(!Status.IsOk(), LogZenStoreWriter, Fatal, TEXT("Failed to append ReferencedSetOp"));
		}
		{
			TIoStatusOr<uint64> Status = HttpClient->EndBuildPass(CreateProjectMetaDataOpPackage("EndCook"));
			UE_CLOG(!Status.IsOk(), LogZenStoreWriter, Fatal, TEXT("Failed to append OpLog and end the build pass"));
		}
	}

	UE_LOG(LogZenStoreWriter, Display, TEXT("Output:\t%d Public runtime script objects"), PackageStoreOptimizer->GetTotalScriptObjectCount());
}

FZenStoreWriter::ZenHostInfo FZenStoreWriter::GetHostInfo() const
{
	FZenStoreWriter::ZenHostInfo Info;
	Info.ProjectId = ProjectId;
	Info.OplogId = OplogId;
	if (IsLocalConnection)
	{
		Info.HostName = "localhost";
	}
	else
	{
		Info.HostName = HttpClient->GetHostName();
	}
	Info.HostPort = HttpClient->GetPort();
	return Info;
}

void FZenStoreWriter::BeginPackage(const FBeginPackageInfo& Info)
{
	FPendingPackageState& State = AddPendingPackage(Info.PackageName);
	State.PackageName = Info.PackageName;
}

bool FZenStoreWriter::IsReservedOplogKey(FUtf8StringView Key)
{
	int32 Index = Algo::LowerBound(ReservedOplogKeys, Key,
		[](const UTF8CHAR* Existing, FUtf8StringView Key)
		{
			return FUtf8StringView(Existing).Compare(Key, ESearchCase::IgnoreCase) < 0;
		});
	return Index != ReservedOplogKeys.Num() &&
		FUtf8StringView(ReservedOplogKeys[Index]).Equals(Key, ESearchCase::IgnoreCase);
}

void FZenStoreWriter::CommitPackage(FCommitPackageInfo&& Info)
{
	if (Info.Status == ECommitStatus::Canceled || Info.Status == ECommitStatus::NotCommitted)
	{
		RemovePendingPackage(Info.PackageName);
		return;
	}

	UE::SavePackageUtilities::IncrementOutstandingAsyncWrites();

	// If we are computing hashes, we need to allocate where the hashes will go.
	// Access to this is protected by the above IncrementOutstandingAsyncWrites.
	if (EnumHasAnyFlags(Info.WriteOptions, EWriteOptions::ComputeHash))
	{
		FPendingPackageState& ExistingState = GetPendingPackage(Info.PackageName);
		ExistingState.PackageHashes = new FPackageHashes();
		if (bProvidePerPackageResults)
		{
			ExistingState.PackageHashesCompletionPromise.Reset(new TPromise<int>());
			ExistingState.PackageHashes->CompletionFuture = ExistingState.PackageHashesCompletionPromise->GetFuture();
		}

		if (Info.Status == ECommitStatus::Success)
		{
			// Only record hashes for successful saves. A single package can be saved unsuccessfully multiple times
			// during a cook if it keeps timing out.
			TRefCountPtr<FPackageHashes>& ExistingPackageHashes = AllPackageHashes.FindOrAdd(Info.PackageName);
			// This looks weird but we've found the _TRefCountPtr_, not the FPackageHashes. When newly assigned
			// it will be an empty pointer, which is what we want.
			if (ExistingPackageHashes.IsValid())
			{
				UE_LOG(LogZenStoreWriter, Error, TEXT("FZenStoreWriter commiting the same package twice during a cook! (%s)"), *Info.PackageName.ToString());
			}
			ExistingPackageHashes = ExistingState.PackageHashes;
		}
	}

	TUniquePtr<FPendingPackageState> PackageState = RemovePendingPackage(Info.PackageName);
	if (EnumHasAnyFlags(Info.WriteOptions, IPackageWriter::EWriteOptions::Write | IPackageWriter::EWriteOptions::ComputeHash))
	{
		checkf(Info.Status != ECommitStatus::Success || !PackageState->PackageData.IsEmpty(),
			TEXT("CommitPackage called with CommitStatus::Success but without first calling WritePackageData"));
	}
	FZenCommitInfo ZenCommitInfo{ Forward<FCommitPackageInfo>(Info), MoveTemp(PackageState) };
	if (FPlatformProcess::SupportsMultithreading())
	{
		CommitQueue->Enqueue(MoveTemp(ZenCommitInfo));
	}
	else
	{
		CommitPackageInternal(MoveTemp(ZenCommitInfo));
	}
}

void FZenStoreWriter::CommitPackageInternal(FZenCommitInfo&& ZenCommitInfo)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FZenStoreWriter::CommitPackage);
	FCommitPackageInfo& CommitInfo = ZenCommitInfo.CommitInfo;
	
	TUniquePtr<FPendingPackageState> PackageState = MoveTemp(ZenCommitInfo.PackageState);
	checkf(PackageState.IsValid(), TEXT("Trying to commit non-pending package '%s'"), *CommitInfo.PackageName.ToString());

	IPackageStoreWriter::FCommitEventArgs CommitEventArgs;

	CommitEventArgs.PlatformName = TargetPlatformFName;
	CommitEventArgs.PackageName = CommitInfo.PackageName;
	CommitEventArgs.EntryIndex = INDEX_NONE;

	const bool bWriteHashFlag = EnumHasAnyFlags(CommitInfo.WriteOptions, EWriteOptions::ComputeHash);
	const bool bWriteOp = EnumHasAnyFlags(CommitInfo.WriteOptions, EWriteOptions::Write);
	const bool bComputeHash = bWriteHashFlag && CommitInfo.Status == ECommitStatus::Success;
	const bool bComputeValidPackage = bWriteOp && CommitInfo.Status == ECommitStatus::Success;

	if (bWriteOp)
	{
		checkf(EnumHasAllFlags(CommitInfo.WriteOptions, EWriteOptions::Write), TEXT("Partial EWriteOptions::Write options are not yet implemented."));
		checkf(!EnumHasAnyFlags(CommitInfo.WriteOptions, EWriteOptions::SaveForDiff), TEXT("-diffonly -savefordiff is not yet implemented."));

		FPackageStoreEntryResource PackageStoreEntry;

		if (bComputeValidPackage)
		{
			checkf(!PackageState->PackageData.IsEmpty(), TEXT("CommitPackage called with bSucceeded but without first calling WritePackageData"));

			FPackageDataEntry* PkgData = nullptr;
			FPackageDataEntry* OptionalSegmentPkgData = nullptr;
			for (FPackageDataEntry& PackageDataEntry : PackageState->PackageData)
			{
				check(PackageDataEntry.Info.MultiOutputIndex <= 1);
				if (PackageDataEntry.Info.MultiOutputIndex == 0)
				{
					check(!PkgData);
					PkgData = &PackageDataEntry;
				}
				else if (PackageDataEntry.Info.MultiOutputIndex == 1)
				{
					check(!OptionalSegmentPkgData);
					OptionalSegmentPkgData = &PackageDataEntry;
				}
			}
			PackageStoreEntry = PackageStoreOptimizer->CreatePackageStoreEntry(
				PkgData->OptimizedPackage.Get(),
				OptionalSegmentPkgData ? OptionalSegmentPkgData->OptimizedPackage.Get() : nullptr);
		}
		else
		{
			const bool bHasCookError = CommitInfo.Status == ECommitStatus::Error;
			PackageStoreEntry = FPackageStoreEntryResource::CreateEmptyPackage(
				CommitInfo.PackageName, bHasCookError);
		}

		FMD5 PkgHashGen;
		FCbPackage OplogEntry;
		// Commit attachments
		const int32 NumAttachments = CommitInfo.Attachments.Num();
		TArray<FOplogCookInfo::FAttachment> CookInfoAttachments;
		TArray<FCbAttachment, TInlineAllocator<2>> CbAttachments;

		if (NumAttachments)
		{
			TArray<const FCommitAttachmentInfo*, TInlineAllocator<2>> SortedAttachments;
			SortedAttachments.Reserve(NumAttachments);
			
			for (const FCommitAttachmentInfo& AttachmentInfo : CommitInfo.Attachments)
			{
				SortedAttachments.Add(&AttachmentInfo);
			}

			SortedAttachments.Sort([](const FCommitAttachmentInfo& A, const FCommitAttachmentInfo& B)
			{
				return A.Key.Compare(B.Key, ESearchCase::IgnoreCase) < 0;
			});
			
			CbAttachments.Reserve(NumAttachments);
			CookInfoAttachments.Reserve(NumAttachments);
			
			for (const FCommitAttachmentInfo* AttachmentInfo : SortedAttachments)
			{
				check(!IsReservedOplogKey(AttachmentInfo->Key));
				const FCbAttachment& CbAttachment = CbAttachments.Add_GetRef(CreateAttachment(AttachmentInfo->Value.GetBuffer().ToShared()));
				OplogEntry.AddAttachment(CbAttachment);
				
				CookInfoAttachments.Add(FOplogCookInfo::FAttachment
				{
					UE::FZenStoreHttpClient::FindOrAddAttachmentId(AttachmentInfo->Key), CbAttachment.GetHash()
				});
			}
		}

		// Create the oplog entry object
		FCbWriter OplogEntryDesc;
		OplogEntryDesc.BeginObject();
		FString PackageNameKey = CommitInfo.PackageName.ToString();
		PackageNameKey.ToLowerInline();
		OplogEntryDesc << "key" << PackageNameKey;
		OplogEntryDesc << "packagestoreentry" << PackageStoreEntry;

		auto AppendFileNameAndRegionsToOplog = [this, &OplogEntryDesc](const FString& LooseFilePath, const TArray<FFileRegion>& FileRegions)
		{
			FStringView RelativePathView;
			if (FPathViews::TryMakeChildPathRelativeTo(LooseFilePath, OutputPath, RelativePathView))
			{
				OplogEntryDesc << "filename" << RelativePathView;
			}
			if (!FileRegions.IsEmpty())
			{
				OplogEntryDesc.BeginArray("fileregions");
				for (const FFileRegion& FileRegion : FileRegions)
				{
					OplogEntryDesc << FileRegion;
				}
				OplogEntryDesc.EndArray();
			}
		};

		if (bComputeValidPackage && PackageState->PackageData.Num())
		{
			OplogEntryDesc.BeginArray("packagedata");

			for (FPackageDataEntry& PkgData : PackageState->PackageData)
			{
				FCompressedBuffer Payload = PkgData.CompressedPayload.Get();
				if (bComputeHash)
				{
					PackageState->PackageHashes->ChunkHashes.Add(PkgData.Info.ChunkId, Payload.GetRawHash());
				}

				FCbAttachment PkgDataAttachment = FCbAttachment(Payload);
				PkgHashGen.Update(PkgDataAttachment.GetHash().GetBytes(), sizeof(FIoHash::ByteArray));
				OplogEntry.AddAttachment(PkgDataAttachment);

				OplogEntryDesc.BeginObject();
				OplogEntryDesc << "id" << PkgData.ChunkId;
				OplogEntryDesc << "size" << Payload.GetCompressedSize();
				OplogEntryDesc << "rawsize" << Payload.GetRawSize();
				OplogEntryDesc << "data" << PkgDataAttachment;
				AppendFileNameAndRegionsToOplog(PkgData.Info.LooseFilePath, PkgData.FileRegions);
				OplogEntryDesc.EndObject();
			}

			OplogEntryDesc.EndArray();
		}

		if (bComputeValidPackage && PackageState->BulkData.Num())
		{
			OplogEntryDesc.BeginArray("bulkdata");

			for (FBulkDataEntry& Bulk : PackageState->BulkData)
			{
				FCompressedBuffer Payload = Bulk.CompressedPayload.Get();
				if (bComputeHash)
				{
					PackageState->PackageHashes->ChunkHashes.Add(Bulk.Info.ChunkId, Payload.GetRawHash());
				}

				FCbAttachment BulkAttachment(Payload);
				PkgHashGen.Update(BulkAttachment.GetHash().GetBytes(), sizeof(FIoHash::ByteArray));
				OplogEntry.AddAttachment(BulkAttachment);

				OplogEntryDesc.BeginObject();
				OplogEntryDesc << "id" << Bulk.ChunkId;
				OplogEntryDesc << "type" << LexToString(Bulk.Info.BulkDataType);
				OplogEntryDesc << "size" << Payload.GetCompressedSize();
				OplogEntryDesc << "rawsize" << Payload.GetRawSize();
				OplogEntryDesc << "data" << BulkAttachment;
				AppendFileNameAndRegionsToOplog(Bulk.Info.LooseFilePath, Bulk.FileRegions);
				OplogEntryDesc.EndObject();
			}

			OplogEntryDesc.EndArray();
		}

		if (bComputeValidPackage && PackageState->FileData.Num())
		{
			OplogEntryDesc.BeginArray("files");

			for (FFileDataEntry& File : PackageState->FileData)
			{
				if (bComputeHash)
				{
					PackageState->PackageHashes->ChunkHashes.Add(File.Info.ChunkId, File.CompressedPayload.Get().GetRawHash());
				}

				FCbAttachment FileDataAttachment(File.CompressedPayload.Get());
				PkgHashGen.Update(FileDataAttachment.GetHash().GetBytes(), sizeof(FIoHash::ByteArray));
				OplogEntry.AddAttachment(FileDataAttachment);

				OplogEntryDesc.BeginObject();
				OplogEntryDesc << "id" << ToObjectId(File.Info.ChunkId);
				// ZenServer treats the hash stored in "data" as mutually exlusive with the string stored in "serverpath".
				// We must write data as a zero hash (or exclude it entirely) if we want to be able to get the serverpath from ZenServer later.
				// This is relevant to incremental cooks which will obtain the filesystem manifest contents from ZenServer.
				OplogEntryDesc << "data" << FIoHash::Zero;
				OplogEntryDesc << "serverpath" << File.ZenManifestServerPath;
				OplogEntryDesc << "clientpath" << File.ZenManifestClientPath;
				OplogEntryDesc.EndObject();

				CommitEventArgs.AdditionalFiles.Add(FAdditionalFileInfo
				{ 
					CommitInfo.PackageName,
					File.ZenManifestClientPath,
					File.Info.ChunkId
				});
			}

			OplogEntryDesc.EndArray();
		}

		if (bComputeHash)
		{
			PackageState->PackageHashes->PackageHash.Set(PkgHashGen);
		}

		for (int32 Index = 0; Index < NumAttachments; ++Index)
		{
			FCbAttachment& CbAttachment = CbAttachments[Index];
			FOplogCookInfo::FAttachment& CookInfoAttachment = CookInfoAttachments[Index];
			OplogEntryDesc << CookInfoAttachment.Key << CbAttachment;
		}

		OplogEntryDesc.EndObject();
		OplogEntry.SetObject(OplogEntryDesc.Save().AsObject());

		if (bComputeValidPackage && EntryCreatedEvent.IsBound())
		{
			IPackageStoreWriter::FEntryCreatedEventArgs EntryCreatedEventArgs
			{
				TargetPlatformFName,
				PackageStoreEntry
			};
			EntryCreatedEvent.Broadcast(EntryCreatedEventArgs);
		}

		{
			FWriteScopeLock _(EntriesLock);
			CommitEventArgs.EntryIndex = PackageNameToIndex.FindOrAdd(
				CommitInfo.PackageName, PackageStoreEntries.Num());
			if (CommitEventArgs.EntryIndex == PackageStoreEntries.Num())
			{
				PackageStoreEntries.Emplace();
				CookedPackagesInfo.Emplace();
			}
			PackageStoreEntries[CommitEventArgs.EntryIndex] = MoveTemp(PackageStoreEntry);

			FOplogCookInfo& CookInfo = CookedPackagesInfo[CommitEventArgs.EntryIndex];
			CookInfo.bUpToDate = true;
			CookInfo.Attachments = MoveTemp(CookInfoAttachments);
		}

		TIoStatusOr<uint64> Status = HttpClient->AppendOp(MoveTemp(OplogEntry));
		UE_CLOG(!Status.IsOk(), LogZenStoreWriter, Error, TEXT("Failed to commit oplog entry '%s' to Zen"), *CommitInfo.PackageName.ToString());
	}
	else if (bComputeHash)
	{
		checkf(!PackageState->PackageData.IsEmpty(), TEXT("CommitPackage called with bSucceeded but without first calling WritePackageData"));
		
		FMD5 PkgHashGen;
		
		for (FPackageDataEntry& PkgData : PackageState->PackageData)
		{
			FCompressedBuffer Payload = PkgData.CompressedPayload.Get();
			FIoHash IoHash = Payload.GetRawHash();
			PkgHashGen.Update(IoHash.GetBytes(), sizeof(FIoHash::ByteArray));
		}
		
		for (FBulkDataEntry& Bulk : PackageState->BulkData)
		{
			FCompressedBuffer Payload = Bulk.CompressedPayload.Get();
			FIoHash IoHash = Payload.GetRawHash();
			PackageState->PackageHashes->ChunkHashes.Add(Bulk.Info.ChunkId, IoHash);
			PkgHashGen.Update(IoHash.GetBytes(), sizeof(FIoHash::ByteArray));
		}
		
		for (FFileDataEntry& File : PackageState->FileData)
		{
			FCompressedBuffer Payload = File.CompressedPayload.Get();
			FIoHash IoHash = Payload.GetRawHash();
			PackageState->PackageHashes->ChunkHashes.Add(File.Info.ChunkId, IoHash);
			PkgHashGen.Update(IoHash.GetBytes(), sizeof(FIoHash::ByteArray));
		}
		
		PackageState->PackageHashes->PackageHash.Set(PkgHashGen);
	}

	if (bWriteOp)
	{
		BroadcastCommit(CommitEventArgs);
	}

	if (PackageState->PackageHashesCompletionPromise)
	{
		// Setting the CompletionFuture value may call arbitrary continuation code, so it
		// must be done outside of any lock.
		PackageState->PackageHashesCompletionPromise->EmplaceValue(0);
	}
	UE::SavePackageUtilities::DecrementOutstandingAsyncWrites();
}

void FZenStoreWriter::GetEntries(TFunction<void(TArrayView<const FPackageStoreEntryResource>, TArrayView<const FOplogCookInfo>)>&& Callback)
{
	FReadScopeLock _(EntriesLock);
	Callback(PackageStoreEntries, CookedPackagesInfo);
}

TUniquePtr<FAssetRegistryState> FZenStoreWriter::LoadPreviousAssetRegistry() 
{
	// Load the previous asset registry to return to CookOnTheFlyServer, and set the packages enumerated in both *this and
	// the returned asset registry to the intersection of the oplog and the previous asset registry;
	// to report a package as already cooked we have to have the information from both sources.
	FString PreviousAssetRegistryFile = FPaths::Combine(MetadataDirectoryPath, GetDevelopmentAssetRegistryFilename());
	TUniquePtr<FArchive> Reader(CookArtifactReader->CreateFileReader(*PreviousAssetRegistryFile));

	if (!Reader)
	{
		RemoveCookedPackages();
		return TUniquePtr<FAssetRegistryState>();
	}

	TUniquePtr<FAssetRegistryState> PreviousState = MakeUnique<FAssetRegistryState>();
	PreviousState->Load(*Reader);

	TSet<FName> RemoveSet;
	TArray<FName> RemoveArray;
	const TMap<FName, const FAssetPackageData*>& PreviousStatePackages = PreviousState->GetAssetPackageDataMap();
	{
		FReadScopeLock _(EntriesLock);
		for (const TPair<FName, const FAssetPackageData*>& Pair : PreviousStatePackages)
		{
			FName PackageName = Pair.Key;
			if (Pair.Value->DiskSize < 0)
			{
				// Keep the FailedSave previous cook packages; some of them (NeverCookPlaceholders) are not expected to exist
				// in the package store
				continue;
			}
			if (PackageNameToIndex.Find(PackageName))
			{
				continue;
			}
			RemoveSet.Add(PackageName);
		}
		if (RemoveSet.Num())
		{
			PreviousState->PruneAssetData(TSet<FName>(), RemoveSet, FAssetRegistrySerializationOptions());
		}

		for (TPair<FName, int32>& Pair : PackageNameToIndex)
		{
			FName PackageName = Pair.Key;
			if (!PreviousStatePackages.Find(PackageName))
			{
				RemoveArray.Add(PackageName);
			}
		}
	}
	if (RemoveArray.Num())
	{
		RemoveCookedPackages(RemoveArray);
	}

	return PreviousState;
}

FCbObject FZenStoreWriter::GetOplogAttachment(FName PackageName, FUtf8StringView AttachmentKey)
{
	FIoHash AttachmentHash;
	{
		FReadScopeLock _(EntriesLock);

		const int32* Idx = PackageNameToIndex.Find(PackageName);
		if (!Idx)
		{
			return FCbObject();
		}

		const UTF8CHAR* AttachmentId = UE::FZenStoreHttpClient::FindAttachmentId(AttachmentKey);
		if (!AttachmentId)
		{
			return FCbObject();
		}
		FUtf8StringView AttachmentIdView(AttachmentId);

		const FOplogCookInfo& CookInfo = CookedPackagesInfo[*Idx];
		int32 AttachmentIndex = Algo::LowerBound(CookInfo.Attachments, AttachmentIdView,
			[](const FOplogCookInfo::FAttachment& Existing, FUtf8StringView AttachmentIdView)
			{
				return FUtf8StringView(Existing.Key).Compare(AttachmentIdView, ESearchCase::IgnoreCase) < 0;
			});
		if (AttachmentIndex == CookInfo.Attachments.Num())
		{
			return FCbObject();
		}
		const FOplogCookInfo::FAttachment& Existing = CookInfo.Attachments[AttachmentIndex];
		if (!FUtf8StringView(Existing.Key).Equals(AttachmentIdView, ESearchCase::IgnoreCase))
		{
			return FCbObject();
		}
		AttachmentHash = Existing.Hash;
	}
	TIoStatusOr<FIoBuffer> BufferResult = HttpClient->ReadChunk(AttachmentHash);
	if (!BufferResult.IsOk())
	{
		return FCbObject();
	}
	FIoBuffer Buffer = BufferResult.ValueOrDie();
	if (Buffer.DataSize() == 0)
	{
		return FCbObject();
	}

	FSharedBuffer SharedBuffer = IoBufferToSharedBuffer(Buffer);
	return FCbObject(SharedBuffer);
}

void FZenStoreWriter::GetOplogAttachments(TArrayView<FName> PackageNames,
	TArrayView<FUtf8StringView> AttachmentKeys,
	TUniqueFunction<void(FName PackageName, FUtf8StringView AttachmentKey, FCbObject&& Attachment)>&& Callback)
{
	const int MaximumHashCount = PackageNames.Num() * AttachmentKeys.Num();
	TArray<FIoHash> AttachmentHashes;
	AttachmentHashes.Reserve(MaximumHashCount);
	struct FAttachmentHashParam
	{
		FName PackageName;
		FUtf8StringView AttachmentKey;

		FAttachmentHashParam(const FName& InPackageName, FUtf8StringView InAttachmentKey)
		: PackageName(InPackageName), AttachmentKey(InAttachmentKey)
		{
		}
	};
	TMultiMap<FIoHash, FAttachmentHashParam> AttachmentHashParams;
	AttachmentHashParams.Reserve(MaximumHashCount);

	TArray<FAttachmentHashParam> InvalidAttachmentHashParams;
	InvalidAttachmentHashParams.Reserve(MaximumHashCount);

	TArray<const UTF8CHAR*, TInlineAllocator<2>> AttachmentIds;
	for (FUtf8StringView AttachmentKey : AttachmentKeys)
	{
		AttachmentIds.Add(UE::FZenStoreHttpClient::FindAttachmentId(AttachmentKey));
	}

	{
		FReadScopeLock _(EntriesLock);

		for (FName PackageName : PackageNames)
		{
			const int32* Idx = PackageNameToIndex.Find(PackageName);

			for (int32 InputAttachmentIndex = 0; InputAttachmentIndex < AttachmentKeys.Num(); ++InputAttachmentIndex)
			{
				FUtf8StringView AttachmentKey = AttachmentKeys[InputAttachmentIndex];
				const UTF8CHAR* AttachmentId = AttachmentIds[InputAttachmentIndex];

				FIoHash AttachmentHash;
				ON_SCOPE_EXIT
				{
					if (AttachmentHash.IsZero())
					{
						InvalidAttachmentHashParams.Emplace(PackageName, AttachmentKey);
					}
					else
					{
						AttachmentHashes.Add(AttachmentHash);
						AttachmentHashParams.Emplace(AttachmentHash, FAttachmentHashParam{PackageName, AttachmentKey});
					}
				};

				if (!Idx || !AttachmentId)
				{
					continue;
				}

				FUtf8StringView AttachmentIdView(AttachmentId);

				const FOplogCookInfo& CookInfo = CookedPackagesInfo[*Idx];
				int32 AttachmentIndex = Algo::LowerBound(CookInfo.Attachments, AttachmentIdView,
					[](const FOplogCookInfo::FAttachment& Existing, FUtf8StringView AttachmentIdView)
					{
						return FUtf8StringView(Existing.Key).Compare(AttachmentIdView, ESearchCase::IgnoreCase) < 0;
					});
				if (AttachmentIndex == CookInfo.Attachments.Num())
				{
					continue;
				}
				const FOplogCookInfo::FAttachment& Existing = CookInfo.Attachments[AttachmentIndex];
				if (!FUtf8StringView(Existing.Key).Equals(AttachmentIdView, ESearchCase::IgnoreCase))
				{
					continue;
				}
				AttachmentHash = Existing.Hash;
			}
		}
	}

	// Invoke the callback for all invalid attachment hashes
	for (FAttachmentHashParam& InvalidAttachmentHashParam : InvalidAttachmentHashParams)
	{
		Callback(InvalidAttachmentHashParam.PackageName, InvalidAttachmentHashParam.AttachmentKey, FCbObject());
	}
	
	if (AttachmentHashes.IsEmpty())
	{
		return;
	}

	HttpClient->ReadChunksAsync(AttachmentHashes, [Callback = MoveTemp(Callback), AttachmentHashParams = MoveTemp(AttachmentHashParams)](const FIoHash& RawHash, TIoStatusOr<FIoBuffer> Result)
	{
		for (auto It(AttachmentHashParams.CreateConstKeyIterator(RawHash)); It; ++It)
		{
			const FAttachmentHashParam& Param = It.Value();
			if (!Result.IsOk())
			{
				Callback(Param.PackageName, Param.AttachmentKey, FCbObject());
				continue;
			}

			FIoBuffer Buffer = Result.ConsumeValueOrDie();
			if (Buffer.DataSize() == 0)
			{
				Callback(Param.PackageName, Param.AttachmentKey, FCbObject());
				continue;
			}
			FSharedBuffer SharedBuffer = IoBufferToSharedBuffer(Buffer);
			Callback(Param.PackageName, Param.AttachmentKey, FCbObject(SharedBuffer));
		}
	});
}

IPackageWriter::ECommitStatus FZenStoreWriter::GetCommitStatus(FName PackageName)
{
	FReadScopeLock _(EntriesLock);

	const int32* Idx = PackageNameToIndex.Find(PackageName);
	if (!Idx)
	{
		return ECommitStatus::NotCommitted;
	}
	if (PackageStoreEntries[*Idx].HasPackageData())
	{
		return ECommitStatus::Success;
	}
	if (!PackageStoreEntries[*Idx].HasPackageData())
	{
		return ECommitStatus::NothingToCook;
	}
	return ECommitStatus::Error;
}

void FZenStoreWriter::RemoveCookedPackages(TArrayView<const FName> PackageNamesToRemove)
{
	FWriteScopeLock _(EntriesLock);

	TSet<int32> PackageIndicesToKeep;
	for (int32 Idx = 0, Num = PackageStoreEntries.Num(); Idx < Num; ++Idx)
	{
		PackageIndicesToKeep.Add(Idx);
	}
	
	for (const FName& PackageName : PackageNamesToRemove)
	{
		if (const int32* Idx = PackageNameToIndex.Find(PackageName))
		{
			PackageIndicesToKeep.Remove(*Idx);
		}
	}

	const int32 NumPackagesToKeep = PackageIndicesToKeep.Num();
	
	TArray<FPackageStoreEntryResource> PreviousPackageStoreEntries = MoveTemp(PackageStoreEntries);
	TArray<FOplogCookInfo> PreviousCookedPackageInfo = MoveTemp(CookedPackagesInfo);
	PackageNameToIndex.Empty();

	if (NumPackagesToKeep > 0)
	{
		PackageStoreEntries.Reserve(NumPackagesToKeep);
		CookedPackagesInfo.Reserve(NumPackagesToKeep);
		PackageNameToIndex.Reserve(NumPackagesToKeep);

		int32 EntryIndex = 0;
		for (int32 Idx : PackageIndicesToKeep)
		{
			const FName PackageName = PreviousCookedPackageInfo[Idx].PackageName;

			PackageStoreEntries.Add(MoveTemp(PreviousPackageStoreEntries[Idx]));
			CookedPackagesInfo.Add(MoveTemp(PreviousCookedPackageInfo[Idx]));
			PackageNameToIndex.Add(PackageName, EntryIndex++);
		}
	}
}

void FZenStoreWriter::RemoveCookedPackages()
{
	FWriteScopeLock _(EntriesLock);

	PackageStoreEntries.Empty();
	CookedPackagesInfo.Empty();
	PackageNameToIndex.Empty();
}

void FZenStoreWriter::UpdatePackageModificationStatus(FName PackageName, bool bIncrementallyUnmodified,
	bool& bInOutShouldIncrementallySkip)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FZenStoreWriter::UpdatePackageModificationStatus);

	if (!bIncrementallyUnmodified)
	{
		return;
	}

	IPackageStoreWriter::FMarkUpToDateEventArgs MarkUpToDateEventArgs;

	{
		FWriteScopeLock _(EntriesLock);
		int32* Index = PackageNameToIndex.Find(PackageName);
		if (!Index)
		{
			if (!FPackageName::IsScriptPackage(WriteToString<128>(PackageName)))
			{
				UE_LOG(LogZenStoreWriter, Verbose, TEXT("UpdatePackageModificationStatus called with package %s that is not in the oplog."),
					*PackageName.ToString());
			}
			return;
		}

		MarkUpToDateEventArgs.PackageIndexes.Add(*Index);
		CookedPackagesInfo[*Index].bUpToDate = true;
	}
	if (MarkUpToDateEventArgs.PackageIndexes.Num())
	{
		BroadcastMarkUpToDate(MarkUpToDateEventArgs);
	}
}

bool FZenStoreWriter::GetPreviousCookedBytes(const FPackageInfo& Info, FPreviousCookedBytesData& OutData)
{
	if (!Info.ChunkId.IsValid())
	{
		return false;
	}

	FIoReadOptions ReadOptions;
	TIoStatusOr<FIoBuffer> Status = HttpClient->ReadChunk(Info.ChunkId, ReadOptions.GetOffset(), ReadOptions.GetSize());
	if (!Status.IsOk())
	{
		return false;
	}

	FIoBuffer Buffer = Status.ConsumeValueOrDie();
	OutData.HeaderSize = reinterpret_cast<const FZenPackageSummary*>(Buffer.Data())->HeaderSize;
	OutData.Size = Buffer.GetSize();
	OutData.StartOffset = 0;
	Buffer.EnsureOwned();
	OutData.Data.Reset(Buffer.Release().ConsumeValueOrDie());

	return true;
}

void FZenStoreWriter::CompleteExportsArchiveForDiff(FPackageInfo& Info, FLargeMemoryWriter& ExportsArchive)
{
	check(Info.ChunkId.IsValid());
	FPendingPackageState& ExistingState = GetPendingPackage(Info.PackageName);

	uint64 OptimizedHeaderSize = 0;
	TUniquePtr<FPackageStorePackage> PackageStorePackage;
	FIoBuffer PackageBuffer;
	{
		FIoBuffer CookedHeaderBuffer(FIoBuffer::Wrap, ExportsArchive.GetData(), Info.HeaderSize);
		FIoBuffer CookedExportsBuffer(FIoBuffer::Wrap, ExportsArchive.GetData() + Info.HeaderSize, ExportsArchive.TotalSize() - Info.HeaderSize);
		PackageStorePackage.Reset(PackageStoreOptimizer->CreatePackageFromCookedHeader(Info.PackageName, CookedHeaderBuffer));
		OptimizedHeaderSize = PackageStorePackage->GetHeaderSize();
		PackageBuffer = PackageStoreOptimizer->CreatePackageBuffer(PackageStorePackage.Get(), CookedExportsBuffer);
	}

	ExistingState.OriginalHeaderSize = Info.HeaderSize;
	ExportsArchive.Seek(0);
	FMemory::Free(ExportsArchive.ReleaseOwnership());
	ExportsArchive.Reserve(PackageBuffer.DataSize());
	ExportsArchive.Serialize(PackageBuffer.GetData(), PackageBuffer.DataSize());
	Info.HeaderSize = OptimizedHeaderSize;
	ExistingState.PreOptimizedPackage = MoveTemp(PackageStorePackage);
}

EPackageWriterResult FZenStoreWriter::BeginCacheForCookedPlatformData(
	FBeginCacheForCookedPlatformDataInfo& Info)
{
	return BeginCacheCallback(Info);
}

TFuture<FCbObject> FZenStoreWriter::WriteMPCookMessageForPackage(FName PackageName)
{
	TArray<FString> AdditionalFiles;
	PackageAdditionalFiles.RemoveAndCopyValue(PackageName, AdditionalFiles);

	TRefCountPtr<FPackageHashes> PackageHashes;
	AllPackageHashes.RemoveAndCopyValue(PackageName, PackageHashes);

	auto ComposeMessage =
	[AdditionalFiles=MoveTemp(AdditionalFiles)](FPackageHashes* PackageHashes)
	{
		FCbWriter Writer;
		Writer.BeginObject();
		if (!AdditionalFiles.IsEmpty())
		{
			Writer << "AdditionalFiles" << AdditionalFiles;
		}
		if (PackageHashes)
		{
			Writer << "PackageHash" << PackageHashes->PackageHash;
			Writer << "ChunkHashes" << PackageHashes->ChunkHashes.Array();
		}
		Writer.EndObject();
		return Writer.Save().AsObject();
	};

	if (PackageHashes && PackageHashes->CompletionFuture.IsValid())
	{
		TUniquePtr<TPromise<FCbObject>> Promise(new TPromise<FCbObject>());
		TFuture<FCbObject> ResultFuture = Promise->GetFuture();
		PackageHashes->CompletionFuture.Next(
			[PackageHashes, Promise = MoveTemp(Promise), ComposeMessage = MoveTemp(ComposeMessage)](int)
			{
				Promise->SetValue(ComposeMessage(PackageHashes.GetReference()));
			});
		return ResultFuture;
	}
	else
	{
		TPromise<FCbObject> Promise;
		Promise.SetValue(ComposeMessage(PackageHashes.GetReference()));
		return Promise.GetFuture();
	}
}

bool FZenStoreWriter::TryReadMPCookMessageForPackage(FName PackageName, FCbObjectView Message)
{
	TArray<FString> AdditionalFiles;
	if (LoadFromCompactBinary(Message["AdditionalFiles"], AdditionalFiles))
	{
		for (const FString& Filename : AdditionalFiles)
		{
			ZenFileSystemManifest->CreateManifestEntry(Filename);
		}
	}

	bool bOk = true;
	TRefCountPtr<FPackageHashes> ThisPackageHashes(new FPackageHashes());
	if (LoadFromCompactBinary(Message["PackageHash"], ThisPackageHashes->PackageHash))
	{
		TArray<TPair<FIoChunkId, FIoHash>> LocalChunkHashes;
		bOk = LoadFromCompactBinary(Message["ChunkHashes"], LocalChunkHashes) & bOk;
		if (bOk)
		{
			for (TPair<FIoChunkId, FIoHash>& Pair : LocalChunkHashes)
			{
				ThisPackageHashes->ChunkHashes.Add(Pair.Key, Pair.Value);
			}
			bool bAlreadyExisted = false;
			TRefCountPtr<FPackageHashes>& ExistingPackageHashes = AllPackageHashes.FindOrAdd(PackageName);
			bAlreadyExisted = ExistingPackageHashes.IsValid();
			ExistingPackageHashes = ThisPackageHashes;
			if (bAlreadyExisted)
			{
				UE_LOG(LogSavePackage, Error, TEXT("FZenStoreWriter encountered the same package twice in a cook! (%s)"),
					*PackageName.ToString());
			}
		}
	}

	return bOk;
}

FZenStoreWriter::FPendingPackageState& FZenStoreWriter::AddPendingPackage(const FName& PackageName)
{
	FScopeLock _(&PackagesCriticalSection);
	checkf(!PendingPackages.Contains(PackageName), TEXT("Trying to add package that is already pending"));
	TUniquePtr<FPendingPackageState>& Package = PendingPackages.Add(PackageName, MakeUnique<FPendingPackageState>());
	check(Package.IsValid());
	return *Package;
}

FCbPackage FZenStoreWriter::CreateReferencedSetOpPackage()
{
#if WITH_EDITOR
	TOptional<FZenFileSystemManifestEntry> ReferencedSet = ZenFileSystemManifest->GetReferencedSet();
	if (!ReferencedSet)
	{
		return FCbPackage();
	}
	FCbPackage Pkg;
	FCbWriter PackageObj;

	PackageObj.BeginObject();
	PackageObj << "key" << UE::Cook::GetReferencedSetOpName();
	PackageObj.BeginArray("files");
	WriteManifestEntryToPackageWriter(Pkg, PackageObj, *ReferencedSet);
	PackageObj.EndArray();
	PackageObj.EndObject();
	FCbObject Obj = PackageObj.Save().AsObject();

	Pkg.SetObject(Obj);
	return Pkg;
#else // else !WITH_EDITOR
	return FCbPackage();
#endif
}

FCbPackage FZenStoreWriter::CreateProjectMetaDataOpPackage(const ANSICHAR* MetadataOplogKeyName)
{
	FCbPackage Pkg;
	FCbWriter PackageObj;

	PackageObj.BeginObject();
	PackageObj << "key" << MetadataOplogKeyName;
	CreateProjectMetaData(Pkg, PackageObj);
	PackageObj.EndObject();

	Pkg.SetObject(PackageObj.Save().AsObject());
	return Pkg;
}

void FZenStoreWriter::CreateProjectMetaData(FCbPackage& Pkg, FCbWriter& PackageObj)
{
	// File Manifest
	{
		if (ZenFileSystemManifest->NumEntries() > 0)
		{
			TArrayView<FZenFileSystemManifestEntry const> Entries = ZenFileSystemManifest->ManifestEntries();

			PackageObj.BeginArray("files");
			for (const FZenFileSystemManifestEntry& NewEntry : Entries)
			{
				WriteManifestEntryToPackageWriter(Pkg, PackageObj, NewEntry);
			}
			PackageObj.EndArray();
		}

		FString ManifestPath = FPaths::Combine(MetadataDirectoryPath, TEXT("zenfs.manifest"));
		UE_LOG(LogZenStoreWriter, Display, TEXT("Saving Zen filesystem manifest '%s'"), *ManifestPath);
		ZenFileSystemManifest->Save(*ManifestPath);
	}

	// Metadata section
	{
		PackageObj.BeginArray("meta");

		// Summarize Script Objects
		FIoBuffer ScriptObjectsBuffer = PackageStoreOptimizer->CreateScriptObjectsBuffer();
		FCbObjectId ScriptOid = ToObjectId(CreateIoChunkId(0, 0, EIoChunkType::ScriptObjects));

		FCbAttachment ScriptAttachment = CreateAttachment(ScriptObjectsBuffer); 
		Pkg.AddAttachment(ScriptAttachment);

		PackageObj.BeginObject();
		PackageObj << "id" << ScriptOid;
		PackageObj << "name" << "ScriptObjects";
		PackageObj << "data" << ScriptAttachment;
		PackageObj.EndObject();

		PackageObj.EndArray();	// End of Meta array
	}
}

void FZenStoreWriter::WriteManifestEntryToPackageWriter(FCbPackage& Pkg, FCbWriter& PackageObj,
	const FZenFileSystemManifestEntry& Entry) const
{
	FCbObjectId FileOid = ToObjectId(Entry.FileChunkId);

	if (IsLocalConnection)
	{
		PackageObj.BeginObject();
		PackageObj << "id" << FileOid;
		PackageObj << "data" << FIoHash::Zero;
		PackageObj << "serverpath" << Entry.ServerPath;
		PackageObj << "clientpath" << Entry.ClientPath;
		PackageObj.EndObject();
	}
	else
	{
		const FString AbsPath = ZenFileSystemManifest->ServerRootPath() / Entry.ServerPath;
		TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*AbsPath, 0));
		if (Reader)
		{
			int64 TotalSize = Reader->TotalSize();
			if (TotalSize > 0)
			{
				FIoBuffer FileBuffer(TotalSize);
				Reader->Serialize(FileBuffer.GetData(), TotalSize);
				bool Success = Reader->Close();
				FCbAttachment FileAttachment = CreateAttachment(MoveTemp(FileBuffer));

				PackageObj.BeginObject();
				PackageObj << "id" << FileOid;
				PackageObj << "data" << FileAttachment;
				PackageObj << "serverpath" << Entry.ServerPath;
				PackageObj << "clientpath" << Entry.ClientPath;
				PackageObj.EndObject();

				Pkg.AddAttachment(FileAttachment);
			}
		}
	}
}

void FZenStoreWriter::BroadcastCommit(IPackageStoreWriter::FCommitEventArgs& EventArgs)
{
	FScopeLock CommitEventLock(&CommitEventCriticalSection);
	
	if (CommitEvent.IsBound())
	{
		FReadScopeLock _(EntriesLock);
		EventArgs.Entries = PackageStoreEntries;
		CommitEvent.Broadcast(EventArgs);
	}
}

void FZenStoreWriter::BroadcastMarkUpToDate(IPackageStoreWriter::FMarkUpToDateEventArgs& EventArgs)
{
	FScopeLock CommitEventLock(&CommitEventCriticalSection);

	if (MarkUpToDateEvent.IsBound())
	{
		FReadScopeLock _(EntriesLock);
		EventArgs.PlatformName = TargetPlatformFName;
		EventArgs.Entries = PackageStoreEntries;
		EventArgs.CookInfos = CookedPackagesInfo;
		MarkUpToDateEvent.Broadcast(EventArgs);
	}
}

FCbAttachment FZenStoreWriter::CreateAttachment(FSharedBuffer AttachmentData) const
{
	check(AttachmentData.GetSize() > 0);
	FCompressedBuffer CompressedBuffer = FCompressedBuffer::Compress(AttachmentData, Compressor, CompressionLevel);
	check(!CompressedBuffer.IsNull());
	return FCbAttachment(CompressedBuffer);
}

FCbAttachment FZenStoreWriter::CreateAttachment(FIoBuffer AttachmentData) const
{
	return CreateAttachment(IoBufferToSharedBuffer(AttachmentData));
}
