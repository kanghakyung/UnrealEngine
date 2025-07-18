// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <carbon/Common.h>
#include <carbon/io/JsonIO.h>
#include <nls/geometry/BarycentricCoordinates.h>
#include <nls/geometry/Mesh.h>
#include <nls/math/Math.h>

#include <map>
#include <set>
#include <string>
#include <vector>

CARBON_NAMESPACE_BEGIN(TITAN_NAMESPACE)

struct ContourData
{
    Eigen::VectorXi region;
    Eigen::VectorXi curve;
};

template <class T>
class MeshLandmarks
{
public:
    static constexpr const char* DEFAULT_MESH_NAME = "head_lod0_mesh";

public:
    MeshLandmarks() = default;

    //! loads all landmarks and curves from stream that are part of mesh @p meshName.
    bool DeserializeJson(const JsonElement& json, const Mesh<T>& mesh, const std::string& meshName = DEFAULT_MESH_NAME);

    //! loads all landmarks and curves from stream that are part of mesh @p meshName.
    bool DeserializeJson(const std::string& data, const Mesh<T>& mesh, const std::string& meshName = DEFAULT_MESH_NAME);

    //! loads all landmarks and curves from file @p filename that are pare of mesh @p meshName.
    bool Load(const std::string& filename, const Mesh<T>& mesh, const std::string& meshName = DEFAULT_MESH_NAME);

    //! Saves the mesh landmarks to a JSON file
    void Save(const std::string& filename, const std::string& meshName = "") const;

    //! Appends the mesh landmarks to a JSON file
    void Append(const std::string& filename, const std::string& meshName = "") const;

    std::string SerializeJson(std::string& previousData, const std::string& meshName) const;

    const std::map<std::string, BarycentricCoordinates<T>>& LandmarksBarycentricCoordinates() const
    {
        return m_meshLandmarksBarycentricCoordinates;
    }

    bool HasLandmark(const std::string& name) const
    {
        return m_meshLandmarksBarycentricCoordinates.find(name) != m_meshLandmarksBarycentricCoordinates.end();
    }

    const std::map<std::string, std::vector<BarycentricCoordinates<T>>>& MeshCurvesBarycentricCoordinates() const
    {
        return m_meshCurvesBarycentricCoordinates;
    }

    bool HasCurve(const std::string& name) const
    {
        return m_meshCurvesBarycentricCoordinates.find(name) != m_meshCurvesBarycentricCoordinates.end();
    }

    const std::vector<std::vector<int>>& InnerLowerLipContourLines() const { return m_innerLowerLipContourLines; }
    const std::vector<std::vector<int>>& InnerUpperLipContourLines() const { return m_innerUpperLipContourLines; }

    const std::map<std::string, std::vector<std::vector<int>>>& Contours() const { return m_contours; }

    bool HasContour(const std::string& name) const
    {
        return m_contours.find(name) != m_contours.end();
    }

    /**
     * @returns a contour of type @p name
     * @pre a contour of type @p name exists.
     */
    const std::vector<std::vector<int>>& Contour(const std::string& name) { return m_contours.find(name)->second; }

    void AddLandmark(const std::string& name, const BarycentricCoordinates<T>& landmark) { m_meshLandmarksBarycentricCoordinates[name] = landmark; }

    void AddCurve(const std::string& name, const std::vector<BarycentricCoordinates<T>>& curve) { m_meshCurvesBarycentricCoordinates[name] = curve; }

    void MergeCurves(const std::vector<std::string>& curveNames, const std::string& newCurveName, bool removePreviousCurves);

    //! get all the vertices that are used by landmarks, mesh curves, and contour lines.
    std::set<int> GetAllVertexIndices() const;

    //! remap the mesh landmarks. @returns True if mapping was successful, false if the map does not map all of the vertex IDs.
    bool Remap(const std::map<int, int>& oldIndexToNewIndex);

    //! @returns True if the curve is a loop.
    bool IsLoop(const std::string& curveName) const;

    //! sort a @p curve of unordered points to be sorted based on the mesh edges and optionally sorting it right to left.
    static std::pair<Eigen::VectorXi, bool> SortCurveUsingMeshTopology(const Mesh<T>& mesh,
                                                                       const Eigen::VectorXi& curve,
                                                                       const std::string& name,
                                                                       bool sortRightToLeft);

    /**
     * Calculates the contour lines by walking along the edge loops that are orthogonal to the @p curve line.
     * The resulting contour lines are sorted from front to back on the face.
     */
    static std::vector<std::vector<int>> CalculateContourLines(const Eigen::VectorXi& region,
                                                               const Eigen::VectorXi& curve,
                                                               const Mesh<T>& mesh,
                                                               const std::string& name);

private:
    std::map<std::string, BarycentricCoordinates<T>> m_meshLandmarksBarycentricCoordinates;
    std::map<std::string, std::vector<BarycentricCoordinates<T>>> m_meshCurvesBarycentricCoordinates;
    std::map<std::string, std::vector<std::vector<int>>> m_contours;
    std::map<std::string, ContourData> m_contourData;

    std::vector<std::vector<int>> m_innerLowerLipContourLines;
    std::vector<std::vector<int>> m_innerUpperLipContourLines;
    ContourData m_innerLowerLipContourData;
    ContourData m_innerUpperLipContourData;

    // set of curves that are loops
    std::set<std::string> m_loops;
};

CARBON_NAMESPACE_END(TITAN_NAMESPACE)
