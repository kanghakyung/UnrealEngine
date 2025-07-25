// Copyright Epic Games, Inc. All Rights Reserved.

#include <nls/geometry/BarycentricEmbedding.h>

#include <carbon/io/JsonIO.h>
#include <carbon/io/Utils.h>
#include <carbon/utils/TaskThreadPool.h>

CARBON_NAMESPACE_BEGIN(TITAN_NAMESPACE)

template <class T>
void BarycentricEmbedding<T>::SetBarycentricEmbedding(const Eigen::Matrix<T, 3, -1>& points, const TetMesh<T>& tetMesh)
{
    auto threadPool = TITAN_NAMESPACE::TaskThreadPool::GlobalInstance(/*createIfNotAvailable=*/true);

    const int numPoints = int(points.cols());
    m_barycentricCoordinates.resize(size_t(numPoints));

    const int numTets = tetMesh.NumTets();
    Eigen::Matrix<T, 3, -1> bboxMin(3, numTets);
    Eigen::Matrix<T, 3, -1> bboxMax(3, numTets);
    for (int i = 0; i < numTets; ++i)
    {
        const Eigen::Vector4i indices = tetMesh.Tets().col(i);
        Eigen::Matrix<T, 3, 4> vertices;
        for (int k = 0; k < 4; ++k)
        {
            vertices.col(k) = tetMesh.Vertices().col(indices[k]);
        }
        bboxMin.col(i) = vertices.rowwise().minCoeff();
        bboxMax.col(i) = vertices.rowwise().maxCoeff();
    }

    const Eigen::Matrix<T, 3, -1> delta = bboxMax - bboxMin;
    bboxMax += delta / 2;
    bboxMin -= delta / 2;

    std::vector<Eigen::Matrix<T, 3, 3>> toTetCoordinates;
    for (int i = 0; i < numTets; ++i)
    {
        Eigen::Matrix<T, 3, 3> A;
        const Eigen::Vector4i indices = tetMesh.Tets().col(i);
        A.col(0) = tetMesh.Vertices().col(indices[1]) - tetMesh.Vertices().col(indices[0]);
        A.col(1) = tetMesh.Vertices().col(indices[2]) - tetMesh.Vertices().col(indices[0]);
        A.col(2) = tetMesh.Vertices().col(indices[3]) - tetMesh.Vertices().col(indices[0]);
        if (A.determinant() < T(1e-6))
        {
            CARBON_CRITICAL("Attempting to compute barycentric coordinates w.r.t. a degenerate tet");
        }
        toTetCoordinates.push_back(A.inverse());
    }

    auto calculateEmbedding = [&](int start, int end) {
            for (int i = start; i < end; i++)
            {
                int bestTet = -1;
                Eigen::Vector<T, 4> bestWeights;
                T maxMinWeight = std::numeric_limits<T>::lowest();

                for (int j = 0; j < tetMesh.NumTets(); j++)
                {
                    bool outOfBounds = false;
                    for (int k = 0; k < 3; ++k)
                    {
                        if ((points(k, i) < bboxMin(k, j)) || (points(k, i) > bboxMax(k, j)))
                        {
                            outOfBounds = true;
                        }
                    }
                    if (outOfBounds) { continue; }

                    // Eigen::Vector<T, 4> weights = BarycentricCoordinates<T, 4>::ComputeBarycentricCoordinates(points.col(i), tetMesh.Tets().col(j),
                    // tetMesh.Vertices());
                    const Eigen::Vector<T, 3> abc = toTetCoordinates[j] * (points.col(i) - tetMesh.Vertices().col(tetMesh.Tets()(0, j)));
                    Eigen::Vector<T, 4> weights;
                    weights[0] = T(1.0) - abc[0] - abc[1] - abc[2];
                    weights[1] = abc[0];
                    weights[2] = abc[1];
                    weights[3] = abc[2];

                    const T minWeight = weights.minCoeff();
                    if (minWeight > maxMinWeight)
                    {
                        maxMinWeight = minWeight;
                        bestTet = j;
                        bestWeights = weights;
                    }
                }
                m_barycentricCoordinates[i] = BarycentricCoordinates<T, 4>(tetMesh.Tets().col(bestTet), bestWeights);
            }
        };

    threadPool->AddTaskRangeAndWait(numPoints, calculateEmbedding);
}

template <class T>
void BarycentricEmbedding<T>::Serialize(std::string fname) const
{
    JsonElement json(JsonElement::JsonType::Object);
    Serialize(json);
    TITAN_NAMESPACE::WriteFile(fname, WriteJson(json));
}

template <class T>
void BarycentricEmbedding<T>::Serialize(JsonElement& json) const
{
    JsonElement indices(JsonElement::JsonType::Array);
    JsonElement weights(JsonElement::JsonType::Array);

    const size_t numPoints = m_barycentricCoordinates.size();
    for (size_t i = 0; i < numPoints; i++)
    {
        for (int c = 0; c < 4; c++)
        {
            indices.Append(JsonElement(m_barycentricCoordinates[i].Index(c)));
            weights.Append(JsonElement(m_barycentricCoordinates[i].Weight(c)));
        }
    }
    JsonElement jData(JsonElement::JsonType::Object);
    jData.Insert("numPoints", JsonElement(int(numPoints)));
    jData.Insert("indices", std::move(indices));
    jData.Insert("weights", std::move(weights));
    json.Insert("embedding", std::move(jData));
}

template <class T>
void BarycentricEmbedding<T>::Deserialize(std::string fname) { Deserialize(ReadJson(TITAN_NAMESPACE::ReadFile(fname))); }

template <class T>
void BarycentricEmbedding<T>::Deserialize(const JsonElement& json)
{
    if (!json.IsObject())
    {
        CARBON_CRITICAL("BarycentricEmbedding expects a json dictionary");
    }
    const JsonElement& jData = json.Contains("embedding") ? json["embedding"] : json;
    const size_t numPoints = jData["numPoints"].Get<size_t>();
    const JsonElement& jIndices = jData["indices"];
    const JsonElement& jWeights = jData["weights"];

    m_barycentricCoordinates.resize(numPoints);
    Eigen::Vector<int, 4> indices;
    Eigen::Vector<T, 4> weights;
    for (size_t i = 0; i < numPoints; i++)
    {
        for (int c = 0; c < 4; c++)
        {
            indices[c] = jIndices[4 * i + c].Get<int>();
            weights[c] = jWeights[4 * i + c].Get<T>();
        }
        m_barycentricCoordinates[i] = BarycentricCoordinates<T, 4>(indices, weights);
    }

    if (!allAffine()) { CARBON_CRITICAL("Deserialized barycentrics are not affine"); }
}

template class BarycentricEmbedding<float>;
template class BarycentricEmbedding<double>;

CARBON_NAMESPACE_END(TITAN_NAMESPACE)
