// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuT/ASTOpMeshMorphReshape.h"

#include "HAL/PlatformMath.h"
#include "MuR/ModelPrivate.h"
#include "MuR/RefCounted.h"
#include "MuR/Types.h"


namespace mu
{


	ASTOpMeshMorphReshape::ASTOpMeshMorphReshape()
		: Morph(this)
		, Reshape(this)
	{
	}


	ASTOpMeshMorphReshape::~ASTOpMeshMorphReshape()
	{
		// Explicit call needed to avoid recursive destruction
		ASTOp::RemoveChildren();
	}


	bool ASTOpMeshMorphReshape::IsEqual(const ASTOp& otherUntyped) const
	{
		if (otherUntyped.GetOpType() == GetOpType())
		{
			const ASTOpMeshMorphReshape* other = static_cast<const ASTOpMeshMorphReshape*>(&otherUntyped);
			return Morph == other->Morph && Reshape == other->Reshape;
		}

		return false;
	}


	uint64 ASTOpMeshMorphReshape::Hash() const
	{
		uint64 res = std::hash<void*>()(Morph.child().get());
		hash_combine(res, Reshape.child().get());

		return res;
	}


	mu::Ptr<ASTOp> ASTOpMeshMorphReshape::Clone(MapChildFuncRef mapChild) const
	{
		Ptr<ASTOpMeshMorphReshape> n = new ASTOpMeshMorphReshape();
		n->Morph = mapChild(Morph.child());
		n->Reshape = mapChild(Reshape.child());
		return n;
	}


	void ASTOpMeshMorphReshape::ForEachChild(const TFunctionRef<void(ASTChild&)> f)
	{
		f(Morph);
		f(Reshape);
	}


	void ASTOpMeshMorphReshape::Link(FProgram& program, FLinkerOptions*)
	{
		// Already linked?
		if (!linkedAddress)
		{
			OP::MeshMorphReshapeArgs Args;
			FMemory::Memzero(Args);

			if (Morph)
			{
				Args.Morph = Morph->linkedAddress;
			}

			if (Reshape)
			{
				Args.Reshape = Reshape->linkedAddress;
			}

			linkedAddress = (OP::ADDRESS)program.OpAddress.Num();
			program.OpAddress.Add((uint32_t)program.ByteCode.Num());
			AppendCode(program.ByteCode, EOpType::ME_MORPHRESHAPE);
			AppendCode(program.ByteCode, Args);
		}

	}


	FSourceDataDescriptor ASTOpMeshMorphReshape::GetSourceDataDescriptor(FGetSourceDataDescriptorContext* Context) const
	{
		if (Morph)
		{
			return Morph->GetSourceDataDescriptor(Context);
		}

		return {};
	}

}
