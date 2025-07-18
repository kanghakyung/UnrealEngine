// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InstallBundleTypes.h"
#include "InstallBundleUtils.h"

#define UE_API DEFAULTINSTALLBUNDLEMANAGER_API

class FConfigFile;

#define INSTALL_BUNDLE_ALLOW_ERROR_SIMULATION (!UE_BUILD_SHIPPING)

class IInstallBundleSource;
struct FAnalyticsEventAttribute;
class IAnalyticsProviderET;
struct FInstallBundleCacheStats;

DECLARE_LOG_CATEGORY_EXTERN(LogDefaultInstallBundleManager, Display, All);

namespace InstallBundleManagerUtil
{
	DEFAULTINSTALLBUNDLEMANAGER_API TSharedPtr<IInstallBundleSource> MakeBundleSource(FInstallBundleSourceType Type);

#if WITH_PLATFORM_INSTALL_BUNDLE_SOURCE
	DEFAULTINSTALLBUNDLEMANAGER_API TSharedPtr<IInstallBundleSource> MakePlatformBundleSource();
#endif

	// Returns a thread pool with one thread suitible for running in-order journal tasks
	UE_DEPRECATED(5.4, "Use UE::Tasks::FPipe instead.")
	DEFAULTINSTALLBUNDLEMANAGER_API TSharedPtr<FQueuedThreadPool, ESPMode::ThreadSafe> GetJournalThreadPool();

	// Fills out a FInstallBundleSourceBundleInfo from the specified config section
	// Returns false if the provided config section is not a bundle definition section.
	DEFAULTINSTALLBUNDLEMANAGER_API bool LoadBundleSourceBundleInfoFromConfig(FInstallBundleSourceType SourceType, const FConfigFile& InstallBundleConfig, const FString& Section, FInstallBundleSourcePersistentBundleInfo& OutInfo);

	// Traverses bundle config sections and loads all dependencies for InBundleName, including InBundleName
	// Sets bSkippedUnknownBundles if a config section for InBundleName or a dependency can't be found.
	DEFAULTINSTALLBUNDLEMANAGER_API TSet<FName> GetBundleDependenciesFromConfig(FName InBundleName, TSet<FName>* SkippedUnknownBundles /*= nullptr*/);

	// adds all the known up to date bundles to the output array.
	DEFAULTINSTALLBUNDLEMANAGER_API void GetAllUpToDateBundlesFromConfg(const FConfigFile& InstallBundleConfig, TArray<FName>& OutBundles);

	// This class is a helper for parsing the buildinfo meta json file that should be either loadable from apk/ipa bundle or can be requested from CDN.
	// It will eventually be completely replaced by, possible key-value lookup from apk/ipa bundle, or by a call to FN service backend to request similar info.
#define JSON_MCI_VALUE(var) JSON_SERIALIZE(#var, var)
	class FContentBuildMetaData : public FJsonSerializable
	{
	public:
		BEGIN_JSON_SERIALIZER
			JSON_MCI_VALUE(AppName);
			JSON_MCI_VALUE(BuildVersion);
			JSON_MCI_VALUE(Platform);
			JSON_MCI_VALUE(ManifestPath);
			JSON_MCI_VALUE(ManifestHash);
		END_JSON_SERIALIZER

	public:
		FString AppName;
		FString BuildVersion;
		FString Platform;
		FString ManifestPath;
		FString ManifestHash;
	};
#undef JSON_MCI_VALUE

	DEFAULTINSTALLBUNDLEMANAGER_API void LogBundleRequestStats(const TCHAR* BundleName, const InstallBundleUtil::FContentRequestStats& RequestStats, ELogVerbosity::Type LogVerbosityOverride);

	class FPersistentStatContainer : public InstallBundleUtil::PersistentStats::FPersistentStatContainerBase
	{
	public:
		UE_API FPersistentStatContainer();
		UE_API virtual ~FPersistentStatContainer();

		//Adds the BundleNames to the given session's RequiredBundle list
		UE_API void AddRequiredBundlesForSession(const FString& SessionName, const TArray<FName>& BundleNames);

		UE_API void UpdateForContentState(const FInstallBundleCombinedContentState& ContentState, const FString& SessionName);
		UE_API void UpdateForBundleSource(const FInstallBundleSourceUpdateContentResultInfo& BundleSourceResult, FInstallBundleSourceType SourceType, const FString& BundleName);
		
	private:
		void SendEnteringBackgroundAnalytic();
		void SendEnteringForegroundAnalytic();
			
	public:
		//Helper struct to contain the information from a StatSession to pass into analytics cleaner
		struct FPersistentStatsInformation
		{
		public:
			FString SessionName;
			FString RequiredBundleNames;
			FString BundleStats;
			
			int NumBackgrounded;
			int NumResumedFromBackground;
			int NumResumedFromLaunch;
			
			double RealTotalTime;
			double ActiveTotalTime;
			double EstimatedTotalBGTime;
			double RealChunkDBDownloadTime;
			double ActiveChunkDBDownloadTime;
			double EstimatedBackgroundChunkDBDownloadTime;
			double ActiveInstallTime;
			double EstimatedBGInstallTime;
			double ActivePSOTime;
			double EstimatedBGPSOTime;
			
			bool bRequiresDownload;
			bool bRequiresInstall;
			
			FString BundleSourcesThatDidWork;
			
			FPersistentStatsInformation()
			: SessionName()
			, RequiredBundleNames()
			, BundleStats()
			, NumBackgrounded(0)
			, NumResumedFromBackground(0)
			, NumResumedFromLaunch(0)
			, RealTotalTime(0.)
			, ActiveTotalTime(0.)
			, EstimatedTotalBGTime(0.)
			, RealChunkDBDownloadTime(0.)
			, ActiveChunkDBDownloadTime(0.)
			, EstimatedBackgroundChunkDBDownloadTime(0.)
			, ActiveInstallTime(0.)
			, EstimatedBGInstallTime(0.)
			, ActivePSOTime(0.)
			, EstimatedBGPSOTime(0.)
			, bRequiresDownload(false)
			, bRequiresInstall(false)
			, BundleSourcesThatDidWork()
			{}
			
			//Puts this FPersistentStatsInformation's data into the provided analytics array
			void FillOutAnalyticsArrayWithData(TArray<FAnalyticsEventAttribute>& OutAnalyticsArray) const;
		};
		
	private:
		//Creates an FPersistentStatsInformation struct to pass into analytics
		FPersistentStatsInformation CalculatePersistentStatsInformationForSession(const FString& SessionName);
		
	private:
		
		//Helper struct to store data needed for our analytics
		struct FSessionAnalyticsData
		{
		public:
			bool bRequiresDownload;
			bool bRequiresInstall;
			bool bShouldSendBGAnalyticsSessionMap;
			
			FSessionAnalyticsData()
			: bRequiresDownload(false)
			, bRequiresInstall(false)
			, bShouldSendBGAnalyticsSessionMap(false)
			{}
			
			void ResetShouldSendBGAnalytics()
			{
				//Always reset our value to send BG analytics if we are either downloading or installing data.
				//Shouldn't send otherwise as we aren't doing anything we care about the analytics for anymore.
				bShouldSendBGAnalyticsSessionMap = bRequiresDownload || bRequiresInstall;
			}
		};
		TMap<FString, FSessionAnalyticsData> SessionAnalyticsDataMap;
		
		//Helper struct to store data needed for our analytics on bundle persistent stats
		struct FBundleAnalyticsData
		{
		public:
			//Store if each bundle source type did any work for this bundle. If its in this map it did work.
			TSet<FInstallBundleSourceType> BundleSourcesThatDidWorkMap;
			
			FBundleAnalyticsData()
			: BundleSourcesThatDidWorkMap()
			{}
		};
		TMap<FString, FBundleAnalyticsData> BundleAnalyticsDataMap;
		
	public:
		FString BundleSourcesThatDidWork;
		
	//FPersistentStatContainerBase Overrides
	public:
		UE_API virtual void StartSessionPersistentStatTracking(const FString& SessionName, const TArray<FName>& RequiredBundles = TArray<FName>(), const FString& ExpectedAnalyticsID = FString(), bool bForceResetStatData = false) override;
		UE_API virtual void StopSessionPersistentStatTracking(const FString& SessionName, bool bStopAllActiveTimers = true) override;
		
		UE_API virtual void RemoveSessionStats(const FString& SessionName) override;
		UE_API virtual void RemoveBundleStats(FName BundleName) override;
		
	protected:
		UE_API virtual void OnApp_EnteringBackground() override;
		UE_API virtual void OnApp_EnteringForeground() override;
	};
}

namespace InstallBundleManagerAnalytics
{
	struct FBundleHeartbeatStats
	{
		FName BundleName;
		FString LastStatusText{ TEXT("Unknown") };

		float Finishing_Percent = 0.0f;
		float Install_Percent = 0.0f;

		EInstallBundleResult LastErrorResult = EInstallBundleResult::OK;
		EInstallBundlePauseFlags PauseFlags = EInstallBundlePauseFlags::None;

		bool bIsComplete = false;
	};


	/**
	 * @EventName InstallBundleManager.InitComplete
	 * @Trigger Bundle Manager finished async initialization
	 * @Type Client
	 * @EventParam CanRetry Bool True if initialization can be retried if it failed
	 * @EventParam InitResultString String Result code
	 * @Comments
	 */
	DEFAULTINSTALLBUNDLEMANAGER_API void FireEvent_InitBundleManagerComplete(IAnalyticsProviderET* AnalyticsProvider,
		const bool bCanRetry,
		const FString InitResultString);

	/**
	 * @EventName InstallBundleManager.CacheStats
	 * @Trigger Bundle Manager finished async initialization successfully
	 * @Type Client
	 * @EventParam CacheName (string)
	 * @EventParam MaxSize (uint64) Configured size of the cache
	 * @EventParam UsedSize (uint64) Amount of the cache used by bundles associated with this cache
	 * @EventParam ReservedSize (uint64) Amount the cache taken up bundles that cannot be evicted
	 * @EventParam FreeSize (uint64) Amount of the cache not being used
	 * @Comments Values may represent content that is not yet committed to or removed from the disk
	 */
	DEFAULTINSTALLBUNDLEMANAGER_API void FireEvent_BundleManagerCacheStats(IAnalyticsProviderET* AnalyticsProvider, const FInstallBundleCacheStats& Stats);

	/**
	* @EventName InstallBundleManager.InitBundleSourceBulkComplete
	* @Trigger Bundle Manager finished async initialization
	* @Type Client
	* @EventParam InitResultString String Result code
	* @Comments
	*/
	DEFAULTINSTALLBUNDLEMANAGER_API void FireEvent_InitBundleSourceBulkComplete(IAnalyticsProviderET* AnalyticsProvider,
		const FString InitResultString);

	/**
	* @EventName InstallBundleManager.InitBundleSourcePlayGoComplete
	* @Trigger Bundle Manager finished async initialization
	* @Type Client
	* @EventParam InitResultString String Result code
	* @Comments
	*/
	DEFAULTINSTALLBUNDLEMANAGER_API void FireEvent_InitBundleSourcePlayGoComplete(IAnalyticsProviderET* AnalyticsProvider,
		const FString InitResultString);

	/**
	* @EventName InstallBundleManager.InitBundleSourceIntelligentDeliveryComplete
	* @Trigger Bundle Manager finished async initialization
	* @Type Client
	* @EventParam InitResultString String Result code
	* @Comments
	*/
	DEFAULTINSTALLBUNDLEMANAGER_API void FireEvent_InitBundleSourceIntelligentDeliveryComplete(IAnalyticsProviderET* AnalyticsProvider,
		const FString InitResultString);

	/**
	* @EventName InstallBundleManager.FireEvent_InitBundleSourcePlatformChunkInstallComplete
	* @Trigger Bundle Manager finished async initialization
	* @Type Client
	* @EventParam InitResultString String Result code
	* @Comments
	*/
	DEFAULTINSTALLBUNDLEMANAGER_API void FireEvent_InitBundleSourcePlatformChunkInstallComplete(IAnalyticsProviderET* AnalyticsProvider,
		const FString InitResultString);

	/**
	 * @EventName InstallBundleManager.BundleLatestClientCheckComplete
	 *
	 * @Type Client
	 *
	 * @EventParam BundleName (string) String Name of bundle being installed.
	 * @EventParam AppVersion (string) Version of the client app
	 * @EventParam SkippedCheck (bool) True if the check was skipped
	 * @EventParam SkipReason (string) If skipped, the reason
	 * @EventParam ShouldPatch (bool) True if we are going to proceed with patching content
	 * @EventParam RequestFailed (bool) True if the patch check request failed
	 *
	 * @Comments
	 *
	 */
	DEFAULTINSTALLBUNDLEMANAGER_API void FireEvent_BundleLatestClientCheckComplete(IAnalyticsProviderET* AnalyticsProvider,
		const FString& BundleName,
		bool bSkippedCheck,
		FString SkipReason,
		bool bShouldPatch,
		bool bRequestFailed);

	/**
	 * @EventName InstallBundleManager.BundleRequestStarted
	 *
	 * @Trigger Fired when a bundle request is started
	 *
	 * @Type Client
	 *
	 * @EventParam BundleName (string) String Name of bundle being installed.
	 *
	 */
	DEFAULTINSTALLBUNDLEMANAGER_API void FireEvent_BundleRequestStarted(IAnalyticsProviderET* AnalyticsProvider,
		const FString& BundleName);

	/**
	 * @EventName InstallBundleManager.BundleRequestComplete
	 *
	 * @Trigger Fired after an install bundle request is completed
	 *
	 * @Type Client
	 *
	 * @EventParam BundleName (string) String Name of bundle being installed.
	 * @EventParam DidInstall (bool) True if the bundle was installed successfully
	 * @EventParam Result (string) Result of the request
	 * @EventParam TimingStates (double) A variable number of stats based on what bundle manager steps were run. Fields end in "_Time"
	 */
	DEFAULTINSTALLBUNDLEMANAGER_API void FireEvent_BundleRequestComplete(IAnalyticsProviderET* AnalyticsProvider,
		const FString& BundleName,
		bool bDidInstall,
		const FString& Result,
		const InstallBundleUtil::FContentRequestStats& TimingStats);

	/**
	 * @EventName InstallBundleManager.BundleReleaseRequestStarted
	 *
	 * @Trigger Fired when a bundle release request is started
	 *
	 * @Type Client
	 *
	 * @EventParam BundleName (string) String Name of bundle.
	 * @EventParam RemoveFilesIfPossible (bool) True if this request will try to clean up files.
	 * @EventParam UnmountOnly (bool) True if this request is only for unmount
	 */
	DEFAULTINSTALLBUNDLEMANAGER_API void FireEvent_BundleReleaseRequestStarted(IAnalyticsProviderET* AnalyticsProvider,
		const FString& BundleName,
		bool bRemoveFilesIfPossible,
		bool bUnmountOnly);

	/**
	 * @EventName InstallBundleManager.BundleReleaseRequestComplete
	 *
	 * @Trigger Fired when a bundle release request is complete
	 *
	 * @Type Client
	 *
	 * @EventParam BundleName (string) String Name of bundle.
	 * @EventParam RemoveFilesIfPossible (bool) True if this request will try to clean up files.
	 * @EventParam UnmountOnly (bool) True if this request is only for unmount
	 * @EventParam Result (string) Result of the request
	 */
	DEFAULTINSTALLBUNDLEMANAGER_API void FireEvent_BundleReleaseRequestComplete(IAnalyticsProviderET* AnalyticsProvider,
		const FString& BundleName,
		bool bRemoveFilesIfPossible,
		bool bUnmountOnly,
		const FString& Result);

	/**
	 * @EventName InstallBundleManager.BundleEvictedFromCache
	 *
	 * @Trigger Fired after a cached bundle's content has been removed from disk
	 *
	 * @Type Client
	 *
	 * @EventParam BundleName (string) Name of bundle.
	 * @EventParam BundleSource (string) Source the bundle's content was removed from
	 * @EventParam LastAccessTime (string) Format yyyy.mm.dd-hh.mm.ss, last access time for the BundleSource specified.
	 * @EventParam BundleAgeHours (double) Amount of time in hours bundle content has been installed, measured from LastAccessTime.
	 * @EventParam Result (string) Result of the request, OK or an error code
	 */
	DEFAULTINSTALLBUNDLEMANAGER_API void FireEvent_BundleEvictedFromCache(IAnalyticsProviderET* AnalyticsProvider,
		const FString& BundleName, 
		const FString& BundleSource,
		FDateTime LastAccessTime, 
		const FString& Result);

	/**
	 * @EventName InstallBundleManager.BundleCacheHit
	 *
	 * @Trigger Fired after a cached bundle's source completes it's update
	 *
	 * @Type Client
	 *
	 * @EventParam BundleName (string) Name of bundle.
	 * @EventParam BundleSource (string) Source of the bundle's content
	 */
	DEFAULTINSTALLBUNDLEMANAGER_API void FireEvent_BundleCacheHit(IAnalyticsProviderET* AnalyticsProvider,
								  const FString& BundleName, const FString& BundleSource);

	/**
	 * @EventName InstallBundleManager.BundleCacheMiss
	 *
	 * @Trigger Fired after a cached bundle's source completes it's update
	 *
	 * @Type Client
	 *
	 * @EventParam BundleName (string) Name of bundle.
	 * @EventParam BundleSource (string) Source the bundle's content
	 * @EventParam PatchRequired (bool) If true, this miss was because we had to patch the bundle.  If false, the miss was because the bundle was not in the cache
	 */
	DEFAULTINSTALLBUNDLEMANAGER_API void FireEvent_BundleCacheMiss(IAnalyticsProviderET* AnalyticsProvider,
								   const FString& BundleName, const FString& BundleSource, bool bPatchRequired);

	/*
	 * @EventName PersistentPatchStats.StartPatching
	 *
	 * @Trigger Fired whenever we begin patching for a particular session on every update start for each launch of the app.
	 *
	 * @Type Client
	 *
	 * @EventParam SessionName (string) Name of the session these stats correspond too.
	 * @EventParam RequiredBundleNames (string) List of bundles that were required to be patched as part of this session.
	 * @EventParam BundleStats (string) JSON array of all of the stats data for all bundles in the RequiredBundleNames array for this session.
	 *
	 * @EventParam NumBackgrounded (int) Number of times the application was backgrounded during this patch.
	 * @EventParam NumResumedFromBackground (int) Number of times the patch was resumed from returning to foreground from background (does not count initial app launches).
	 * @EventParam NumResumedFromLaunch (int) Number of times the patch was resumed from launching the app fresh (instead of from coming back from background).
	 *
	 * @EventParam RealTotalTime (double)
	 * @EventParam ActiveTotalTime (double)
	 * @EventParam EstimatedTotalBGTime (double)
	 * @EventParam RealChunkDBDownloadTime (double)
	 * @EventParam ActiveChunkDBDownloadTime (double)
	 * @EventParam EstimatedBackgroundChunkDBDownloadTime (double)
	 * @EventParam ActiveInstallTime (double)
	 * @EventParam EstimatedBGInstallTime (double)
	 * @EventParam ActivePSOTime (double)
	 * @EventParam EstimatedBGPSOTime (double)
	 *
	 * @EventParam bRequiresInstall (bool) If the update will require some amount of install work
	 * @EventParam bRequiresDownload (bool) If the update will require downloading some chunk data
	 * @EventParam bRequiresUpdate (bool) If the update we are tracking requires downloading or installing data
	 *
	 * @EventParam BundleSourcesThatDidWork (string) List of bundle sources that did work as part of one of the bundles for this session
	 *
	 * @Comments
	 *
	 */
	DEFAULTINSTALLBUNDLEMANAGER_API void FireEvent_PersistentPatchStats_StartPatching(IAnalyticsProviderET* AnalyticsProvider, const InstallBundleManagerUtil::FPersistentStatContainer::FPersistentStatsInformation& PersistentStatInformation);

	/*
	 * @EventName PersistentPatchStats.EndPatching
	 *
	 * @Trigger Fired whenever we have finished patching for a particular session
	 *
	 * @Type Client
	 *
	 * @EventParam SessionName (string) Name of the session these stats correspond too.
	 * @EventParam RequiredBundleNames (string) List of bundles that were required to be patched as part of this session.
	 * @EventParam BundleStats (string) JSON array of all of the stats data for all bundles in the RequiredBundleNames array for this session.
	 *
	 * @EventParam NumBackgrounded (int) Number of times the application was backgrounded during this patch.
	 * @EventParam NumResumedFromBackground (int) Number of times the patch was resumed from returning to foreground from background (does not count initial app launches).
	 * @EventParam NumResumedFromLaunch (int) Number of times the patch was resumed from launching the app fresh (instead of from coming back from background).
	 *
	 * @EventParam RealTotalTime (double)
	 * @EventParam ActiveTotalTime (double)
	 * @EventParam EstimatedTotalBGTime (double)
	 * @EventParam RealChunkDBDownloadTime (double)
	 * @EventParam ActiveChunkDBDownloadTime (double)
	 * @EventParam EstimatedBackgroundChunkDBDownloadTime (double)
	 * @EventParam ActiveInstallTime (double)
	 * @EventParam EstimatedBGInstallTime (double)
	 * @EventParam ActivePSOTime (double)
	 * @EventParam EstimatedBGPSOTime (double)
	 *
	 * @EventParam bRequiresInstall (bool) If the update will require some amount of install work
	 * @EventParam bRequiresDownload (bool) If the update will require downloading some chunk data
	 * @EventParam bRequiresUpdate (bool) If the update we are tracking requires downloading or installing data
	 *
	 * @EventParam BundleSourcesThatDidWork (string) List of bundle sources that did work as part of one of the bundles for this session
	 *
	 * @Comments
	 *
	 */
	DEFAULTINSTALLBUNDLEMANAGER_API void FireEvent_PersistentPatchStats_EndPatching(IAnalyticsProviderET* AnalyticsProvider, const InstallBundleManagerUtil::FPersistentStatContainer::FPersistentStatsInformation& PersistentStatInformation);

	/*
	 * @EventName PersistentPatchStats.Background
	 *
	 * @Trigger Fired whenever we background the app during the patching process
	 *
	 * @Type Client
	 *
	 * @EventParam SessionName (string) Name of the session these stats correspond too.
	 * @EventParam RequiredBundleNames (string) List of bundles that were required to be patched as part of this session.
	 * @EventParam BundleStats (string) JSON array of all of the stats data for all bundles in the RequiredBundleNames array for this session.
	 *
	 * @EventParam NumBackgrounded (int) Number of times the application was backgrounded during this patch.
	 * @EventParam NumResumedFromBackground (int) Number of times the patch was resumed from returning to foreground from background (does not count initial app launches).
	 * @EventParam NumResumedFromLaunch (int) Number of times the patch was resumed from launching the app fresh (instead of from coming back from background).
	 *
	 * @EventParam RealTotalTime (double)
	 * @EventParam ActiveTotalTime (double)
	 * @EventParam EstimatedTotalBGTime (double)
	 * @EventParam RealChunkDBDownloadTime (double)
	 * @EventParam ActiveChunkDBDownloadTime (double)
	 * @EventParam EstimatedBackgroundChunkDBDownloadTime (double)
	 * @EventParam ActiveInstallTime (double)
	 * @EventParam EstimatedBGInstallTime (double)
	 * @EventParam ActivePSOTime (double)
	 * @EventParam EstimatedBGPSOTime (double)
	 *
	 * @EventParam bRequiresInstall (bool) If the update will require some amount of install work
	 * @EventParam bRequiresDownload (bool) If the update will require downloading some chunk data
	 * @EventParam bRequiresUpdate (bool) If the update we are tracking requires downloading or installing data
	 *
	 * @EventParam BundleSourcesThatDidWork (string) List of bundle sources that did work as part of one of the bundles for this session
	 *
	 * @Comments
	 *
	 */
	DEFAULTINSTALLBUNDLEMANAGER_API void FireEvent_PersistentPatchStats_Background(IAnalyticsProviderET* AnalyticsProvider, const InstallBundleManagerUtil::FPersistentStatContainer::FPersistentStatsInformation& PersistentStatInformation);

	/*
	 * @EventName PersistentPatchStats.Foreground
	 *
	 * @Trigger Fired whenever we return from background into the foreground during the patching process
	 *
	 * @Type Client
	 *
	 * @EventParam SessionName (string) Name of the session these stats correspond too.
	 * @EventParam RequiredBundleNames (string) List of bundles that were required to be patched as part of this session.
	 * @EventParam BundleStats (string) JSON array of all of the stats data for all bundles in the RequiredBundleNames array for this session.
	 *
	 * @EventParam NumBackgrounded (int) Number of times the application was backgrounded during this patch.
	 * @EventParam NumResumedFromBackground (int) Number of times the patch was resumed from returning to foreground from background (does not count initial app launches).
	 * @EventParam NumResumedFromLaunch (int) Number of times the patch was resumed from launching the app fresh (instead of from coming back from background).
	 *
	 * @EventParam RealTotalTime (double)
	 * @EventParam ActiveTotalTime (double)
	 * @EventParam EstimatedTotalBGTime (double)
	 * @EventParam RealChunkDBDownloadTime (double)
	 * @EventParam ActiveChunkDBDownloadTime (double)
	 * @EventParam EstimatedBackgroundChunkDBDownloadTime (double)
	 * @EventParam ActiveInstallTime (double)
	 * @EventParam EstimatedBGInstallTime (double)
	 * @EventParam ActivePSOTime (double)
	 * @EventParam EstimatedBGPSOTime (double)
	 *
	 * @EventParam bRequiresInstall (bool) If the update will require some amount of install work
	 * @EventParam bRequiresDownload (bool) If the update will require downloading some chunk data
	 * @EventParam bRequiresUpdate (bool) If the update we are tracking requires downloading or installing data
	 *
	 * @EventParam BundleSourcesThatDidWork (string) List of bundle sources that did work as part of one of the bundles for this session
	 *
	 * @Comments
	 *
	 */
	DEFAULTINSTALLBUNDLEMANAGER_API void FireEvent_PersistentPatchStats_Foreground(IAnalyticsProviderET* AnalyticsProvider, const InstallBundleManagerUtil::FPersistentStatContainer::FPersistentStatsInformation& PersistentStatInformation);
}

#undef UE_API
