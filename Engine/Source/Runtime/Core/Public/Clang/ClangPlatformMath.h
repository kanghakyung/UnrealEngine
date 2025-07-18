// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================================
	ClangPlatformMath.h: Clang intrinsic implementations of some platform Math functions
==============================================================================================*/

#pragma once

#ifdef __clang__

#include "GenericPlatform/GenericPlatformMath.h"

// HEADER_UNIT_UNSUPPORTED - Clang not supporting header units

/**
 * Clang implementation of math functions
 **/
struct FClangPlatformMath : public FGenericPlatformMath
{
	/**
	 * Counts the number of leading zeros in the bit representation of the value
	 *
	 * @param Value the value to determine the number of leading zeros for
	 *
	 * @return the number of zeros before the first "on" bit
	 */
	static FORCEINLINE constexpr uint8 CountLeadingZeros8(uint8 Value)
	{
		return uint8(__builtin_clz((uint32(Value) << 1) | 1) - 23);
	}

	/**
	 * Counts the number of leading zeros in the bit representation of the value
	 *
	 * @param Value the value to determine the number of leading zeros for
	 *
	 * @return the number of zeros before the first "on" bit
	 */
	static FORCEINLINE constexpr uint32 CountLeadingZeros(uint32 Value)
	{
		return __builtin_clzll((uint64(Value) << 1) | 1) - 31;
	}

	/**
	 * Counts the number of leading zeros in the bit representation of the value
	 *
	 * @param Value the value to determine the number of leading zeros for
	 *
	 * @return the number of zeros before the first "on" bit
	 */
	static FORCEINLINE constexpr uint64 CountLeadingZeros64(uint64 Value)
	{
		if (Value == 0)
		{
			return 64;
		}

		return __builtin_clzll(Value);
	}

	/**
	 * Counts the number of trailing zeros in the bit representation of the value
	 *
	 * @param Value the value to determine the number of trailing zeros for
	 *
	 * @return the number of zeros after the last "on" bit
	 */
	static FORCEINLINE constexpr uint32 CountTrailingZeros(uint32 Value)
	{
		if (Value == 0)
		{
			return 32;
		}
	
		return (uint32)__builtin_ctz(Value);
	}

	static FORCEINLINE constexpr uint32 CountTrailingZerosConstExpr(uint32 Value)
	{
		return CountTrailingZeros(Value);
	}
	
	/**
	 * Counts the number of trailing zeros in the bit representation of the value
	 *
	 * @param Value the value to determine the number of trailing zeros for
	 *
	 * @return the number of zeros after the last "on" bit
	 */
	static FORCEINLINE constexpr uint64 CountTrailingZeros64(uint64 Value)
	{
		if (Value == 0)
		{
			return 64;
		}
	
		return (uint64)__builtin_ctzll(Value);
	}

	static FORCEINLINE constexpr uint64 CountTrailingZeros64ConstExpr(uint64 Value)
	{
		return CountTrailingZeros64(Value);
	}
	

	static FORCEINLINE constexpr uint32 FloorLog2(uint32 Value)
	{
		return 31 - __builtin_clz(Value | 1);
	}

	static FORCEINLINE constexpr uint32 FloorLog2NonZero(uint32 Value)
	{
		return 31 - __builtin_clz(Value);
	}

	static FORCEINLINE constexpr uint64 FloorLog2_64(uint64 Value)
	{
		return 63 - __builtin_clzll(Value | 1);
	}

	static FORCEINLINE constexpr uint64 FloorLog2NonZero_64(uint64 Value)
	{
		return 63 - __builtin_clzll(Value);
	}

	/**
	 * Adds two integers of any integer type, checking for overflow.
	 * If there was overflow, it returns false, and OutResult may or may not be written.
	 * If there wasn't overflow, it returns true, and the result of the addition is written to OutResult.
	 */
	template <
		typename IntType
		UE_REQUIRES(std::is_integral_v<IntType>)
	>
	static FORCEINLINE bool AddAndCheckForOverflow(IntType A, IntType B, IntType& OutResult)
	{
		return !__builtin_add_overflow(A, B, &OutResult);
	}

	/**
	 * Subtracts two integers of any integer type, checking for overflow.
	 * If there was overflow, it returns false, and OutResult may or may not be written.
	 * If there wasn't overflow, it returns true, and the result of the subtraction is written to OutResult.
	 */
	template <
		typename IntType
		UE_REQUIRES(std::is_integral_v<IntType>)
	>
	static FORCEINLINE bool SubtractAndCheckForOverflow(IntType A, IntType B, IntType& OutResult)
	{
		return !__builtin_sub_overflow(A, B, &OutResult);
	}

	/**
	 * Multiplies two integers of any integer type, checking for overflow.
	 * If there was overflow, it returns false, and OutResult may or may not be written.
	 * If there wasn't overflow, it returns true, and the result of the multiplication is written to OutResult.
	 */
	template <
		typename IntType
		UE_REQUIRES(std::is_integral_v<IntType>)
	>
	static FORCEINLINE bool MultiplyAndCheckForOverflow(IntType A, IntType B, IntType& OutResult)
	{
		return !__builtin_mul_overflow(A, B, &OutResult);
	}
};

#endif //~__clang__