// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <carbon/common/Common.h>
#include <carbon/simd/Simd.h>

CARBON_NAMESPACE_BEGIN(TITAN_NAMESPACE)

#if defined(CARBON_ENABLE_SSE) && CARBON_ENABLE_SSE

struct VertexPack128
{
    simde__m128 v0;
    simde__m128 v1;
    simde__m128 v2;
};

/**
 * @brief Converts ArrayOfStructures into StructuresOfArray for 4 vectors with 3 elements each.
 * [x1, y1, z1, x2], [y2, z2, x3, y3] [z3, x4, y4, z4]
 * =>
 * [x1, x2, x3, x4], [y1, y2, y3, y4] [z1, z2, z3, z4]
 *
 * This is useful to extract consecutive vertex data and unpack each vertex dimension.
 */
static inline VertexPack128 VertexUnpack(const simde__m128& v0, const simde__m128& v1, const simde__m128& v2)
{
    // Input:
    // v0: 00, 01, 02, 03
    // v1: 10, 11, 12, 13
    // v2: 20, 21, 22, 23

    // Output
    // v0n: 00, 03, 12, 21
    // v1n: 01, 10, 13, 22
    // v2n: 02, 11, 20, 23
    const simde__m128 tmp0 = simde_mm_shuffle_ps(v1, v2, SIMDE_MM_SHUFFLE(1, 0, 3, 2)); // 12, 13, 20, 21
	const simde__m128 v0n = simde_mm_shuffle_ps(v0, tmp0, SIMDE_MM_SHUFFLE(3, 0, 3, 0)); // 00, 03, 12, 21

    const simde__m128 tmp1 = simde_mm_shuffle_ps(v0, v1, SIMDE_MM_SHUFFLE(1, 0, 1, 0)); // 00, 01, 10, 11
    const simde__m128 tmp2 = simde_mm_shuffle_ps(v1, v2, SIMDE_MM_SHUFFLE(3, 2, 3, 2)); // 12, 13, 22, 23
    const simde__m128 v1n = simde_mm_shuffle_ps(tmp1, tmp2, SIMDE_MM_SHUFFLE(2, 1, 2, 1)); // 01, 10, 13, 22

    const simde__m128 tmp3 = simde_mm_shuffle_ps(v0, v1, SIMDE_MM_SHUFFLE(1, 0, 3, 2)); // 02, 03, 10, 11
    const simde__m128 v2n = simde_mm_shuffle_ps(tmp3, v2, SIMDE_MM_SHUFFLE(3, 0, 3, 0)); // 02, 11, 20, 23

    return { v0n, v1n, v2n };
}

#endif

#if defined(CARBON_ENABLE_AVX) && CARBON_ENABLE_AVX

struct VertexPack256
{
    simde__m256 v0;
    simde__m256 v1;
    simde__m256 v2;
};

/**
 * @brief Converts ArrayOfStructures into StructuresOfArray for 4 vectors with 3 elements each.
 * [x1, y1, z1, x2], [y2, z2, x3, y3] [z3, x4, y4, z4]
 * =>
 * [x1, x2, x3, x4], [y1, y2, y3, y4] [z1, z2, z3, z4]
 *
 * This is useful to extract consecutive vertex data and unpack each vertex dimension.
 */
static inline VertexPack256 VertexUnpack(const simde__m256& v0, const simde__m256& v1, const simde__m256& v2)
{
    // Input:
    // v0: 00, 01, 02, 03, 04, 05, 06, 07
    // v1: 10, 11, 12, 13, 14, 15, 16, 17
    // v2: 20, 21, 22, 23, 24, 25, 26, 27
    // Output:
    // v0n: 00, 03, 06, 11, 14, 17, 22, 25
    // v1n: 01, 04, 07, 12, 15, 20, 23, 26
    // v2n: 02, 05, 10, 13, 16, 21, 24, 27
    // create 6 __m128 and use VertexUnpack for __m128
    const simde__m128 p00 = simde_mm256_extractf128_ps(v0, 0);
    const simde__m128 p01 = simde_mm256_extractf128_ps(v0, 1);
    const simde__m128 p10 = simde_mm256_extractf128_ps(v1, 0);
    const simde__m128 p11 = simde_mm256_extractf128_ps(v1, 1);
    const simde__m128 p20 = simde_mm256_extractf128_ps(v2, 0);
    const simde__m128 p21 = simde_mm256_extractf128_ps(v2, 1);
    auto [r0, r1, r2] = VertexUnpack(p00, p01, p10);
    auto [r3, r4, r5] = VertexUnpack(p11, p20, p21);
    // concatenate __m128 back into __m256
    simde__m256 c0 = simde_mm256_castps128_ps256(r0);
    c0 = simde_mm256_insertf128_ps(c0, r3, 1);
    simde__m256 c1 = simde_mm256_castps128_ps256(r1);
    c1 = simde_mm256_insertf128_ps(c1, r4, 1);
    simde__m256 c2 = simde_mm256_castps128_ps256(r2);
    c2 = simde_mm256_insertf128_ps(c2, r5, 1);
    return { c0, c1, c2 };
}

#endif

template <typename T, int C>
std::tuple<_SimdType<T, C>, _SimdType<T, C>, _SimdType<T, C>> VertexUnpack(const _SimdType<T, C>& v0, const _SimdType<T, C>& v1, const _SimdType<T, C>& v2)
{
    auto unpacked = TITAN_NAMESPACE::VertexUnpack(v0.vec, v1.vec, v2.vec);
    return { _SimdType<T, C>(std::move(unpacked.v0)), _SimdType<T, C>(std::move(unpacked.v1)), _SimdType<T, C>(std::move(unpacked.v2)) };
}

CARBON_NAMESPACE_END(TITAN_NAMESPACE)
