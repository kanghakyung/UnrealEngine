// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <dna/DataLayer.h>
#include <dna/BinaryStreamReader.h>
#include <dna/BinaryStreamWriter.h>
#include <dna/types/Aliases.h>
#include <dna/types/Vector3.h>
#include <pma/MemoryResource.h>
#include <pma/ScopedPtr.h>
#include <pma/resources/AlignedMemoryResource.h>
#include <pma/resources/ArenaMemoryResource.h>
#include <pma/resources/DefaultMemoryResource.h>
#include <status/Status.h>
#include <tdm/TDM.h>
#include <trio/Stream.h>
#include <trio/streams/FileStream.h>
#include <trio/streams/MemoryMappedFileStream.h>
#include <trio/streams/MemoryStream.h>

namespace rl4 {

using sc::Status;
using trio::BoundedIOStream;
using trio::FileStream;
using trio::MemoryMappedFileStream;
using trio::MemoryStream;
using dna::DataLayer;
using dna::BinaryStreamReader;
using dna::BinaryStreamWriter;
using dna::StringView;
using dna::UnknownLayerPolicy;
using dna::Vector3;
using tdm::fvec3;
using tdm::fquat;

template<typename T>
using ArrayView = dna::ArrayView<T>;

template<typename T>
using ConstArrayView = dna::ConstArrayView<T>;

using namespace pma;

}  // namespace rl4
