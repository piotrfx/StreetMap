#include "StreetMapComponent.h"

#include "StreetMapSceneProxy.h"
#include "NavigationSystem.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "PolygonTools.h"
#include "Rendering/StaticMeshVertexBuffer.h"
#include "StaticMeshResources.h"
#include "PhysicsEngine/BodySetup.h"
#include "UObject/ConstructorHelpers.h"
#include "StreetMapTerrainUtils.h"

#if WITH_EDITOR
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#endif //WITH_EDITOR

UStreetMapComponent::UStreetMapComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, StreetMap(nullptr)
	, CachedLocalBounds(ForceInit)
{
	// We make sure our mesh collision profile name is set to NoCollisionProfileName at initialization. 
	// Because we don't have collision data yet!
	SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);

	// We don't currently need to be ticked.  This can be overridden in a derived class though.
	PrimaryComponentTick.bCanEverTick = false;
	this->bAutoActivate = false;	// NOTE: Components instantiated through C++ are not automatically active, so they'll only tick once and then go to sleep!

	// We don't currently need InitializeComponent() to be called on us.  This can be overridden in a
	// derived class though.
	bWantsInitializeComponent = false;

	// Turn on shadows.  It looks better.
	CastShadow = true;

	// Our mesh is too complicated to be a useful occluder.
	bUseAsOccluder = false;

	// Our mesh can influence navigation.
	bCanEverAffectNavigation = true;

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> DefaultMaterialAsset(TEXT("/StreetMap/StreetMapDefaultMaterial"));
	StreetMapDefaultMaterial = DefaultMaterialAsset.Object;

}

UMaterialInterface* UStreetMapComponent::GetDefaultMaterial() const
{
	return StreetMapDefaultMaterial != nullptr ? StreetMapDefaultMaterial : UMaterial::GetDefaultMaterial(MD_Surface);
}


FPrimitiveSceneProxy* UStreetMapComponent::CreateSceneProxy()
{
	FStreetMapSceneProxy* StreetMapSceneProxy = nullptr;

	if( HasValidMesh() )
	{
		StreetMapSceneProxy = new FStreetMapSceneProxy( this );
		StreetMapSceneProxy->Init( this, Vertices, Indices );
	}
	
	return StreetMapSceneProxy;
}


int32 UStreetMapComponent::GetNumMaterials() const
{
	// NOTE: This is a bit of a weird thing about Unreal that we need to deal with when defining a component that
	// can have materials assigned.  UPrimitiveComponent::GetNumMaterials() will return 0, so we need to override it 
	// to return the number of overridden materials, which are the actual materials assigned to the component.
	return HasValidMesh() ? GetNumMeshSections() : GetNumOverrideMaterials();
}


void UStreetMapComponent::SetStreetMap(class UStreetMap* NewStreetMap, bool bClearPreviousMeshIfAny /*= false*/, bool bRebuildMesh /*= false */)
{
	if (StreetMap != NewStreetMap)
	{
		StreetMap = NewStreetMap;

		if (bClearPreviousMeshIfAny)
			InvalidateMesh();

		if (bRebuildMesh)
			BuildMesh();
	}
}


bool UStreetMapComponent::GetPhysicsTriMeshData(struct FTriMeshCollisionData* CollisionData, bool InUseAllTriData)
{

	if (!CollisionSettings.bGenerateCollision || !HasValidMesh())
	{
		return false;
	}

	// Copy vertices data
	const int32 NumVertices = Vertices.Num();
	CollisionData->Vertices.Empty();
	CollisionData->Vertices.AddUninitialized(NumVertices);

	for (int32 VertexIndex = 0; VertexIndex < NumVertices; VertexIndex++)
	{
		CollisionData->Vertices[VertexIndex] = Vertices[VertexIndex].Position;
	}

	// Copy indices data
	const int32 NumTriangles = Indices.Num() / 3;
	FTriIndices TempTriangle;
	for (int32 TriangleIndex = 0; TriangleIndex < NumTriangles * 3; TriangleIndex += 3)
	{

		TempTriangle.v0 = Indices[TriangleIndex + 0];
		TempTriangle.v1 = Indices[TriangleIndex + 1];
		TempTriangle.v2 = Indices[TriangleIndex + 2];


		CollisionData->Indices.Add(TempTriangle);
		CollisionData->MaterialIndices.Add(0);
	}

	CollisionData->bFlipNormals = true;
	CollisionData->bDeformableMesh = true;

	return HasValidMesh();
}


bool UStreetMapComponent::ContainsPhysicsTriMeshData(bool InUseAllTriData) const
{
	return HasValidMesh() && CollisionSettings.bGenerateCollision;
}


bool UStreetMapComponent::WantsNegXTriMesh()
{
	return false;
}


void UStreetMapComponent::CreateBodySetupIfNeeded(bool bForceCreation /*= false*/)
{
	if (StreetMapBodySetup == nullptr || bForceCreation == true)
	{
		// Creating new BodySetup Object.
		StreetMapBodySetup = NewObject<UBodySetup>(this);
		StreetMapBodySetup->BodySetupGuid = FGuid::NewGuid();
		StreetMapBodySetup->bDoubleSidedGeometry = CollisionSettings.bAllowDoubleSidedGeometry;

		// shapes per poly shape for collision (Not working in simulation mode).
		StreetMapBodySetup->CollisionTraceFlag = CTF_UseComplexAsSimple;
	}
}


void UStreetMapComponent::GenerateCollision()
{
	if (!CollisionSettings.bGenerateCollision || !HasValidMesh())
	{
		return;
	}

	// create a new body setup
	CreateBodySetupIfNeeded(true);


	if (GetCollisionProfileName() == UCollisionProfile::NoCollision_ProfileName)
	{
		SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	}

	// Rebuild the body setup
	StreetMapBodySetup->InvalidatePhysicsData();
	StreetMapBodySetup->CreatePhysicsMeshes();
	UpdateNavigationIfNeeded();
}


void UStreetMapComponent::ClearCollision()
{

	if (StreetMapBodySetup != nullptr)
	{
		StreetMapBodySetup->InvalidatePhysicsData();
		StreetMapBodySetup = nullptr;
	}

	if (GetCollisionProfileName() != UCollisionProfile::NoCollision_ProfileName)
	{
		SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	}

	UpdateNavigationIfNeeded();
}

class UBodySetup* UStreetMapComponent::GetBodySetup()
{
	if (CollisionSettings.bGenerateCollision == true)
	{
		// checking if we have a valid body setup. 
		// A new one is created only if a valid body setup is not found.
		CreateBodySetupIfNeeded();
		return StreetMapBodySetup;
	}

	if (StreetMapBodySetup != nullptr) StreetMapBodySetup = nullptr;

	return nullptr;
}

void UStreetMapComponent::GenerateMesh()
{
	/////////////////////////////////////////////////////////
	// Visual tweakables for generated Street Map mesh
	//
	const float RoadZ = MeshBuildSettings.RoadOffsetZ;
	const bool bWant3DBuildings = MeshBuildSettings.bWant3DBuildings;
	const float BuildingLevelFloorFactor = MeshBuildSettings.BuildingLevelFloorFactor;
	const bool bWantLitBuildings = MeshBuildSettings.bWantLitBuildings;
	const bool bWantBuildingBorderOnGround = !bWant3DBuildings;
	const float StreetThickness = MeshBuildSettings.StreetThickness;
	const FColor StreetColor = MeshBuildSettings.StreetColor.ToFColor( false );
	const float MajorRoadThickness = MeshBuildSettings.MajorRoadThickness;
	const FColor MajorRoadColor = MeshBuildSettings.MajorRoadColor.ToFColor( false );
	const float HighwayThickness = MeshBuildSettings.HighwayThickness;
	const FColor HighwayColor = MeshBuildSettings.HighwayColor.ToFColor( false );
	const float RiverThickness = MeshBuildSettings.RiverThickness;
	const FColor RiverColor = MeshBuildSettings.RiverColor.ToFColor( false );
	const FColor WaterAreaColor = MeshBuildSettings.WaterAreaColor.ToFColor( false );
	const float BuildingBorderThickness = MeshBuildSettings.BuildingBorderThickness;
	FLinearColor BuildingBorderLinearColor = MeshBuildSettings.BuildingBorderLinearColor;
	const float BuildingBorderZ = MeshBuildSettings.BuildingBorderZ;
	const FColor BuildingBorderColor( BuildingBorderLinearColor.ToFColor( false ) );
	const FColor BuildingFillColor( FLinearColor( BuildingBorderLinearColor * 0.33f ).CopyWithNewOpacity( 1.0f ).ToFColor( false ) );
	/////////////////////////////////////////////////////////


	// Flat/horizontal polygons (building caps, water areas) don't have a natural per-edge UV like a road
	// or wall quad does, so approximate one by normalizing each point's XY position against the polygon's
	// own XY bounding box -- gives a full 0-1 UV spread across the shape instead of a single fixed texel,
	// so a tiled material shows real texture detail rather than reading as a flat solid color.
	auto ComputeXYBoundingBoxUVs = []( const TArray<FVector3f>& Points ) -> TArray<FVector2f>
	{
		FVector2D MinP( TNumericLimits<double>::Max(), TNumericLimits<double>::Max() );
		FVector2D MaxP( TNumericLimits<double>::Lowest(), TNumericLimits<double>::Lowest() );
		for( const FVector3f& Point : Points )
		{
			MinP.X = FMath::Min( MinP.X, (double)Point.X ); MinP.Y = FMath::Min( MinP.Y, (double)Point.Y );
			MaxP.X = FMath::Max( MaxP.X, (double)Point.X ); MaxP.Y = FMath::Max( MaxP.Y, (double)Point.Y );
		}
		const FVector2D Extent = MaxP - MinP;

		TArray<FVector2f> UVs;
		UVs.Reserve( Points.Num() );
		for( const FVector3f& Point : Points )
		{
			const float U = Extent.X > KINDA_SMALL_NUMBER ? (float)( ( (double)Point.X - MinP.X ) / Extent.X ) : 0.0f;
			const float V = Extent.Y > KINDA_SMALL_NUMBER ? (float)( ( (double)Point.Y - MinP.Y ) / Extent.Y ) : 0.0f;
			UVs.Add( FVector2f( U, V ) );
		}
		return UVs;
	};

	CachedLocalBounds = FBox( ForceInit );
	Vertices.Reset();
	Indices.Reset();

	if( StreetMap != nullptr )
	{
		FBox3f MeshBoundingBox;
		MeshBoundingBox.Init();

		// Resolved once per generation -- LocalXY is in this component's owner's local space (the same
		// space Road.RoadPoints/Building.BuildingPoints are already stored in), converted to world space
		// via OwnerTransform before querying the Landscape, then back to local space for the mesh data.
		ALandscapeProxy* TerrainLandscapePtr = TerrainLandscape.LoadSynchronous();
		const FTransform OwnerTransform = GetOwner() ? GetOwner()->GetActorTransform() : FTransform::Identity;
		auto SampleLocalZ = [&]( const FVector2D& LocalXY, float FallbackZ ) -> float
		{
			float SampledZ;
			if( TerrainLandscapePtr != nullptr && StreetMapTerrainUtils::TrySampleLocalZ( TerrainLandscapePtr, OwnerTransform, LocalXY, SampledZ ) )
			{
				return SampledZ;
			}
			return FallbackZ;
		};

		// Roads sit exactly at sampled ground height by default, which is prone to z-fighting against the
		// Landscape surface directly underneath. RoadOffsetZ (already an exposed, live-tunable property)
		// now also doubles as a small lift above the sampled terrain, not just the flat-fallback value.
		auto SampleRoadZ = [&]( const FVector2D& LocalXY ) -> float
		{
			float SampledZ;
			if( TerrainLandscapePtr != nullptr && StreetMapTerrainUtils::TrySampleLocalZ( TerrainLandscapePtr, OwnerTransform, LocalXY, SampledZ ) )
			{
				return SampledZ + RoadZ;
			}
			return RoadZ;
		};

		const auto& Roads = StreetMap->GetRoads();
		const auto& Nodes = StreetMap->GetNodes();
		const auto& WaterAreas = StreetMap->GetWaterAreas();
		const auto& Buildings = StreetMap->GetBuildings();

		for( const auto& Road : Roads )
		{
			// Rivers are drawn from the Roads array but conceptually belong to the Water layer.
			const bool bIsRiver = ( Road.RoadType == EStreetMapRoadType::River );
			const bool bWantThisRoad = ( MeshLayer == EStreetMapMeshLayer::All )
				|| ( bIsRiver && MeshLayer == EStreetMapMeshLayer::Water )
				|| ( !bIsRiver && MeshLayer == EStreetMapMeshLayer::Roads );
			if( !bWantThisRoad )
			{
				continue;
			}

			float RoadThickness = StreetThickness;
			FColor RoadColor = StreetColor;
			switch( Road.RoadType )
			{
				case EStreetMapRoadType::Highway:
					RoadThickness = HighwayThickness;
					RoadColor = HighwayColor;
					break;
					
				case EStreetMapRoadType::MajorRoad:
					RoadThickness = MajorRoadThickness;
					RoadColor = MajorRoadColor;
					break;

				case EStreetMapRoadType::River:
					RoadThickness = RiverThickness;
					RoadColor = RiverColor;
					break;

				case EStreetMapRoadType::Street:
				case EStreetMapRoadType::Other:
					break;
					
				default:
					check( 0 );
					break;
			}
			
			// OSM nodes on a long straight road can be tens of meters apart -- sampling terrain height only
			// at those nodes and bridging with a single straight ribbon lets the road float over any dip
			// (or cut through any rise) that falls between them. Subdivide each segment so height is
			// resampled at least every MaxRoadSubdivisionLength, following the ground in between too.
			const float MaxRoadSubdivisionLength = 1000.0f; // cm
			for( int32 PointIndex = 0; PointIndex < Road.RoadPoints.Num() - 1; ++PointIndex )
			{
				const FVector2D SegmentStart = Road.RoadPoints[ PointIndex ];
				const FVector2D SegmentEnd = Road.RoadPoints[ PointIndex + 1 ];
				const int32 NumSubSegments = FMath::Max( 1, FMath::CeilToInt( FVector2D::Distance( SegmentStart, SegmentEnd ) / MaxRoadSubdivisionLength ) );

				FVector2D PrevSubPoint = SegmentStart;
				float PrevSubZ = SampleRoadZ( SegmentStart );
				for( int32 SubIndex = 1; SubIndex <= NumSubSegments; ++SubIndex )
				{
					const FVector2D SubPoint = FMath::Lerp( SegmentStart, SegmentEnd, (double)SubIndex / (double)NumSubSegments );
					const float SubZ = SampleRoadZ( SubPoint );
					AddThick2DLine(
						FVector2f(PrevSubPoint),
						FVector2f(SubPoint),
						PrevSubZ,
						SubZ,
						RoadThickness,
						RoadColor,
						RoadColor,
						MeshBoundingBox );
					PrevSubPoint = SubPoint;
					PrevSubZ = SubZ;
				}
			}

			// Fill the gap/overlap left at each interior bend between two independently-capped segments
			// (see AddRoadJoin) so the road reads as a fluent line rather than a chain of angled rectangles.
			for( int32 PointIndex = 1; PointIndex < Road.RoadPoints.Num() - 1; ++PointIndex )
			{
				const FVector2f PrevPoint( Road.RoadPoints[ PointIndex - 1 ] );
				const FVector2f JointPoint( Road.RoadPoints[ PointIndex ] );
				const FVector2f NextPoint( Road.RoadPoints[ PointIndex + 1 ] );

				const FVector2f PrevDirection = ( JointPoint - PrevPoint ).GetSafeNormal();
				const FVector2f NextDirection = ( NextPoint - JointPoint ).GetSafeNormal();

				const float JointZ = SampleRoadZ( Road.RoadPoints[ PointIndex ] );
				AddRoadJoin( JointPoint, PrevDirection, NextDirection, JointZ, RoadThickness, RoadColor, MeshBoundingBox );
			}
		}
		
		TArray< int32 > TempIndices;
		TArray< int32 > TriangulatedVertexIndices;
		TArray< FVector3f > TempPoints;
		TArray< FVector2f > TempUVs;
		const bool bWantBuildings = ( MeshLayer == EStreetMapMeshLayer::All
			|| MeshLayer == EStreetMapMeshLayer::Buildings
			|| MeshLayer == EStreetMapMeshLayer::BuildingsResidential
			|| MeshLayer == EStreetMapMeshLayer::BuildingsCommercial
			|| MeshLayer == EStreetMapMeshLayer::BuildingsResidentialWalls
			|| MeshLayer == EStreetMapMeshLayer::BuildingsResidentialRoof );
		// Roof-only and walls-only layers are both restricted to residential buildings (only they get a
		// gable roof at all -- see bWantGableRoof below), so they reuse the same residential-only filter.
		const bool bLayerIsResidentialOnly = ( MeshLayer == EStreetMapMeshLayer::BuildingsResidential
			|| MeshLayer == EStreetMapMeshLayer::BuildingsResidentialWalls
			|| MeshLayer == EStreetMapMeshLayer::BuildingsResidentialRoof );
		// Within a residential-only layer, walls and roof can additionally be split into their own
		// components/actors so each can carry a different material (e.g. brick walls, tiled roof).
		const bool bWantRoofGeometry = ( MeshLayer != EStreetMapMeshLayer::BuildingsResidentialWalls );
		const bool bWantWallGeometry = ( MeshLayer != EStreetMapMeshLayer::BuildingsResidentialRoof );
		for( int32 BuildingIndex = 0; bWantBuildings && BuildingIndex < Buildings.Num(); ++BuildingIndex )
		{
			const auto& Building = Buildings[ BuildingIndex ];

			// When split by residential/commercial, skip buildings that don't belong to this layer.
			if( ( bLayerIsResidentialOnly && !Building.bIsResidential )
				|| ( MeshLayer == EStreetMapMeshLayer::BuildingsCommercial && Building.bIsResidential ) )
			{
				continue;
			}

			// Building mesh (or filled area, if the building has no height)

			// Base Z for this building: sampled once at the footprint centroid (not per-vertex -- the
			// gable-roof generator below requires one flat eave plane, and footprints are small
			// relative to typical terrain relief, so a single flat base per building stays visually
			// correct). Falls back to the original flat local Z=0 where no TerrainLandscape is set.
			// Computed here (rather than inside the triangulation block below) since the building-border
			// pass further down needs it too, regardless of whether triangulation succeeds.
			FVector2D BuildingCentroid( 0.0, 0.0 );
			for( const FVector2D& BuildingPoint : Building.BuildingPoints )
			{
				BuildingCentroid += BuildingPoint;
			}
			BuildingCentroid /= (double)Building.BuildingPoints.Num();
			const float BuildingBaseZ = SampleLocalZ( BuildingCentroid, 0.0f );

			// Per-edge terrain sampling for lit walls and the gable roof (below) -- lets long terrace
			// walls/eaves follow real ground contour instead of sitting at one flat BuildingBaseZ.
			// Subdivides an edge into steps no longer than MaxWallSubdivisionLength, sampling height at
			// every step (not just the two endpoints), so adjacent sub-quads/strips share an exact
			// boundary sample and never show a seam. Computed unconditionally (like BuildingBaseZ above)
			// so a walls-only or roof-only component/actor (see MeshLayer split below) computes identical
			// results to a combined one -- the eave/wall-top boundary must line up exactly either way.
			const float MaxWallSubdivisionLength = 400.0f; // cm
			auto SampleEdgeProfile = [&]( int32 A_Index, int32 B_Index ) -> TArray<TPair<FVector2D,float>>
			{
				const FVector2D& A = Building.BuildingPoints[ A_Index ];
				const FVector2D& B = Building.BuildingPoints[ B_Index ];
				const int32 N = FMath::Max( 1, FMath::CeilToInt( FVector2D::Distance( A, B ) / MaxWallSubdivisionLength ) );
				TArray<TPair<FVector2D,float>> Profile;
				Profile.Reserve( N + 1 );
				for( int32 SubIndex = 0; SubIndex <= N; ++SubIndex )
				{
					const FVector2D P = FMath::Lerp( A, B, (double)SubIndex / (double)N );
					Profile.Add( TPair<FVector2D,float>( P, SampleLocalZ( P, 0.0f ) ) );
				}
				return Profile;
			};

			// Triangulate this building
			// @todo: Performance: Triangulating lots of building polygons is quite slow.  We could easily do this
			//        as part of the import process and store tessellated geometry instead of doing this at load time.
			bool WindsClockwise;
			if( FPolygonTools::TriangulatePolygon( Building.BuildingPoints, TempIndices, /* Out */ TriangulatedVertexIndices, /* Out */ WindsClockwise ) )
			{
				// @todo: Performance: We could preprocess the building shapes so that the points always wind
				//        in a consistent direction, so we can skip determining the winding above.

				const int32 FirstTopVertexIndex = this->Vertices.Num();

				// calculate fill Z for buildings
				// either use the defined height or extrapolate from building level count
				float BuildingHeightAboveBase = 0.0f;
				if (bWant3DBuildings) {
					if (Building.Height > 0) {
						BuildingHeightAboveBase = Building.Height;
					}
					else if (Building.BuildingLevels > 0) {
						BuildingHeightAboveBase = (float)Building.BuildingLevels * BuildingLevelFloorFactor;
					}
				}
				const float BuildingFillZ = BuildingBaseZ + BuildingHeightAboveBase;

				// Top of building: a pitched (gable) roof for residential buildings, a flat cap otherwise.
				// The gable roof is approximated from an oriented bounding rectangle (aligned to the
				// footprint's longest edge) rather than the exact polygon, so it may not perfectly
				// hug very irregular (e.g. L-shaped) footprints -- an acceptable tradeoff given most
				// OSM house footprints are roughly rectangular.
				const bool bWantGableRoof = Building.bIsResidential && BuildingHeightAboveBase > KINDA_SMALL_NUMBER;
				if( bWantGableRoof && bWantRoofGeometry )
				{
					// Emits both winding orders for each face, so it renders regardless of which
					// side is "front" per the engine's culling convention -- simpler and more
					// robust than trying to compute the correct single winding for every one of
					// these faces, at the cost of a small amount of extra (harmless) geometry.
					auto AddQuad = [&]( const FVector3f& P0, const FVector3f& P1, const FVector3f& P2, const FVector3f& P3 )
					{
						TempPoints.SetNum( 4, EAllowShrinking::No );
						TempPoints[0] = P0; TempPoints[1] = P1; TempPoints[2] = P2; TempPoints[3] = P3;
						TempUVs.SetNum( 4, EAllowShrinking::No );
						TempUVs[0] = FVector2f(0,0); TempUVs[1] = FVector2f(1,0); TempUVs[2] = FVector2f(1,1); TempUVs[3] = FVector2f(0,1);
						TempIndices.SetNum( 12, EAllowShrinking::No );
						TempIndices[0] = 0; TempIndices[1] = 1; TempIndices[2] = 2;
						TempIndices[3] = 0; TempIndices[4] = 2; TempIndices[5] = 3;
						TempIndices[6] = 2; TempIndices[7] = 1; TempIndices[8] = 0;
						TempIndices[9] = 3; TempIndices[10] = 2; TempIndices[11] = 0;
						AddTriangles( TempPoints, TempUVs, TempIndices, FVector3f::ForwardVector, FVector3f::UpVector, BuildingFillColor, MeshBoundingBox );
					};
					auto AddTri = [&]( const FVector3f& P0, const FVector3f& P1, const FVector3f& P2 )
					{
						TempPoints.SetNum( 3, EAllowShrinking::No );
						TempPoints[0] = P0; TempPoints[1] = P1; TempPoints[2] = P2;
						TempUVs.SetNum( 3, EAllowShrinking::No );
						TempUVs[0] = FVector2f(0,0); TempUVs[1] = FVector2f(1,0); TempUVs[2] = FVector2f(0.5f,1);
						TempIndices.SetNum( 6, EAllowShrinking::No );
						TempIndices[0] = 0; TempIndices[1] = 1; TempIndices[2] = 2;
						TempIndices[3] = 2; TempIndices[4] = 1; TempIndices[5] = 0;
						AddTriangles( TempPoints, TempUVs, TempIndices, FVector3f::ForwardVector, FVector3f::UpVector, BuildingFillColor, MeshBoundingBox );
					};

					// Ridge axis: direction of the footprint's longest edge, instead of the world X/Y axes --
					// most real building footprints are rotated relative to the map's lat/long grid, and
					// an axis-aligned bounding box for a rotated rectangle balloons out well past its
					// actual walls.
					FVector2D RidgeDir( 1.0, 0.0 );
					{
						double LongestEdgeLenSq = 0.0;
						const int32 NumPts = Building.BuildingPoints.Num();
						for( int32 PointIndex = 0; PointIndex < NumPts; ++PointIndex )
						{
							const FVector2D& A = Building.BuildingPoints[ PointIndex ];
							const FVector2D& B = Building.BuildingPoints[ ( PointIndex + 1 ) % NumPts ];
							const double EdgeLenSq = FVector2D::DistSquared( A, B );
							if( EdgeLenSq > LongestEdgeLenSq )
							{
								LongestEdgeLenSq = EdgeLenSq;
								RidgeDir = ( B - A ).GetSafeNormal();
							}
						}
					}
					const FVector2D PerpDir( -RidgeDir.Y, RidgeDir.X );

					// Project the footprint onto the (RidgeDir, PerpDir) axes to get an oriented
					// bounding rectangle that hugs a rotated building much more closely than the
					// axis-aligned BoundsMin/BoundsMax would.
					double UMin = TNumericLimits<double>::Max(), UMax = TNumericLimits<double>::Lowest();
					double VMin = TNumericLimits<double>::Max(), VMax = TNumericLimits<double>::Lowest();
					for( const FVector2D& Point : Building.BuildingPoints )
					{
						const double U = FVector2D::DotProduct( Point, RidgeDir );
						const double V = FVector2D::DotProduct( Point, PerpDir );
						UMin = FMath::Min( UMin, U ); UMax = FMath::Max( UMax, U );
						VMin = FMath::Min( VMin, V ); VMax = FMath::Max( VMax, V );
					}
					const double VMid = ( VMin + VMax ) * 0.5;
					const float RidgeRiseOffset = FMath::Clamp( 0.35f * (float)( VMax - VMin ), 150.0f, 600.0f );

					auto MakePoint = [&]( double U, double V, float Z ) -> FVector3f
					{
						const FVector2D WorldXY = RidgeDir * U + PerpDir * V;
						return FVector3f( (float)WorldXY.X, (float)WorldXY.Y, Z );
					};

					// Sample front (V=VMin) and back (V=VMax) eave height along a fixed U grid derived
					// from the oriented bounding box itself -- NOT by searching the footprint polygon for
					// a matching "front"/"back" edge pair (an earlier version of this code did that, and
					// it proved unreliable on real terrace data: many small facade jogs/notches let the
					// search grab a mismatched, oddly-short edge, giving the two sides different U-ranges/
					// point-counts and producing a fragmented, self-intersecting roof). The bounding box's
					// own U range is always well-defined and identical for both sides, so front and back
					// stay perfectly aligned regardless of how messy the actual polygon is.
					const int32 NumRoofSteps = FMath::Max( 1, FMath::CeilToInt( ( UMax - UMin ) / MaxWallSubdivisionLength ) );
					TArray<float> FrontZ, BackZ, RidgeZArr;
					FrontZ.SetNum( NumRoofSteps + 1 );
					BackZ.SetNum( NumRoofSteps + 1 );
					RidgeZArr.SetNum( NumRoofSteps + 1 );
					for( int32 StepIndex = 0; StepIndex <= NumRoofSteps; ++StepIndex )
					{
						const double U = FMath::Lerp( UMin, UMax, (double)StepIndex / (double)NumRoofSteps );
						FrontZ[ StepIndex ] = SampleLocalZ( RidgeDir * U + PerpDir * VMin, BuildingBaseZ ) + BuildingHeightAboveBase;
						BackZ[ StepIndex ] = SampleLocalZ( RidgeDir * U + PerpDir * VMax, BuildingBaseZ ) + BuildingHeightAboveBase;
						RidgeZArr[ StepIndex ] = 0.5f * ( FrontZ[ StepIndex ] + BackZ[ StepIndex ] ) + RidgeRiseOffset;
					}

					for( int32 StepIndex = 0; StepIndex < NumRoofSteps; ++StepIndex )
					{
						const double U0 = FMath::Lerp( UMin, UMax, (double)StepIndex / (double)NumRoofSteps );
						const double U1 = FMath::Lerp( UMin, UMax, (double)( StepIndex + 1 ) / (double)NumRoofSteps );
						AddQuad(
							MakePoint( U0, VMin, FrontZ[ StepIndex ] ), MakePoint( U1, VMin, FrontZ[ StepIndex + 1 ] ),
							MakePoint( U1, VMid, RidgeZArr[ StepIndex + 1 ] ), MakePoint( U0, VMid, RidgeZArr[ StepIndex ] ) );
						AddQuad(
							MakePoint( U1, VMax, BackZ[ StepIndex + 1 ] ), MakePoint( U0, VMax, BackZ[ StepIndex ] ),
							MakePoint( U0, VMid, RidgeZArr[ StepIndex ] ), MakePoint( U1, VMid, RidgeZArr[ StepIndex + 1 ] ) );
					}
					AddTri(
						MakePoint( UMin, VMin, FrontZ[0] ), MakePoint( UMin, VMid, RidgeZArr[0] ), MakePoint( UMin, VMax, BackZ[0] ) );
					AddTri(
						MakePoint( UMax, VMax, BackZ[NumRoofSteps] ), MakePoint( UMax, VMid, RidgeZArr[NumRoofSteps] ), MakePoint( UMax, VMin, FrontZ[NumRoofSteps] ) );
				}
				else if( bWantRoofGeometry )
				{
					// Flat cap (non-residential buildings, or a residential-roof layer's fallback for a
					// zero-height building that never got a gable roof in the first place).
					TempPoints.SetNum( Building.BuildingPoints.Num(), EAllowShrinking::No );
					for( int32 PointIndex = 0; PointIndex < Building.BuildingPoints.Num(); ++PointIndex )
					{
						TempPoints[ PointIndex ] = FVector3f( FVector2f(Building.BuildingPoints[ ( Building.BuildingPoints.Num() - PointIndex ) - 1 ]), BuildingFillZ );
					}
					AddTriangles( TempPoints, ComputeXYBoundingBoxUVs( TempPoints ), TriangulatedVertexIndices, FVector3f::ForwardVector, FVector3f::UpVector, BuildingFillColor, MeshBoundingBox );
				}
				// else: this is a walls-only layer -- deliberately skip the top of the building, it's
				// rendered by a separate roof-only component/actor instead (see bWantRoofGeometry above).

				if( bWantWallGeometry && bWant3DBuildings && (Building.Height > KINDA_SMALL_NUMBER || Building.BuildingLevels > 0) )
				{
					// NOTE: Lit buildings can't share vertices beyond quads (all quads have their own face normals), so this uses a lot more geometry!
					if( bWantLitBuildings )
					{
						// Create edges for the walls of the 3D buildings -- each polygon edge subdivided via
						// SampleEdgeProfile (instead of one quad spanning the whole edge at a single flat
						// BuildingBaseZ/BuildingFillZ) so long terrace walls follow real ground contour.
						// Wall height stays constant (BuildingHeightAboveBase); only the base/top each
						// sub-quad extrudes from varies along the edge.
						for( int32 LeftPointIndex = 0; LeftPointIndex < Building.BuildingPoints.Num(); ++LeftPointIndex )
						{
							const int32 RightPointIndex = ( LeftPointIndex + 1 ) % Building.BuildingPoints.Num();
							const TArray<TPair<FVector2D,float>> EdgeProfile = SampleEdgeProfile( LeftPointIndex, RightPointIndex );

							for( int32 SubIndex = 0; SubIndex < EdgeProfile.Num() - 1; ++SubIndex )
							{
								const FVector2D& SubLeft2D = EdgeProfile[ SubIndex ].Key;
								const float SubLeftBaseZ = EdgeProfile[ SubIndex ].Value;
								const FVector2D& SubRight2D = EdgeProfile[ SubIndex + 1 ].Key;
								const float SubRightBaseZ = EdgeProfile[ SubIndex + 1 ].Value;

								TempPoints.SetNum( 4, EAllowShrinking::No );

								const int32 TopLeftVertexIndex = 0;
								TempPoints[ TopLeftVertexIndex ] = FVector3f( FVector2f(WindsClockwise ? SubRight2D : SubLeft2D), ( WindsClockwise ? SubRightBaseZ : SubLeftBaseZ ) + BuildingHeightAboveBase );

								const int32 TopRightVertexIndex = 1;
								TempPoints[ TopRightVertexIndex ] = FVector3f( FVector2f(WindsClockwise ? SubLeft2D : SubRight2D), ( WindsClockwise ? SubLeftBaseZ : SubRightBaseZ ) + BuildingHeightAboveBase );

								const int32 BottomRightVertexIndex = 2;
								TempPoints[ BottomRightVertexIndex ] = FVector3f( FVector2f(WindsClockwise ? SubLeft2D : SubRight2D), WindsClockwise ? SubLeftBaseZ : SubRightBaseZ );

								const int32 BottomLeftVertexIndex = 3;
								TempPoints[ BottomLeftVertexIndex ] = FVector3f( FVector2f(WindsClockwise ? SubRight2D : SubLeft2D), WindsClockwise ? SubRightBaseZ : SubLeftBaseZ );

								// Simple per-edge 0-1 UV (same scheme as AddThick2DLine for roads): U runs across
								// the edge, V runs up the wall height. Not seamless with neighboring edges, but
								// gives every wall quad real texture variation instead of one flat sampled texel.
								TempUVs.SetNum( 4, EAllowShrinking::No );
								TempUVs[ TopLeftVertexIndex ] = FVector2f( 0.0f, 1.0f );
								TempUVs[ TopRightVertexIndex ] = FVector2f( 1.0f, 1.0f );
								TempUVs[ BottomRightVertexIndex ] = FVector2f( 1.0f, 0.0f );
								TempUVs[ BottomLeftVertexIndex ] = FVector2f( 0.0f, 0.0f );

								TempIndices.SetNum( 6, EAllowShrinking::No );

								TempIndices[ 0 ] = BottomLeftVertexIndex;
								TempIndices[ 1 ] = TopLeftVertexIndex;
								TempIndices[ 2 ] = BottomRightVertexIndex;

								TempIndices[ 3 ] = BottomRightVertexIndex;
								TempIndices[ 4 ] = TopLeftVertexIndex;
								TempIndices[ 5 ] = TopRightVertexIndex;

								const FVector3f FaceNormal = FVector3f::CrossProduct( ( TempPoints[ 0 ] - TempPoints[ 2 ] ).GetSafeNormal(), ( TempPoints[ 0 ] - TempPoints[ 1 ] ).GetSafeNormal() );
								const FVector3f ForwardVector = FVector3f::UpVector;
								const FVector3f UpVector = FaceNormal;
								AddTriangles( TempPoints, TempUVs, TempIndices, ForwardVector, UpVector, BuildingFillColor, MeshBoundingBox );
							}
						}
					}
					else
					{
						// Create vertices for the bottom
						const int32 FirstBottomVertexIndex = this->Vertices.Num();
						for( int32 PointIndex = 0; PointIndex < Building.BuildingPoints.Num(); ++PointIndex )
						{
							const FVector2D Point = Building.BuildingPoints[ PointIndex ];

							FStreetMapVertex& NewVertex = *new( this->Vertices )FStreetMapVertex();
							NewVertex.Position = FVector3f( FVector2f(Point), BuildingBaseZ );
							NewVertex.TextureCoordinate = FVector2f( 0.0f, 0.0f );	// NOTE: We're not using texture coordinates for anything yet
							NewVertex.TangentX = FVector3f::ForwardVector;	 // NOTE: Tangents aren't important for these unlit buildings
							NewVertex.TangentZ = FVector3f::UpVector;
							NewVertex.Color = BuildingFillColor;

							MeshBoundingBox += NewVertex.Position;
						}

						// Create edges for the walls of the 3D buildings
						for( int32 LeftPointIndex = 0; LeftPointIndex < Building.BuildingPoints.Num(); ++LeftPointIndex )
						{
							const int32 RightPointIndex = ( LeftPointIndex + 1 ) % Building.BuildingPoints.Num();

							const int32 BottomLeftVertexIndex = FirstBottomVertexIndex + LeftPointIndex;
							const int32 BottomRightVertexIndex = FirstBottomVertexIndex + RightPointIndex;
							const int32 TopRightVertexIndex = FirstTopVertexIndex + RightPointIndex;
							const int32 TopLeftVertexIndex = FirstTopVertexIndex + LeftPointIndex;

							this->Indices.Add( BottomLeftVertexIndex );
							this->Indices.Add( TopLeftVertexIndex );
							this->Indices.Add( BottomRightVertexIndex );

							this->Indices.Add( BottomRightVertexIndex );
							this->Indices.Add( TopLeftVertexIndex );
							this->Indices.Add( TopRightVertexIndex );
						}
					}
				}
			}
			else
			{
				// @todo: Triangulation failed for some reason, possibly due to degenerate polygons.  We can
				//        probably improve the algorithm to avoid this happening.
			}

			// Building border
			if( bWantBuildingBorderOnGround )
			{
				for( int32 PointIndex = 0; PointIndex < Building.BuildingPoints.Num(); ++PointIndex )
				{
					AddThick2DLine(
						FVector2f(Building.BuildingPoints[ PointIndex ]),
						FVector2f(Building.BuildingPoints[ ( PointIndex + 1 ) % Building.BuildingPoints.Num() ]),
						BuildingBaseZ + BuildingBorderZ,
						BuildingBaseZ + BuildingBorderZ,
						BuildingBorderThickness,		// Thickness
						BuildingBorderColor,
						BuildingBorderColor,
						MeshBoundingBox );
				}
			}
		}

		// Water areas (ponds, lakes, reservoirs) -- flat filled polygons, same triangulation
		// approach as the building flat cap, just at ground/road level with no walls.
		const bool bWantWater = ( MeshLayer == EStreetMapMeshLayer::All || MeshLayer == EStreetMapMeshLayer::Water );
		for( int32 WaterAreaIndex = 0; bWantWater && WaterAreaIndex < WaterAreas.Num(); ++WaterAreaIndex )
		{
			const auto& WaterArea = WaterAreas[ WaterAreaIndex ];

			bool bWaterAreaWindsClockwise;
			if( FPolygonTools::TriangulatePolygon( WaterArea.WaterAreaPoints, TempIndices, /* Out */ TriangulatedVertexIndices, /* Out */ bWaterAreaWindsClockwise ) )
			{
				// A water surface is inherently flat -- one sample per pond/lake centroid, like buildings.
				FVector2D WaterAreaCentroid( 0.0, 0.0 );
				for( const FVector2D& WaterAreaPoint : WaterArea.WaterAreaPoints )
				{
					WaterAreaCentroid += WaterAreaPoint;
				}
				WaterAreaCentroid /= (double)WaterArea.WaterAreaPoints.Num();
				const float WaterAreaZ = SampleLocalZ( WaterAreaCentroid, RoadZ );

				TempPoints.SetNum( WaterArea.WaterAreaPoints.Num(), EAllowShrinking::No );
				for( int32 PointIndex = 0; PointIndex < WaterArea.WaterAreaPoints.Num(); ++PointIndex )
				{
					TempPoints[ PointIndex ] = FVector3f( FVector2f(WaterArea.WaterAreaPoints[ ( WaterArea.WaterAreaPoints.Num() - PointIndex ) - 1 ]), WaterAreaZ );
				}
				AddTriangles( TempPoints, ComputeXYBoundingBoxUVs( TempPoints ), TriangulatedVertexIndices, FVector3f::ForwardVector, FVector3f::UpVector, WaterAreaColor, MeshBoundingBox );
			}
		}

		CachedLocalBounds = FBox(MeshBoundingBox);
	}
}


#if WITH_EDITOR
void UStreetMapComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	bool bNeedRefreshCustomizationModule = false;

	// Check to see if the "StreetMap" property changed.
	if (PropertyChangedEvent.Property != nullptr)
	{
		const FName PropertyName(PropertyChangedEvent.Property->GetFName());
		if (PropertyName == GET_MEMBER_NAME_CHECKED(UStreetMapComponent, StreetMap))
		{
			bNeedRefreshCustomizationModule = true;
		}
		else if (IsCollisionProperty(PropertyName)) // For some unknown reason , GET_MEMBER_NAME_CHECKED(UStreetMapComponent, CollisionSettings) is not working ??? "TO CHECK LATER"
		{
			if (CollisionSettings.bGenerateCollision == true)
			{
				GenerateCollision();
			}
			else
			{
				ClearCollision();
			}
			bNeedRefreshCustomizationModule = true;
		}
	}

	if (bNeedRefreshCustomizationModule)
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.NotifyCustomizationModuleChanged();
	}

	// Call the parent implementation of this function
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif	// WITH_EDITOR


void UStreetMapComponent::BuildMesh()
{
	// Wipes out our cached mesh data. Maybe unnecessary in case GenerateMesh is clearing cached mesh data and creating a new SceneProxy  !
	InvalidateMesh();

	GenerateMesh();

	if (HasValidMesh())
	{
		// We have a new bounding box
		UpdateBounds();
	}
	else
	{
		// No mesh was generated
	}

	GenerateCollision();

	// Mark our render state dirty so that CreateSceneProxy can refresh it on demand
	MarkRenderStateDirty();

	AssignDefaultMaterialIfNeeded();

	Modify();
}


void UStreetMapComponent::AssignDefaultMaterialIfNeeded()
{
	if (this->GetNumMaterials() == 0 || this->GetMaterial(0) == nullptr)
	{
		if (!HasValidMesh() || GetDefaultMaterial() == nullptr)
			return;

		this->SetMaterial(0, GetDefaultMaterial());
	}
}


void UStreetMapComponent::UpdateNavigationIfNeeded()
{
	if (bCanEverAffectNavigation || bNavigationRelevant)
	{
		FNavigationSystem::UpdateComponentData(*this);
	}
}

void UStreetMapComponent::InvalidateMesh()
{
	Vertices.Reset();
	Indices.Reset();
	CachedLocalBounds = FBoxSphereBounds(FBox(ForceInit));
	ClearCollision();
	// Mark our render state dirty so that CreateSceneProxy can refresh it on demand
	MarkRenderStateDirty();
	Modify();
}

FBoxSphereBounds UStreetMapComponent::CalcBounds( const FTransform& LocalToWorld ) const
{
	if( HasValidMesh() )
	{
		FBoxSphereBounds WorldSpaceBounds = CachedLocalBounds.TransformBy( LocalToWorld );
		WorldSpaceBounds.BoxExtent *= BoundsScale;
		WorldSpaceBounds.SphereRadius *= BoundsScale;
		return WorldSpaceBounds;
	}
	else
	{
		return FBoxSphereBounds( LocalToWorld.GetLocation(), FVector::ZeroVector, 0.0f );
	}
}


void UStreetMapComponent::AddThick2DLine( const FVector2f Start, const FVector2f End, const float StartZ, const float EndZ, const float Thickness, const FColor& StartColor, const FColor& EndColor, FBox3f& MeshBoundingBox )
{
	const float HalfThickness = Thickness * 0.5f;

	const FVector2f LineDirection = ( End - Start ).GetSafeNormal();
	const FVector2f RightVector( -LineDirection.Y, LineDirection.X );

	const int32 BottomLeftVertexIndex = Vertices.Num();
	FStreetMapVertex& BottomLeftVertex = *new( Vertices )FStreetMapVertex();
	BottomLeftVertex.Position = FVector3f( Start - RightVector * HalfThickness, StartZ );
	BottomLeftVertex.TextureCoordinate = FVector2f( 0.0f, 0.0f );
	BottomLeftVertex.TangentX = FVector3f( LineDirection, 0.0f );
	BottomLeftVertex.TangentZ = FVector3f::UpVector;
	BottomLeftVertex.Color = StartColor;
	MeshBoundingBox += BottomLeftVertex.Position;

	const int32 BottomRightVertexIndex = Vertices.Num();
	FStreetMapVertex& BottomRightVertex = *new( Vertices )FStreetMapVertex();
	BottomRightVertex.Position = FVector3f( Start + RightVector * HalfThickness, StartZ );
	BottomRightVertex.TextureCoordinate = FVector2f( 1.0f, 0.0f );
	BottomRightVertex.TangentX = FVector3f( LineDirection, 0.0f );
	BottomRightVertex.TangentZ = FVector3f::UpVector;
	BottomRightVertex.Color = StartColor;
	MeshBoundingBox += BottomRightVertex.Position;

	const int32 TopRightVertexIndex = Vertices.Num();
	FStreetMapVertex& TopRightVertex = *new( Vertices )FStreetMapVertex();
	TopRightVertex.Position = FVector3f( End + RightVector * HalfThickness, EndZ );
	TopRightVertex.TextureCoordinate = FVector2f( 1.0f, 1.0f );
	TopRightVertex.TangentX = FVector3f( LineDirection, 0.0f );
	TopRightVertex.TangentZ = FVector3f::UpVector;
	TopRightVertex.Color = EndColor;
	MeshBoundingBox += TopRightVertex.Position;

	const int32 TopLeftVertexIndex = Vertices.Num();
	FStreetMapVertex& TopLeftVertex = *new( Vertices )FStreetMapVertex();
	TopLeftVertex.Position = FVector3f( End - RightVector * HalfThickness, EndZ );
	TopLeftVertex.TextureCoordinate = FVector2f( 0.0f, 1.0f );
	TopLeftVertex.TangentX = FVector3f( LineDirection, 0.0f );
	TopLeftVertex.TangentZ = FVector3f::UpVector;
	TopLeftVertex.Color = EndColor;
	MeshBoundingBox += TopLeftVertex.Position;

	Indices.Add( BottomLeftVertexIndex );
	Indices.Add( BottomRightVertexIndex );
	Indices.Add( TopRightVertexIndex );

	Indices.Add( BottomLeftVertexIndex );
	Indices.Add( TopRightVertexIndex );
	Indices.Add( TopLeftVertexIndex );
};


void UStreetMapComponent::AddRoadJoin( const FVector2f JointPoint, const FVector2f PrevDirection, const FVector2f NextDirection, const float Z, const float Thickness, const FColor& Color, FBox3f& MeshBoundingBox )
{
	// Each road segment is an independent butt-capped quad (see AddThick2DLine), so at a bend the
	// trailing edge of one segment and the leading edge of the next don't line up: the outer side of
	// the turn is left with a triangular gap (reads as two angled rectangles with no fluent connection),
	// and the inner side overlaps. This fills the gap with a small fan centered on the shared point,
	// swept from the previous segment's edge to the next segment's edge, on both sides of the centerline
	// (the "inner" fan just harmlessly overlaps existing geometry). The wider the turn angle, the bigger
	// the gap this closes -- exactly the case that looked worst.
	const float HalfThickness = Thickness * 0.5f;
	const FVector2f PrevRight( -PrevDirection.Y, PrevDirection.X );
	const FVector2f NextRight( -NextDirection.Y, NextDirection.X );

	// Nearly-straight joints leave no visible gap; skip them.
	if( FVector2f::DotProduct( PrevRight, NextRight ) > 0.9999f )
	{
		return;
	}

	const int32 CenterVertexIndex = Vertices.Num();
	FStreetMapVertex& CenterVertex = *new( Vertices )FStreetMapVertex();
	CenterVertex.Position = FVector3f( JointPoint, Z );
	CenterVertex.TextureCoordinate = FVector2f( 0.5f, 0.5f );
	CenterVertex.TangentX = FVector3f( PrevDirection, 0.0f );
	CenterVertex.TangentZ = FVector3f::UpVector;
	CenterVertex.Color = Color;
	MeshBoundingBox += CenterVertex.Position;

	const int32 NumFanSegments = 6;

	// Builds a fan of triangles sweeping from StartRight to EndRight (shortest arc), at HalfThickness
	// from JointPoint. Emits every triangle with both winding orders since the correct one depends on
	// turn direction and isn't worth hand-deriving per case -- same double-sided approach used for the
	// pitched-roof faces, see the roof-feature memory.
	auto BuildFan = [&]( const FVector2f& StartRight, const FVector2f& EndRight )
	{
		const float StartAngle = FMath::Atan2( StartRight.Y, StartRight.X );
		const float EndAngle = FMath::Atan2( EndRight.Y, EndRight.X );
		const float DeltaAngle = FMath::FindDeltaAngleRadians( StartAngle, EndAngle );

		int32 PrevVertexIndex = INDEX_NONE;
		for( int32 Step = 0; Step <= NumFanSegments; ++Step )
		{
			const float Alpha = (float)Step / (float)NumFanSegments;
			const float Angle = StartAngle + DeltaAngle * Alpha;
			const FVector2f Offset( FMath::Cos( Angle ), FMath::Sin( Angle ) );

			const int32 VertexIndex = Vertices.Num();
			FStreetMapVertex& Vertex = *new( Vertices )FStreetMapVertex();
			Vertex.Position = FVector3f( JointPoint + Offset * HalfThickness, Z );
			Vertex.TextureCoordinate = FVector2f( 0.0f, 0.0f );
			Vertex.TangentX = FVector3f( PrevDirection, 0.0f );
			Vertex.TangentZ = FVector3f::UpVector;
			Vertex.Color = Color;
			MeshBoundingBox += Vertex.Position;

			if( PrevVertexIndex != INDEX_NONE )
			{
				Indices.Add( CenterVertexIndex );
				Indices.Add( PrevVertexIndex );
				Indices.Add( VertexIndex );

				Indices.Add( CenterVertexIndex );
				Indices.Add( VertexIndex );
				Indices.Add( PrevVertexIndex );
			}
			PrevVertexIndex = VertexIndex;
		}
	};

	BuildFan( PrevRight, NextRight );
	BuildFan( -PrevRight, -NextRight );
}


void UStreetMapComponent::AddTriangles( const TArray<FVector3f>& Points, const TArray<FVector2f>& UVs, const TArray<int32>& PointIndices, const FVector3f& ForwardVector, const FVector3f& UpVector, const FColor& Color, FBox3f& MeshBoundingBox )
{
	const int32 FirstVertexIndex = Vertices.Num();

	for( int32 PointNum = 0; PointNum < Points.Num(); ++PointNum )
	{
		FStreetMapVertex& NewVertex = *new( Vertices )FStreetMapVertex();
		NewVertex.Position = Points[ PointNum ];
		NewVertex.TextureCoordinate = UVs.IsValidIndex( PointNum ) ? UVs[ PointNum ] : FVector2f( 0.0f, 0.0f );
		NewVertex.TangentX = ForwardVector;
		NewVertex.TangentZ = UpVector;
		NewVertex.Color = Color;

		MeshBoundingBox += NewVertex.Position;
	}

	for( int32 PointIndex : PointIndices )
	{
		Indices.Add( FirstVertexIndex + PointIndex );
	}
};


FString UStreetMapComponent::GetStreetMapAssetName() const
{
	return StreetMap != nullptr ? StreetMap->GetName() : FString(TEXT("NONE"));
}

