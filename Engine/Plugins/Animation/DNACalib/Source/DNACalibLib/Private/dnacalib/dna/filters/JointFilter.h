// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "dnacalib/TypeDefs.h"

#include <cstdint>

namespace dnac {

struct RawBehavior;
struct RawDefinition;
struct RawJointBehaviorMetadata;
struct RawLODMapping;
struct RawTwistSwingBehavior;
struct RawVertexSkinWeights;

class JointFilter {
    public:
        enum class Option {
            All,
            AnimationOnly
        };

    public:
        explicit JointFilter(MemoryResource* memRes_);
        void configure(std::uint16_t jointCount, UnorderedSet<std::uint16_t> allowedJointIndices, Option option_ = Option::All);
        void apply(RawDefinition& dest);
        void apply(RawBehavior& dest);
        void apply(RawVertexSkinWeights& dest);
        void apply(RawJointBehaviorMetadata& dest);
        void apply(RawTwistSwingBehavior& dest);
        bool passes(std::uint16_t index) const;
        std::uint16_t remapped(std::uint16_t oldIndex) const;
        std::uint16_t filteredJointCount() const;

    private:
        MemoryResource* memRes;
        UnorderedSet<std::uint16_t> passingIndices;
        UnorderedMap<std::uint16_t, std::uint16_t> remappedIndices;
        Option option;
        std::uint16_t rootJointIndex;

};

}  // namespace dnac
