// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <carbon/io/JsonIO.h>

#include <nls/serialization/EigenSerialization.h>

CARBON_NAMESPACE_BEGIN(TITAN_NAMESPACE)

/**
 * Serializes vertices to a json dictionary {
 *  “vertices” : 3xN matrix
 * }
 */
template <class T>
void GeometryToJson(JsonElement& j, const Eigen::Matrix<T, 3, Eigen::Dynamic>& vertices) { j.Insert("vertices", ToJson(vertices)); }

/**
 * Deserializes vertices from a json dictionary {
 *  “vertices” : 3xN matrix
 * }
 */
template <class T>
void GeometryFromJson(const JsonElement& j, Eigen::Matrix<T, 3, Eigen::Dynamic>& vertices) { io::FromJson(j["vertices"], vertices); }

/**
 * Deserializes multiple geometries
 * {
 *    “geometry” : {
 *       “name of geometry” : {
 *          “vertices” : 3xN matrix
 *       },
 *       ...
 *    }
 * }
 */
template <class T>
void MultiGeometryFromJson(const JsonElement& j, std::map<std::string, Eigen::Matrix<T, 3, Eigen::Dynamic>>& geometryMap)
{
    geometryMap.clear();
    for (const auto& [geometryName, verticesDict] : j["geometry"].Map())
    {
        Eigen::Matrix<T, 3, Eigen::Dynamic> vertices;
        GeometryFromJson(verticesDict, vertices);
        geometryMap[geometryName] = vertices;
    }
}

template <class T>
void MultiGeometryFromJson(const JsonElement& j, std::vector<Eigen::Matrix<T, 3, Eigen::Dynamic>>& geometryMap)
{
    geometryMap.clear();
    for (const auto& verticesDict : j.Array())
    {
        Eigen::Matrix<T, 3, Eigen::Dynamic> vertices;
        GeometryFromJson(verticesDict, vertices);
        geometryMap.push_back(vertices);
    }
}

CARBON_NAMESPACE_END(TITAN_NAMESPACE)
