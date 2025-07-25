// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"
#include "CoreTypes.h"
#include "HAL/PlatformCrt.h"

#if PLATFORM_HAS_BSD_TIME 
	#include <sys/time.h> // IWYU pragma: export
#endif


/** Contains CPU utilization data. */
struct FCPUTime
{
	/** Initialization constructor. */
	FCPUTime( float InCPUTimePct, float InCPUTimePctRelative ) 
		: CPUTimePct( InCPUTimePct )
		, CPUTimePctRelative( InCPUTimePctRelative )
	{}

	FCPUTime& operator+=( const FCPUTime& Other )
	{
		CPUTimePct += Other.CPUTimePct;
		CPUTimePctRelative += Other.CPUTimePctRelative;
		return *this;
	}

	/** Percentage CPU utilization for the last interval. */
	float CPUTimePct;

	/** Percentage CPU utilization for the last interval relative to one core, 
	 ** so if CPUTimePct is 8.0% and you have 6 core this value will be 48.0%. */
	float CPUTimePctRelative;
};


/**
* Generic implementation for most platforms
**/
struct FGenericPlatformTime
{
#if PLATFORM_HAS_BSD_TIME 
	/**
	 * Does per platform initialization of timing information and returns the current time. This function is
	 * called before the execution of main as GStartTime is statically initialized by it. The function also
	 * internally sets SecondsPerCycle, which is safe to do as static initialization order enforces complex
	 * initialization after the initial 0 initialization of the value.
	 *
	 * @return	current time
	 */
	static CORE_API double InitTiming();

	static FORCEINLINE double Seconds()
	{
		struct timeval tv;
		gettimeofday( &tv, NULL );
		return ((double) tv.tv_sec) + (((double) tv.tv_usec) / 1000000.0);
	}

	static FORCEINLINE uint32 Cycles()
	{
		struct timeval tv;
		gettimeofday( &tv, NULL );
		return (uint32) ((((uint64)tv.tv_sec) * 1000000ULL) + (((uint64)tv.tv_usec)));
	}
	static FORCEINLINE uint64 Cycles64()
	{
		struct timeval tv;
		gettimeofday( &tv, NULL );
		return ((((uint64)tv.tv_sec) * 1000000ULL) + (((uint64)tv.tv_usec)));
	}

	/** Returns the system time. */
	static CORE_API void SystemTime( int32& Year, int32& Month, int32& DayOfWeek, int32& Day, int32& Hour, int32& Min, int32& Sec, int32& MSec );

	/** Returns the UTC time. */
	static CORE_API void UtcTime( int32& Year, int32& Month, int32& DayOfWeek, int32& Day, int32& Hour, int32& Min, int32& Sec, int32& MSec );
#endif

	/**
	 * Get the system date
	 * 
	 * @param Dest Destination buffer to copy to
	 * @param DestSize Size of destination buffer in characters
	 * @return Date string in the format "MM/DD/YY"
	 */
	static CORE_API TCHAR* StrDate( TCHAR* Dest, SIZE_T DestSize );
	/**
	 * Get the system time
	 * 
	 * @param Dest Destination buffer to copy to
	 * @param DestSize Size of destination buffer in characters
	 * @return Time string in the format "HH::mm::ss"
	 */
	static CORE_API TCHAR* StrTime( TCHAR* Dest, SIZE_T DestSize );

	/**
	 * Returns a timestamp string built from the current date and time.
	 * NOTE: Only one return value is valid at a time!
	 *
	 * @return timestamp string in the format "MM/DD/YY HH::mm::ss"
	 */
	static CORE_API const TCHAR* StrTimestamp();

	/**
	 * Returns a pretty-string for a time given in seconds. (I.e. "4:31 min", "2:16:30 hours", etc)
	 *
	 * @param Seconds Time in seconds
	 * @return Time in a pretty formatted string
	 */
	static CORE_API FString PrettyTime( double Seconds );

	/** Updates CPU utilization, called through a delegate from the Core ticker. */
	static bool UpdateCPUTime( float DeltaTime )
	{
		return false;
	}

	/** Updates current thread CPU utilization, calling is user defined per-thread (unused float parameter, is for FTicker compatibility). */
	static bool UpdateThreadCPUTime(float = 0.0)
	{
		return false;
	}

	/** Registers automatic updates of Game Thread CPU utilization */
	static void AutoUpdateGameThreadCPUTime(double UpdateInterval)
	{
	}

	/**
	 * @return structure that contains CPU utilization data.
	 */
	static FCPUTime GetCPUTime()
	{
		return FCPUTime( 0.0f, 0.0f );
	}

	/**
	 * Gets current threads CPU Utilization
	 *
	 * @return	Current threads CPU Utilization
	 */
	static FCPUTime GetThreadCPUTime()
	{
		return FCPUTime(0.0f, 0.0f);
	}

	/**
	 * @return the cpu processing time (kernel + user time of all threads) from the last update
	 */
	static double GetLastIntervalCPUTimeInSeconds()
	{
		return LastIntervalCPUTimeInSeconds;
	}

	/**
	 * Gets the per-thread CPU processing time (kernel + user) from the last update
	 *
	 * @return	The per-thread CPU processing time from the last update
	 */
	static double GetLastIntervalThreadCPUTimeInSeconds()
	{
		return 0.0;
	}

	/**
	 * Each platform implements these two functions, which return the current time in platform-specific cpu cycles.
	 * Cycles64 should be used for most purposes as it may be higher resolution and will not roll over during execution:
	 * 
	 * static uint32 Cycles();
	 * static uint64 Cycles64();
	 */

	/** Returns seconds per cycle, to pair with Cycles(). */
	static double GetSecondsPerCycle()
	{
		return SecondsPerCycle;
	}

	/** Converts cycles to milliseconds. */
	static float ToMilliseconds( const uint32 Cycles )
	{
		return (float)double( SecondsPerCycle * 1000.0 * Cycles );
	}

	/** Converts cycles to seconds. */
	static float ToSeconds( const uint32 Cycles )
	{
		return (float)double( SecondsPerCycle * Cycles );
	}


	/** Returns seconds per cycle, to pair with Cycles64(). */
	static double GetSecondsPerCycle64()
	{
		return SecondsPerCycle64;
	}

	/** Converts 64 bit cycles to milliseconds. */
	static double ToMilliseconds64(const uint64 Cycles)
	{
		return ToSeconds64(Cycles) * 1000.0;
	}

	/** Converts 64 bit cycles to seconds. */
	static double ToSeconds64(const uint64 Cycles)
	{
		return GetSecondsPerCycle64() * double(Cycles);
	}

	/** Convert seconds to cycles, can be added to Cycles64 to set a high resolution timeout */
	static uint64 SecondsToCycles64(double Seconds)
	{
		return static_cast<uint64>(Seconds / GetSecondsPerCycle64());
	}

protected:

	static CORE_API double SecondsPerCycle;
	static CORE_API double SecondsPerCycle64;
	static CORE_API double LastIntervalCPUTimeInSeconds;
};
