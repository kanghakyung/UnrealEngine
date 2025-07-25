// Copyright Epic Games, Inc. All Rights Reserved.

#include "NaniteDisplacedMeshAlgo.h"

#if WITH_EDITOR

#include "Async/ParallelFor.h"
#include "EngineLogs.h"
#include "NaniteDisplacedMesh.h"
#include "DisplacementMap.h"
#include "Engine/Texture2D.h"
#include "Affine.h"
#include "LerpVert.h"
#include "AdaptiveTessellator.h"
#include "Math/Bounds.h"

static FVector3f UserGetDisplacement(
	const FVector3f& Barycentrics,
	const FLerpVert& Vert0,
	const FLerpVert& Vert1,
	const FLerpVert& Vert2,
	int32 MaterialIndex,
	TArrayView< Nanite::FDisplacementMap > DisplacementMaps,
	EDisplaceNaniteMeshOptions::Type Options )
{
	int32 DisplacementIndex = FMath::FloorToInt( Vert0.UVs[1].X );
	if( !DisplacementMaps.IsValidIndex( DisplacementIndex ) )
		return FVector3f::ZeroVector;

	FVector2f UV;
	UV  = Vert0.UVs[0] * Barycentrics.X;
	UV += Vert1.UVs[0] * Barycentrics.Y;
	UV += Vert2.UVs[0] * Barycentrics.Z;

	if ( EnumHasAnyFlags( Options, EDisplaceNaniteMeshOptions::IgnoreNonNormalizedDisplacementUVs ) )
	{
		if (UV.X < 0.0f || UV.X > 1.0f || UV.Y < 0.0f || UV.Y > 1.0f)
		{
			return FVector3f::ZeroVector;
		}
	}

	const FVector3f Normal0 = FVector3f(Vert0.UVs[6].X, Vert0.UVs[6].Y, Vert0.UVs[7].X);
	const FVector3f Normal1 = FVector3f(Vert1.UVs[6].X, Vert1.UVs[6].Y, Vert1.UVs[7].X);
	const FVector3f Normal2 = FVector3f(Vert2.UVs[6].X, Vert2.UVs[6].Y, Vert2.UVs[7].X);

	FVector3f Normal;
	Normal  = Normal0 * Barycentrics.X;
	Normal += Normal1 * Barycentrics.Y;
	Normal += Normal2 * Barycentrics.Z;

	if( Normal0.IsUnit() &&
		Normal1.IsUnit() &&
		Normal2.IsUnit() )
	{
		Normal.Normalize();
	}

	float Displacement = DisplacementMaps[ DisplacementIndex ].Sample( UV );

	return Normal * Displacement;
}

static FVector2f UserGetErrorBounds(
	const FVector3f Barycentrics[3],
	const FLerpVert& Vert0,
	const FLerpVert& Vert1,
	const FLerpVert& Vert2,
	const FVector3f& Displacement0,
	const FVector3f& Displacement1,
	const FVector3f& Displacement2,
	int32 MaterialIndex,
	TArrayView< Nanite::FDisplacementMap > DisplacementMaps )
{
#if 1
	float MinBarycentric0 = FMath::Min3( Barycentrics[0].X, Barycentrics[1].X, Barycentrics[2].X );
	float MaxBarycentric0 = FMath::Max3( Barycentrics[0].X, Barycentrics[1].X, Barycentrics[2].X );

	float MinBarycentric1 = FMath::Min3( Barycentrics[0].Y, Barycentrics[1].Y, Barycentrics[2].Y );
	float MaxBarycentric1 = FMath::Max3( Barycentrics[0].Y, Barycentrics[1].Y, Barycentrics[2].Y );

	TAffine< float, 2 > Barycentric0( MinBarycentric0, MaxBarycentric0, 0 );
	TAffine< float, 2 > Barycentric1( MinBarycentric1, MaxBarycentric1, 1 );
	TAffine< float, 2 > Barycentric2 = TAffine< float, 2 >( 1.0f ) - Barycentric0 - Barycentric1;

	TAffine< FVector3f, 2 > LerpedDisplacement;
	LerpedDisplacement  = TAffine< FVector3f, 2 >( Displacement0 ) * Barycentric0;
	LerpedDisplacement += TAffine< FVector3f, 2 >( Displacement1 ) * Barycentric1;
	LerpedDisplacement += TAffine< FVector3f, 2 >( Displacement2 ) * Barycentric2;

	const FVector3f Normal0 = FVector3f(Vert0.UVs[6].X, Vert0.UVs[6].Y, Vert0.UVs[7].X);
	const FVector3f Normal1 = FVector3f(Vert1.UVs[6].X, Vert1.UVs[6].Y, Vert1.UVs[7].X);
	const FVector3f Normal2 = FVector3f(Vert2.UVs[6].X, Vert2.UVs[6].Y, Vert2.UVs[7].X);

	TAffine< FVector3f, 2 > Normal;
	Normal  = TAffine< FVector3f, 2 >( Normal0 ) * Barycentric0;
	Normal += TAffine< FVector3f, 2 >( Normal1 ) * Barycentric1;
	Normal += TAffine< FVector3f, 2 >( Normal2 ) * Barycentric2;

	if( Normal0.IsUnit() &&
		Normal1.IsUnit() &&
		Normal2.IsUnit() )
	{
		Normal = Normalize( Normal );
	}

	FVector2f MinUV = {  MAX_flt,  MAX_flt };
	FVector2f MaxUV = { -MAX_flt, -MAX_flt };
	for( int k = 0; k < 3; k++ )
	{
		FVector2f UV;
		UV  = Vert0.UVs[0] * Barycentrics[k].X;
		UV += Vert1.UVs[0] * Barycentrics[k].Y;
		UV += Vert2.UVs[0] * Barycentrics[k].Z;

		MinUV = FVector2f::Min( MinUV, UV );
		MaxUV = FVector2f::Max( MaxUV, UV );
	}

	// Assume index is constant across triangle
	int32 DisplacementIndex = FMath::FloorToInt( Vert0.UVs[1].X );

	FVector2f DisplacementBounds( 0.0f, 0.0f );
	if( DisplacementMaps.IsValidIndex( DisplacementIndex ) )
		DisplacementBounds = DisplacementMaps[ DisplacementIndex ].Sample( MinUV, MaxUV );
	
	TAffine< float, 2 > Displacement( DisplacementBounds.X, DisplacementBounds.Y );
	TAffine< float, 2 > Error = ( Normal * Displacement - LerpedDisplacement ).SizeSquared();

	return FVector2f( Error.GetMin(), Error.GetMax() );
#else
	float ScalarDisplacement0 = Displacement0.Length();
	float ScalarDisplacement1 = Displacement1.Length();
	float ScalarDisplacement2 = Displacement2.Length();	
	float LerpedDisplacement[3];
	
	FVector2f MinUV = {  MAX_flt,  MAX_flt };
	FVector2f MaxUV = { -MAX_flt, -MAX_flt };
	for( int k = 0; k < 3; k++ )
	{
		LerpedDisplacement[k]  = ScalarDisplacement0 * Barycentrics[k].X;
		LerpedDisplacement[k] += ScalarDisplacement1 * Barycentrics[k].Y;
		LerpedDisplacement[k] += ScalarDisplacement2 * Barycentrics[k].Z;

		FVector2f UV;
		UV  = Vert0.UVs[0] * Barycentrics[k].X;
		UV += Vert1.UVs[0] * Barycentrics[k].Y;
		UV += Vert2.UVs[0] * Barycentrics[k].Z;

		MinUV = FVector2f::Min( MinUV, UV );
		MaxUV = FVector2f::Max( MaxUV, UV );
	}
	
	FVector2f LerpedBounds;
	LerpedBounds.X = FMath::Min3( LerpedDisplacement[0], LerpedDisplacement[1], LerpedDisplacement[2] );
	LerpedBounds.Y = FMath::Max3( LerpedDisplacement[0], LerpedDisplacement[1], LerpedDisplacement[2] );

	FVector2f DisplacementBounds = DisplacementMaps[ DisplacementIndex ].Sample( MinUV, MaxUV );

	FVector2f Delta( DisplacementBounds.X - LerpedBounds.Y, DisplacementBounds.Y - LerpedBounds.X );
	FVector2f DeltaSqr = Delta * Delta;

	FVector2f ErrorBounds;
	ErrorBounds.X = DeltaSqr.GetMin();
	ErrorBounds.Y = DeltaSqr.GetMax();
	if( Delta.X * Delta.Y < 0.0f )
		ErrorBounds.X = 0.0f;
#endif
}

static int32 UserGetNumSamples(
	const FVector3f Barycentrics[3],
	const FLerpVert& Vert0,
	const FLerpVert& Vert1,
	const FLerpVert& Vert2,
	int32 MaterialIndex,
	TArrayView< Nanite::FDisplacementMap > DisplacementMaps )
{
	// Assume index is constant across triangle
	int32 DisplacementIndex = FMath::FloorToInt( Vert0.UVs[1].X );

	if( DisplacementMaps.IsValidIndex( DisplacementIndex ) )
	{
		FVector2f UVs[3];
		for( int k = 0; k < 3; k++ )
		{
			UVs[k]  = Vert0.UVs[0] * Barycentrics[k].X;
			UVs[k] += Vert1.UVs[0] * Barycentrics[k].Y;
			UVs[k] += Vert2.UVs[0] * Barycentrics[k].Z;

			UVs[k].X *= (float)DisplacementMaps[ DisplacementIndex ].SizeX;
			UVs[k].Y *= (float)DisplacementMaps[ DisplacementIndex ].SizeY;
		}

		FVector2f Edge01 = UVs[1] - UVs[0];
		FVector2f Edge12 = UVs[2] - UVs[1];
		FVector2f Edge20 = UVs[0] - UVs[2];

		float MaxEdgeLength = FMath::Sqrt( FMath::Max3(
			Edge01.SizeSquared(),
			Edge12.SizeSquared(),
			Edge20.SizeSquared() ) );

		float AreaInTexels = FMath::Abs( 0.5f * ( Edge01 ^ Edge12 ) );

		return FMath::CeilToInt( FMath::Max( MaxEdgeLength, AreaInTexels ) );
	}

	return 1;
}

bool DisplaceNaniteMesh(
	const FNaniteDisplacedMeshParams& Parameters,
	const uint32 NumTextureCoord,
	FMeshBuildVertexData& Verts,
	TArray< uint32 >& Indexes,
	TArray< int32 >& MaterialIndexes,
	FBounds3f& VertexBounds,
	EDisplaceNaniteMeshOptions::Type Options
)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DisplaceNaniteMesh);

	uint32 Time0 = FPlatformTime::Cycles();

	// TODO: Make the mesh prepare and displacement logic extensible, and not hardcoded within this plugin

	// START - MESH PREPARE
	TArray<FVector3f> VertDisplacements;
	if (Verts.UVs.Num() > 1)
	{
		TArray<uint32> VertSamples;
		VertSamples.SetNumZeroed(Verts.Position.Num());
		VertDisplacements.SetNumZeroed(Verts.Position.Num());

		ParallelFor(TEXT("Nanite.Displace.Guide"), Verts.Position.Num(), 1024,
		[&](int32 VertIndex)
		{
			for (int32 GuideVertIndex = 0; GuideVertIndex < Verts.Position.Num(); ++GuideVertIndex)
			{
				if (Verts.UVs[1][GuideVertIndex].Y >= 0.0f)
				{
					continue;
				}

				FVector3f GuideVertPos = Verts.Position[GuideVertIndex];

				// Matches the geoscript prototype (TODO: Remove)
				const bool bApplyTolerance = true;
				if (bApplyTolerance)
				{
					float Tolerance = 0.01f;
					GuideVertPos /= Tolerance;
					GuideVertPos.X = float(FMath::CeilToInt(GuideVertPos.X)) * Tolerance;
					GuideVertPos.Y = float(FMath::CeilToInt(GuideVertPos.Y)) * Tolerance;
					GuideVertPos.Z = float(FMath::CeilToInt(GuideVertPos.Z)) * Tolerance;
				}

				if (FVector3f::Distance(Verts.Position[VertIndex], GuideVertPos) < 0.1f)
				{
					++VertSamples[VertIndex];
					VertDisplacements[VertIndex] += Verts.TangentZ[GuideVertIndex];
				}
			}

			if (VertSamples[VertIndex] > 0)
			{
				VertDisplacements[VertIndex].Normalize();
			}
		});
	}
	// END - MESH PREPARE

	FBounds3f Bounds;
	for( const FVector3f& VertPosition : Verts.Position )
		Bounds += VertPosition;

	float SurfaceArea = 0.0f;
	for( int32 TriIndex = 0; TriIndex < MaterialIndexes.Num(); TriIndex++ )
	{
		const FVector3f& Vert0Position = Verts.Position[ Indexes[ TriIndex * 3 + 0 ] ];
		const FVector3f& Vert1Position = Verts.Position[ Indexes[ TriIndex * 3 + 1 ] ];
		const FVector3f& Vert2Position = Verts.Position[ Indexes[ TriIndex * 3 + 2 ] ];

		FVector3f Edge01 = Vert1Position - Vert0Position;
		FVector3f Edge12 = Vert2Position - Vert1Position;
		FVector3f Edge20 = Vert0Position - Vert2Position;

		SurfaceArea += 0.5f * ( Edge01 ^ Edge20 ).Size();
	}

	float TargetError = Parameters.RelativeError * 0.01f * FMath::Sqrt( FMath::Min( 2.0f * SurfaceArea, Bounds.GetSurfaceArea() ) );

	// Overtessellate by 50% and simplify down
	TargetError *= 1.5f;

	TArray< Nanite::FDisplacementMap > DisplacementMaps;
	for( auto& DisplacementMap : Parameters.DisplacementMaps )
	{

		if (DisplacementMap.Texture)
		{
			if (IsValid(DisplacementMap.Texture) && DisplacementMap.Texture->Source.IsValid())
			{
				FImage FirstMipImage;
				if (DisplacementMap.Texture->Source.GetMipImage(FirstMipImage, 0))
				{
					DisplacementMaps.Add(Nanite::FDisplacementMap(
						MoveTemp(FirstMipImage),
						DisplacementMap.Magnitude,
						DisplacementMap.Center,
						DisplacementMap.Texture->AddressX,
						DisplacementMap.Texture->AddressY));
				}
				else
				{
					// Virtualization can fail to fetch the bulk data. (Avoid crashing or polluting a ddc key)
					UE_LOG( LogStaticMesh, Error, TEXT("Adaptive tessellate failed because it couldn't read the displacement texture source data"));
					return false;
				}
			}
			else
			{
				// If the raw pointer is not null, but for some reason it is not valid bail out of the build to not pollute a ddc key
				UE_LOG( LogStaticMesh, Error, TEXT("Adaptive tessellate failed because it couldn't use the displacement texture"));
				return false;
			}
		}
		else
		{
			DisplacementMaps.AddDefaulted();
		}
	}

	TArray< FLerpVert >	LerpVerts;
	LerpVerts.AddUninitialized( Verts.Position.Num() );
	for( int32 i = 0; i < Verts.Position.Num(); i++ )
		LerpVerts[i] = MakeStaticMeshVertex(Verts, i);
	
	if (!VertDisplacements.IsEmpty())
	{
		for (int32 i = 0; i < Verts.Position.Num(); i++)
		{
			LerpVerts[i].UVs[6].X = VertDisplacements[i].X;
			LerpVerts[i].UVs[6].Y = VertDisplacements[i].Y;
			LerpVerts[i].UVs[7].X = VertDisplacements[i].Z;
		}
	}

	Nanite::FAdaptiveTessellator Tessellator( LerpVerts, Indexes, MaterialIndexes, TargetError, TargetError, true,
		[&](const FVector3f& Barycentrics,
			const FLerpVert& Vert0,
			const FLerpVert& Vert1,
			const FLerpVert& Vert2,
			int32 MaterialIndex )
		{
			return UserGetDisplacement(
				Barycentrics,
				Vert0,
				Vert1,
				Vert2,
				MaterialIndex,
				DisplacementMaps,
				Options );
		},
		[&](const FVector3f Barycentrics[3],
			const FLerpVert& Vert0,
			const FLerpVert& Vert1,
			const FLerpVert& Vert2,
			const FVector3f& Displacement0,
			const FVector3f& Displacement1,
			const FVector3f& Displacement2,
			int32 MaterialIndex )
		{
			return UserGetErrorBounds(
				Barycentrics,
				Vert0,
				Vert1,
				Vert2,
				Displacement0,
				Displacement1,
				Displacement2,
				MaterialIndex,
				DisplacementMaps );
		},
		[&](const FVector3f Barycentrics[3],
			const FLerpVert& Vert0,
			const FLerpVert& Vert1,
			const FLerpVert& Vert2,
			int32 MaterialIndex )
		{
			return UserGetNumSamples(
				Barycentrics,
				Vert0,
				Vert1,
				Vert2,
				MaterialIndex,
				DisplacementMaps );
		} );

	const bool bHasVertexColor = Verts.Color.Num() > 0;
	
	Verts.Position.SetNumUninitialized(LerpVerts.Num());
	Verts.TangentX.SetNumUninitialized(LerpVerts.Num());
	Verts.TangentY.SetNumUninitialized(LerpVerts.Num());
	Verts.TangentZ.SetNumUninitialized(LerpVerts.Num());

	for (int32 UVCoord = 0; UVCoord < Verts.UVs.Num(); ++UVCoord)
	{
		Verts.UVs[UVCoord].SetNumUninitialized(LerpVerts.Num());
	}

	Verts.Color.SetNumUninitialized(bHasVertexColor ? LerpVerts.Num() : 0);

	for (int32 LerpIndex = 0; LerpIndex < LerpVerts.Num(); ++LerpIndex)
	{
		const FLerpVert& LerpVert = LerpVerts[LerpIndex];
		Verts.Position[LerpIndex] = LerpVert.Position;
		Verts.TangentX[LerpIndex] = LerpVert.TangentX;
		Verts.TangentY[LerpIndex] = LerpVert.TangentY;
		Verts.TangentZ[LerpIndex] = LerpVert.TangentZ;

		for (int32 UVCoord = 0; UVCoord < Verts.UVs.Num(); ++UVCoord)
		{
			Verts.UVs[UVCoord][LerpIndex] = LerpVert.UVs[UVCoord];
		}

		if (bHasVertexColor)
		{
			Verts.Color[LerpIndex] = LerpVert.Color.ToFColor(false);
		}

		VertexBounds += LerpVert.Position;
	}

	uint32 Time1 = FPlatformTime::Cycles();
	UE_LOG( LogStaticMesh, Log, TEXT("Adaptive tessellate [%.2fs], tris: %i"), FPlatformTime::ToMilliseconds( Time1 - Time0 ) / 1000.0f, Indexes.Num() / 3 );

	return true;
}

#endif
