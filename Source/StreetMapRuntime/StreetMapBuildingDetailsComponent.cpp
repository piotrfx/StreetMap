#include "StreetMapBuildingDetailsComponent.h"
#include "StreetMap.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "StreetMapTerrainUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogStreetMapBuildingDetails, Log, All);


UStreetMapBuildingDetailsComponent::UStreetMapBuildingDetailsComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// WindowInstances/DoorInstances are created and attached by AStreetMapBuildingDetailsActor's
	// constructor instead (see InitializeInstanceComponents) -- see the comment on that function for why.
}


namespace StreetMapBuildingDetailsPrivate
{
	// Shoelace formula: positive for a CCW-wound polygon, negative for CW. Computed once per building
	// so every edge's outward normal follows the same fixed rule below -- unlike a per-edge
	// centroid-side check (the previous approach here), this can't flip sign at a concave/reflex
	// corner, which is exactly where that heuristic broke (confirmed visually: windows on a notched
	// corner edge ended up rotated 90 degrees, standing on-edge instead of flush with the wall).
	double ComputePolygonSignedArea( const TArray<FVector2D>& Points )
	{
		double SignedArea = 0.0;
		const int32 NumPoints = Points.Num();
		for( int32 Index = 0; Index < NumPoints; ++Index )
		{
			const FVector2D& A = Points[ Index ];
			const FVector2D& B = Points[ ( Index + 1 ) % NumPoints ];
			SignedArea += A.X * B.Y - B.X * A.Y;
		}
		return SignedArea * 0.5;
	}

	// Perpendicular to EdgeDir, pointing outward given the polygon's winding direction.
	FVector2D ComputeOutwardNormal( const FVector2D& EdgeDir, bool bIsCounterClockwise )
	{
		return bIsCounterClockwise ? FVector2D( EdgeDir.Y, -EdgeDir.X ) : FVector2D( -EdgeDir.Y, EdgeDir.X );
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

	// SetupAttachment() in the owning actor's constructor does not reliably take effect for these
	// components (confirmed via live inspection: GetAttachParent() came back null on freshly spawned
	// actors despite the constructor call) -- attach explicitly here instead, which does work.
	if( WindowInstances->GetAttachParent() != this )
	{
		WindowInstances->AttachToComponent( this, FAttachmentTransformRules::KeepRelativeTransform );
	}
	if( DoorInstances->GetAttachParent() != this )
	{
		DoorInstances->AttachToComponent( this, FAttachmentTransformRules::KeepRelativeTransform );
	}

	WindowInstances->SetStaticMesh( WindowMesh );
	DoorInstances->SetStaticMesh( DoorMesh );

	// Resolved once per generation -- see StreetMapComponent::GenerateMesh's identical pattern for why
	// this goes through the owning actor's transform. Deliberately independent of any StreetMapComponent's
	// own sample: this component lives on its own actor (AStreetMapBuildingDetailsActor), which can have
	// a different transform than the StreetMapActor its StreetMap data came from.
	ALandscapeProxy* TerrainLandscapePtr = TerrainLandscape.LoadSynchronous();
	const FTransform OwnerTransform = GetOwner() ? GetOwner()->GetActorTransform() : FTransform::Identity;

	// Sampled at each instance's own placement position (not once per whole building) -- a single
	// footprint polygon often represents a whole terrace row, and a single shared base Z visibly floats
	// or sinks relative to the actual wall on sloped terrain once the wall itself follows the ground
	// (see StreetMapComponent::GenerateMesh's per-edge subdivision).
	auto SampleDetailZ = [&]( const FVector2D& Pos ) -> float
	{
		float SampledZ = 0.0f;
		if( TerrainLandscapePtr != nullptr )
		{
			StreetMapTerrainUtils::TrySampleLocalZ( TerrainLandscapePtr, OwnerTransform, Pos, SampledZ );
		}
		return SampledZ;
	};

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

		const bool bIsCounterClockwise = ComputePolygonSignedArea( Building.BuildingPoints ) > 0.0;
		const int32 NumFloors = FMath::Max( 1, FMath::RoundToInt( TotalHeight / FloorHeight ) );

		// Longest edge is the building's front -- a single footprint polygon often represents a whole
		// terrace row sharing one wall, not one house (confirmed by the roof generator: it draws
		// exactly one gable per building record, so N visible roof peaks along this wall means N
		// actual houses here). Subdivide it into HouseWidth-wide segments and repeat door+windows once
		// per house rather than once for the whole wall.
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

		const FVector2D& DoorA = Building.BuildingPoints[ DoorEdgeIndex ];
		const FVector2D& DoorB = Building.BuildingPoints[ ( DoorEdgeIndex + 1 ) % NumPoints ];
		const FVector2D DoorEdgeDelta = DoorB - DoorA;
		const double DoorEdgeLen = DoorEdgeDelta.Size();

		// Places windows (per HouseWidth segment, per floor) along a given edge. bFrontHasDoor makes
		// the ground floor put just one window beside the door instead of two, alternating sides per
		// house; every floor above ground -- and every floor on a non-front wall -- gets both sides.
		auto PlaceWindowsOnEdge = [&]( int32 EdgeIndexToUse, bool bFrontHasDoor )
		{
			if( WindowMesh == nullptr )
			{
				return;
			}

			const FVector2D& EA = Building.BuildingPoints[ EdgeIndexToUse ];
			const FVector2D& EB = Building.BuildingPoints[ ( EdgeIndexToUse + 1 ) % NumPoints ];
			const FVector2D EDelta = EB - EA;
			const double ELen = EDelta.Size();
			if( ELen <= KINDA_SMALL_NUMBER )
			{
				return;
			}

			const FVector2D EDir = EDelta / ELen;
			const FVector2D ENormal = ComputeOutwardNormal( EDir, bIsCounterClockwise );
			FRotator ERotation = RotatorFacing( ENormal );
			ERotation.Yaw += WindowRotationOffsetYaw;
			ERotation.Pitch += WindowRotationOffsetPitch;
			ERotation.Roll += WindowRotationOffsetRoll;

			const int32 NumHouses = FMath::Max( 1, FMath::RoundToInt( ELen / HouseWidth ) );
			const double PerHouseWidth = ELen / (double)NumHouses;
			if( PerHouseWidth < MinEdgeLengthForWindows )
			{
				return;
			}

			for( int32 HouseIndex = 0; HouseIndex < NumHouses; ++HouseIndex )
			{
				const double HouseStart = HouseIndex * PerHouseWidth;
				const double HouseCenter = HouseStart + PerHouseWidth * 0.5;

				for( int32 FloorIndex = 0; FloorIndex < NumFloors; ++FloorIndex )
				{
					const bool bAsymmetric = bFrontHasDoor && FloorIndex == 0;
					const bool bWantLeft = !bAsymmetric || ( ( HouseIndex & 1 ) == 0 );
					const bool bWantRight = !bAsymmetric || ( ( HouseIndex & 1 ) != 0 );

					if( bWantLeft )
					{
						const double Dist = HouseCenter - WindowSpacing;
						if( Dist >= HouseStart + CornerMargin )
						{
							const FVector2D Pos = EA + EDir * Dist;
							const float FloorZ = SampleDetailZ( Pos ) + WindowSillHeightOffset + FloorHeight * (float)FloorIndex;
							FTransform WindowTransform( ERotation, FVector( (float)Pos.X, (float)Pos.Y, FloorZ ), FVector( WindowMeshScale ) );
							WindowInstances->AddInstance( WindowTransform );
							++WindowsPlaced;
						}
					}
					if( bWantRight )
					{
						const double Dist = HouseCenter + WindowSpacing;
						if( Dist <= HouseStart + PerHouseWidth - CornerMargin )
						{
							const FVector2D Pos = EA + EDir * Dist;
							const float FloorZ = SampleDetailZ( Pos ) + WindowSillHeightOffset + FloorHeight * (float)FloorIndex;
							FTransform WindowTransform( ERotation, FVector( (float)Pos.X, (float)Pos.Y, FloorZ ), FVector( WindowMeshScale ) );
							WindowInstances->AddInstance( WindowTransform );
							++WindowsPlaced;
						}
					}
				}
			}
		};

		if( DoorEdgeLen > KINDA_SMALL_NUMBER )
		{
			const FVector2D DoorEdgeDir = DoorEdgeDelta / DoorEdgeLen;
			const FVector2D DoorNormal = ComputeOutwardNormal( DoorEdgeDir, bIsCounterClockwise );

			FRotator DoorRotation = RotatorFacing( DoorNormal );
			DoorRotation.Yaw += DoorRotationOffsetYaw;
			DoorRotation.Pitch += DoorRotationOffsetPitch;
			DoorRotation.Roll += DoorRotationOffsetRoll;

			if( DoorMesh != nullptr )
			{
				const int32 NumHouses = FMath::Max( 1, FMath::RoundToInt( DoorEdgeLen / HouseWidth ) );
				const double PerHouseWidth = DoorEdgeLen / (double)NumHouses;
				for( int32 HouseIndex = 0; HouseIndex < NumHouses; ++HouseIndex )
				{
					const double HouseCenter = ( HouseIndex + 0.5 ) * PerHouseWidth;
					const FVector2D Pos = DoorA + DoorEdgeDir * HouseCenter;
					FTransform DoorTransform( DoorRotation, FVector( (float)Pos.X, (float)Pos.Y, SampleDetailZ( Pos ) + DoorSillHeightOffset ), FVector( DoorMeshScale ) );
					DoorInstances->AddInstance( DoorTransform );
					++DoorsPlaced;
				}
			}

			PlaceWindowsOnEdge( DoorEdgeIndex, /*bFrontHasDoor=*/ true );

			// Back wall: the edge most nearly opposite the front (its direction most anti-parallel to
			// the door edge's), so terraces get at least some windows on the side away from the street
			// too, not just the front facade.
			int32 BackEdgeIndex = INDEX_NONE;
			double BestDot = 1.0;
			for( int32 EdgeIndex = 0; EdgeIndex < NumPoints; ++EdgeIndex )
			{
				if( EdgeIndex == DoorEdgeIndex )
				{
					continue;
				}
				const FVector2D& EA = Building.BuildingPoints[ EdgeIndex ];
				const FVector2D& EB = Building.BuildingPoints[ ( EdgeIndex + 1 ) % NumPoints ];
				const FVector2D EDelta = EB - EA;
				const double ELen = EDelta.Size();
				if( ELen < MinEdgeLengthForWindows )
				{
					continue;
				}
				const double Dot = FVector2D::DotProduct( EDelta / ELen, DoorEdgeDir );
				if( Dot < BestDot )
				{
					BestDot = Dot;
					BackEdgeIndex = EdgeIndex;
				}
			}

			if( BackEdgeIndex != INDEX_NONE )
			{
				PlaceWindowsOnEdge( BackEdgeIndex, /*bFrontHasDoor=*/ false );
			}
		}

		++BuildingsProcessed;
	}

	UE_LOG( LogStreetMapBuildingDetails, Log, TEXT("GenerateBuildingDetails: processed %d buildings, placed %d doors and %d windows."),
		BuildingsProcessed, DoorsPlaced, WindowsPlaced );
}
