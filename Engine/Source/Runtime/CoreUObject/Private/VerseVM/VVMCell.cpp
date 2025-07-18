// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
#include "VerseVM/VVMCell.h"
#include "VerseVM/Inline/VVMAbstractVisitorInline.h"
#include "VerseVM/Inline/VVMCellInline.h"
#include "VerseVM/Inline/VVMMarkStackVisitorInline.h"
#include "VerseVM/VVMCppClassInfo.h"
#include "VerseVM/VVMEmergentType.h"
#include "VerseVM/VVMHeap.h"
#include "VerseVM/VVMOpResult.h"
#include "VerseVM/VVMStructuredArchiveVisitor.h"
#include "VerseVM/VVMValue.h"
#include "VerseVM/VVMWeakKeyMapGuard.h"
#include <type_traits>

namespace Verse
{
DEFINE_BASE_VCPPCLASSINFO(VCell);
DEFINE_DERIVED_VCPPCLASSINFO(VHeapValue);

static_assert(std::is_trivially_destructible_v<VCell>);

TNeverDestroyed<TOptional<FStrongCellRegistry>> VCell::GlobalStrongCellRegistry;

VCell::VCell(FAllocationContext Context, const VEmergentType* EmergentType)
	: EmergentTypeOffset(FHeap::EmergentTypePtrToOffset(EmergentType))
{
	checkSlow(FHeap::OwnsAddress(this));
	// TODO: assert that if the emergent type has a destructor, that this cell is allocated in an appropriate subspace
	Context.RunWriteBarrierNonNull(EmergentType);

#if WITH_EDITORONLY_DATA
	Context.RecordCell(this);
#endif
}

void VCell::SetEmergentType(FAccessContext Context, VEmergentType* EmergentType)
{
	Context.RunWriteBarrierNonNull(EmergentType);
	EmergentTypeOffset = FHeap::EmergentTypePtrToOffset(EmergentType);
}

FString VCell::DebugName() const
{
	return GetEmergentType()->CppClassInfo->DebugName();
}

void VCell::ConductCensus()
{
	GetEmergentType()->CppClassInfo->ConductCensus(this);
}

void VCell::RunDestructor()
{
	VCppClassInfo* CppClassInfo = GetEmergentType()->CppClassInfo;
	checkSlow(CppClassInfo->RunDestructor);
	CppClassInfo->RunDestructor(this);
}

ECompares VCell::Equal(FAllocationContext Context, VCell* Other, const TFunction<void(::Verse::VValue, ::Verse::VValue)>& HandlePlaceholder)
{
	return GetEmergentType()->CppClassInfo->Equal(Context, this, Other, HandlePlaceholder);
}

VValue VCell::Melt(FAllocationContext Context)
{
	return GetEmergentType()->CppClassInfo->Melt(Context, this);
}

FOpResult VCell::Freeze(FAllocationContext Context)
{
	return GetEmergentType()->CppClassInfo->Freeze(Context, this);
}

bool VCell::Subsumes(FAllocationContext Context, VValue Value)
{
	return GetEmergentType()->CppClassInfo->Subsumes(Context, this, Value);
}

void VCell::VisitMembers(FAllocationContext Context, FDebuggerVisitor& Visitor)
{
	GetEmergentType()->CppClassInfo->VisitMembers(Context, this, Visitor);
}

void VCell::Serialize(FAllocationContext Context, FStructuredArchiveVisitor& Visitor)
{
	GetEmergentType()->CppClassInfo->Serialize(Context, this, Visitor);
}

void VCell::Serialize(FAllocationContext Context, FArchive& Ar)
{
	FStructuredArchiveFromArchive StructuredArchive(Ar);
	FStructuredArchiveVisitor Visitor(Context, StructuredArchive.GetSlot().EnterRecord());
	Serialize(Context, Visitor);
}

void VCell::InitializeGlobals(FAllocationContext Context)
{
	GlobalStrongCellRegistry.Get().Emplace();
}

void VCell::ConductCensusImpl()
{
}

ECompares VCell::EqualImpl(FAllocationContext Context, VCell* Other, const TFunction<void(::Verse::VValue, ::Verse::VValue)>& HandlePlaceholder)
{
	V_DIE("VCell subtype without `EqualImpl` override called! Either this type should have an override "
		  "if comparable OR a non-comparable type is being compared which is an error.");
}

uint32 VCell::GetTypeHashImpl()
{
	V_DIE("VCell subtype without `GetTypeHashImpl` override called! Either this type should have an override "
		  "if hashable OR a non-hashable type is being hashed which is an error.");
}

VValue VCell::MeltImpl(FAllocationContext Context)
{
	V_DIE("VCell subtype without `MeltImpl` override called! Either this type should have an override "
		  "or an invalid subtype is being melted.");
}

FOpResult VCell::FreezeImpl(FAllocationContext Context)
{
	V_DIE("VCell subtype '%s' without `FreezeImpl` override called! Either this type should have an override "
		  "or an invalid subtype is being frozen.",
		GetCppClassInfo()->Name);
}

bool VCell::SubsumesImpl(FAllocationContext, VValue)
{
	V_DIE("VCell subtype without `SubsumesImpl` override called!");
}

void VCell::VisitMembersImpl(FAllocationContext, FDebuggerVisitor&)
{
}

void VCell::SerializeImpl(FAllocationContext, FStructuredArchiveVisitor&)
{
	V_DIE("VCell subtype '%s' without `SerializeImpl` override called!", GetCppClassInfo()->Name);
}

void VCell::AddWeakMapping(VCell* Map, VCell* Value)
{
	FWeakKeyMapGuard Guard(FHeapPageHeader::Get(this));
	FWeakKeyMap* KeyMap = Guard.Get();
	KeyMap->Add(this, Map, Value);
	GCData |= GCDataIsWeakKeyBit;
}

void VCell::RemoveWeakMapping(VCell* Map)
{
	FWeakKeyMapGuard Guard(FHeapPageHeader::Get(this));
	if (FWeakKeyMap* KeyMap = Guard.TryGet())
	{
		KeyMap->Remove(this, Map);
	}
}

bool VCell::HasWeakMappings()
{
	// If we cared about performance of this function, we'd introduce some fast path thing where we quickly check the contents of client_data.
	// But we don't care, since this is a test-only function!
	FWeakKeyMapGuard Guard(FHeapPageHeader::Get(this));
	if (FWeakKeyMap* KeyMap = Guard.TryGet())
	{
		return KeyMap->HasEntriesForKey(this);
	}
	else
	{
		return false;
	}
}

void VCell::VisitReferences(FMarkStackVisitor& Visitor)
{
	GetEmergentType()->CppClassInfo->VisitReferences(this, Visitor);
}

void VCell::VisitReferences(FAbstractVisitor& Visitor)
{
	GetEmergentType()->CppClassInfo->VisitReferences(this, Visitor);
}

} // namespace Verse
#endif // WITH_VERSE_VM || defined(__INTELLISENSE__)
