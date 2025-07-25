// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <carbon/Common.h>
#include <nls/math/Math.h>
#include <nls/serialization/BinarySerialization.h>

CARBON_NAMESPACE_BEGIN(TITAN_NAMESPACE)

/**
 * Representation of barycentric coordinates of a point in a triangle (C=3) or tetrahedron (C=4).
 * Note: negative barycentric coordinates are supported and enable extrapolation outside of the tri/tet.
 */
template <class T, int C = 3>
class BarycentricCoordinates
{
static_assert(C >= 1);

public:
    BarycentricCoordinates()
    {
        m_indices.setZero();
        m_weights.setZero();
    }

    BarycentricCoordinates(const Eigen::Vector<int, C>& indices, const Eigen::Vector<T, C>& weights)
        : m_indices(indices)
        , m_weights(weights)
    {}

    template <int R>
    Eigen::Vector<T, R> Evaluate(const Eigen::Ref<const Eigen::Matrix<T, R, -1>>& vertices) const
    {
        Eigen::Vector<T, R> res = Weight(0) * vertices.col(Index(0));
        for (int i = 1; i < C; i++)
        {
            res += Weight(i) * vertices.col(Index(i));
        }
        return res;
    }

    int Index(int i) const
    {
        return m_indices[i];
    }

    T Weight(int i) const
    {
        return m_weights[i];
    }

    const Eigen::Vector<int, C>& Indices() const
    {
        return m_indices;
    }

    const Eigen::Vector<T, C>& Weights() const
    {
        return m_weights;
    }

    static BarycentricCoordinates SingleVertex(int vID)
    {
        Eigen::Vector<int, C> indices;
        for (int i = 0; i < C; i++)
        {
            indices[i] = vID;
        }

        Eigen::Vector<T, C> weights = Eigen::Vector<T, C>::Zero();
        weights[0] = T(1.0);

        return BarycentricCoordinates(indices, weights);
    }

    bool operator==(const BarycentricCoordinates& other) const
    {
        return (m_indices == other.m_indices) && (m_weights == other.m_weights);
    }

    //! Computes barycentric coordinates of point 'p' w.r.t. a triangle/tet:
    static Eigen::Vector<T, C> ComputeBarycentricCoordinates(const Eigen::Vector<T, 3>& p,
                                                             const Eigen::Vector<int, C>& indices,
                                                             const Eigen::Matrix<T, 3, -1>& vertices);

    bool Nonnegative() const
    {
        for (int i = 0; i < C; i++)
        {
            if (m_weights[i] < T(0.0)) { return false; } }
        return true;
    }

    template <class T2, int C2>
    friend bool ToBinaryFile(FILE* pFile, const BarycentricCoordinates<T2, C2>& bcs);
    template <class T2, int C2>
    friend bool FromBinaryFile(FILE* pFile, BarycentricCoordinates<T2, C2>& bcs);

private:
    static constexpr int32_t m_version = 1;
    Eigen::Vector<int, C> m_indices;
    Eigen::Vector<T, C> m_weights;
};

template <class T, int C = 3>
class BarycentricCoordinatesExt : public BarycentricCoordinates<T, C>
{
    public:
        BarycentricCoordinatesExt() : BarycentricCoordinates<T, C>()
        {
            m_triangle = 0;
        }

        BarycentricCoordinatesExt(const Eigen::Vector<int, C>& indices, const Eigen::Vector<T, C>& weights, const int triangle) : BarycentricCoordinates<T, C>(indices, weights)
        {
            m_triangle = triangle;
        }

        int Triangle() const{
            return m_triangle;
        }

    private:
        int m_triangle;
};


template <typename T, int C = 3>
bool ToBinaryFile(FILE* pFile, const BarycentricCoordinates<T, C>& bcs)
{
    bool success = true;
    success &= success &= io::ToBinaryFile<int32_t>(pFile, bcs.m_version);
    success &= io::ToBinaryFile(pFile, bcs.m_indices);
    success &= io::ToBinaryFile(pFile, bcs.m_weights);
    return success;
}

template <typename T, int C = 3>
bool FromBinaryFile(FILE* pFile, BarycentricCoordinates<T, C>& bcs)
{
    bool success = true;
    int32_t version;
    success &= io::FromBinaryFile(pFile, version);
    if (success && version == 1)
    {
        success &= io::FromBinaryFile(pFile, bcs.m_indices);
        success &= io::FromBinaryFile(pFile, bcs.m_weights);
    }
    else
    {
        success = false;
    }
    return success;
}


CARBON_NAMESPACE_END(TITAN_NAMESPACE)
