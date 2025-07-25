// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "riglogic/TypeDefs.h"
#include "riglogic/blendshapes/BlendShapes.h"

namespace rl4 {

struct Configuration;
struct RigMetrics;
class Controls;

struct BlendShapesFactory {
    static BlendShapes::Pointer create(const Configuration& config,
                                       const dna::BehaviorReader* reader,
                                       Controls* controls,
                                       MemoryResource* memRes);
    static BlendShapes::Pointer create(const Configuration& config, const RigMetrics& metrics, MemoryResource* memRes);
};

}  // namespace rl4
