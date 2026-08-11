#include "StreetMapBuildingDetailsComponent.h"
#include "StreetMap.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"

DEFINE_LOG_CATEGORY_STATIC(LogStreetMapBuildingDetails, Log, All);


UStreetMapBuildingDetailsComponent::UStreetMapBuildingDetailsComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// WindowInstances/DoorInstances are created and attached by AStreetMapBuildingDetailsActor's
	// constructor instead (see InitializeInstanceComponents) -- see the comment on that function for why.
}


namespace StreetMapBuildingDetailsPrivate
{
	// Small self-contained integer hash (not FMath::FRand()) so re-running generation while tuning
	// parameters gives reproducible, comparable results instead of a different random layout every time.
	float DeterministicRandom01( int32 A, int32 B, int32 C )
	{
		uint32 H = static_cast<uint32>( A ) * 374761393u + static_cast<uint32>( B ) * 668265263u + static_cast<uint32>( C ) * 2246822519u;
		H = ( H ^ ( H >> 15 ) ) * 2246822519u;
		H = ( H ^ ( H >> 13 ) ) * 3266489917u;
		H = H ^ ( H >> 16 );
		return static_cast<float>( H ) / static_cast<float>( MAX_uint32 );
	}

	// Perpendicular to EdgeDir, flipped to point away from the polygon's centroid -- avoids having to
	// re-derive the footprint's CW/CCW winding (which StreetMapComponent only computes via a full
	// re-triangulation pass).
	FVector2D ComputeOutwardNormal( const FVector2D& EdgeDir, const FVector2D& EdgeMidpoint, const FVector2D& Centroid )
	{
		FVector2D Normal( -EdgeDir.Y, EdgeDir.X );
		if( FVector2D::DotProduct( Normal, EdgeMidpoint - Centroid ) < 0.0 )
		{
			Normal = -Normal;
		}
		return Normal;
	}

	FRotator RotatorFacing( const FVector2D& OutwardNormal )
	{
		// Instances are documented as facing local +X -- FVector::Rotation() gives the rotator that
		// carries the X axis onto the supplied direction.
		return FVector( OutwardNormal.X, OutwardNormal.Y, 0.0f ).Rotation();
	}
}


void UStreetMapBuildingDetailsComponent::ClearBuildingDetails()
{
	if( WindowInstances != nullptr )
	{
		WindowInstances->ClearInstances();
	}
	if( DoorInstances != nullptr )
	{
		DoorInstances->ClearInstances();
	}
}


void UStreetMapBuildingDetailsComponent::GenerateBuildingDetails()
{
	using namespace StreetMapBuildingDetailsPrivate;

	ClearBuildingDetails();

	if( StreetMap == nullptr )
	{
		UE_LOG( LogStreetMapBuildingDetails, Warning, TEXT("GenerateBuildingDetails: no StreetMap assigned.") );
		return;
	}

	if( WindowInstances == nullptr || DoorInstances == nullptr )
	{
		return;
	}

	WindowInstances->SetStaticMesh( WindowMesh );
	DoorInstances->SetStaticMesh( DoorMesh );

	const TArray<FStreetMapBuilding>& Buildings = StreetMap->GetBuildings();

	int32 BuildingsProcessed = 0;
	int32 WindowsPlaced = 0;
	int32 DoorsPlaced = 0;

	for( int32 BuildingIndex = 0; BuildingIndex < Buildings.Num(); ++BuildingIndex )
	{
		const FStreetMapBuilding& Building = Buildings[ BuildingIndex ];

		if( bResidentialOnly && !Building.bIsResidential )
		{
			continue;
		}

		const int32 NumPoints = Building.BuildingPoints.Num();
		if( NumPoints < 3 )
		{
			continue;
		}

		// Same fallback used for BuildingFillZ in StreetMapComponent.cpp: real height wins, otherwise
		// derive from level count, otherwise there's no usable wall to put details on.
		const double TotalHeight = Building.Height > 0.0 ? Building.Height
			: ( Building.BuildingLevels > 0 ? (double)Building.BuildingLevels * FloorHeight : 0.0 );
		if( TotalHeight <= KINDA_SMALL_NUMBER )
		{
			continue;
		}
		const int32 NumFloors = FMath::Max( 1, FMath::RoundToInt( TotalHeight / FloorHeight ) );

		FVector2D Centroid( 0.0, 0.0 );
		for( const FVector2D& Point : Building.BuildingPoints )
		{
			Centroid += Point;
		}
		Centroid /= (double)NumPoints;

		// Longest edge gets the door -- avoids an awkward door on a tiny 1m footprint segment.
		int32 DoorEdgeIndex = 0;
		double LongestEdgeLenSq = -1.0;
		for( int32 EdgeIndex = 0; EdgeIndex < NumPoints; ++EdgeIndex )
		{
			const double EdgeLenSq = FVector2D::DistSquared( Building.BuildingPoints[ EdgeIndex ], Building.BuildingPoints[ ( EdgeIndex + 1 ) % NumPoints ] );
			if( EdgeLenSq > LongestEdgeLenSq )
			{
				LongestEdgeLenSq = EdgeLenSq;
				DoorEdgeIndex = EdgeIndex;
			}
		}

		if( DoorMesh != nullptr )
		{
			const FVector2D& A = Building.BuildingPoints[ DoorEdgeIndex ];
			const FVector2D& B = Building.BuildingPoints[ ( DoorEdgeIndex + 1 ) % NumPoints ];
			const FVector2D EdgeMid = ( A + B ) * 0.5;
			const FVector2D EdgeDir = ( B - A ).GetSafeNormal();
			const FVector2D Normal = ComputeOutwardNormal( EdgeDir, EdgeMid, Centroid );

			FTransform DoorTransform( RotatorFacing( Normal ), FVector( (float)EdgeMid.X, (float)EdgeMid.Y, 0.0f ), FVector( DoorMeshScale ) );
			DoorInstances->AddInstance( DoorTransform );
			++DoorsPlaced;
		}

		if( WindowMesh != nullptr )
		{
			for( int32 EdgeIndex = 0; EdgeIndex < NumPoints; ++EdgeIndex )
			{
				const FVector2D& A = Building.BuildingPoints[ EdgeIndex ];
				const FVector2D& B = Building.BuildingPoints[ ( EdgeIndex + 1 ) % NumPoints ];
				const FVector2D Delta = B - A;
				const double EdgeLen = Delta.Size();
				if( EdgeLen < MinEdgeLengthForWindows )
				{
					continue;
				}

				const FVector2D EdgeDir = Delta / EdgeLen;
				const FVector2D EdgeMid = ( A + B ) * 0.5;
				const FVector2D Normal = ComputeOutwardNormal( EdgeDir, EdgeMid, Centroid );
				const FRotator InstanceRotation = RotatorFacing( Normal );

				// If this is the door's edge, keep a clear span around the door's midpoint.
				const bool bIsDoorEdge = ( EdgeIndex == DoorEdgeIndex ) && ( DoorMesh != nullptr );
				const double DoorSpanStart = bIsDoorEdge ? ( EdgeLen * 0.5 - DoorClearance ) : -1.0;
				const double DoorSpanEnd = bIsDoorEdge ? ( EdgeLen * 0.5 + DoorClearance ) : -1.0;

				int32 SlotWithinEdge = 0;
				for( int32 FloorIndex = 0; FloorIndex < NumFloors; ++FloorIndex )
				{
					const float FloorBaseZ = FloorHeight * (float)FloorIndex;

					for( double Dist = CornerMargin; Dist <= EdgeLen - CornerMargin + KINDA_SMALL_NUMBER; Dist += WindowSpacing, ++SlotWithinEdge )
					{
						if( bIsDoorEdge && FloorIndex == 0 && Dist > DoorSpanStart && Dist < DoorSpanEnd )
						{
							continue;
						}

						const int32 DetailKey = FloorIndex * 100000 + EdgeIndex * 1000 + SlotWithinEdge;
						if( DeterministicRandom01( BuildingIndex, DetailKey, 104729 ) < WindowSkipProbability )
						{
							continue;
						}

						const FVector2D Pos = A + EdgeDir * Dist;
						const FVector Loc( (float)Pos.X, (float)Pos.Y, FloorBaseZ + WindowSillHeightOffset );
						FTransform WindowTransform( InstanceRotation, Loc, FVector( WindowMeshScale ) );
						WindowInstances->AddInstance( WindowTransform );
						++WindowsPlaced;
					}
				}
			}
		}

		++BuildingsProcessed;
	}

	UE_LOG( LogStreetMapBuildingDetails, Log, TEXT("GenerateBuildingDetails: processed %d buildings, placed %d doors and %d windows."),
		BuildingsProcessed, DoorsPlaced, WindowsPlaced );
}
