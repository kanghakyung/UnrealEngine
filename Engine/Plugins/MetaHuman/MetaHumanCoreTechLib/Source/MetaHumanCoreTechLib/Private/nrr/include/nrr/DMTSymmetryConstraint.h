// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <nls/DiffDataMatrix.h>

CARBON_NAMESPACE_BEGIN(TITAN_NAMESPACE)

template <class T>
class DMTSymmetryConstraint
{
public:
    DiffData<T> EvaluateSymmetry(const DiffData<T>& vec, int numRegions, std::vector<std::pair<int, int>> vecPair)
    {
        const int numValues = vec.Size();
        const int numCharacters = numValues / numRegions;
        const int numPairs = static_cast<int>(vecPair.size());

        Vector<T> result(numPairs * numCharacters);
        for (int i = 0; i < numCharacters; i++)
        {
            for (int j = 0; j < numPairs; j++)
            {
                result[j * numCharacters + i] = vec.Value()[i * numRegions + vecPair[j].first] - vec.Value()[i * numRegions + vecPair[j].second];
            }
        }

        JacobianConstPtr<T> Jacobian;
        if (vec.HasJacobian() && (vec.Jacobian().NonZeros() > 0))
        {
            std::vector<Eigen::Triplet<T>> triplets;
            triplets.reserve(numPairs * numCharacters * 2);
            for (int i = 0; i < numCharacters; ++i)
            {
                for (int j = 0; j < numPairs; ++j)
                {
                    triplets.push_back(Eigen::Triplet<T>(j * numCharacters + i, i * numRegions + vecPair[j].first, 1));
                    triplets.push_back(Eigen::Triplet<T>(j * numCharacters + i, i * numRegions + vecPair[j].second, -1));
                }
            }

            SparseMatrix<T> localJacobian(numPairs * numCharacters, numValues);
            localJacobian.setFromTriplets(triplets.begin(), triplets.end());
            Jacobian = vec.Jacobian().Premultiply(localJacobian);
        }

        return DiffData<T>(std::move(result), Jacobian);
    }
};

CARBON_NAMESPACE_END(TITAN_NAMESPACE)
