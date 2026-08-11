#include "StreetMapSceneProxy.h"

#include "StreetMapComponent.h"
#include "Engine/Engine.h"
#include "Materials/Material.h"
#include "Materials/MaterialRenderProxy.h"
#include "SceneManagement.h"

FStreetMapSceneProxy::FStreetMapSceneProxy(const UStreetMapComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent),
	  VertexFactory(GetScene().GetFeatureLevel(), "FStreetMapSceneProxy"),
	  MaterialInterface(nullptr),
	  StreetMapComp(InComponent),
	  CollisionResponse(InComponent->GetCollisionResponseToChannels())
{
}

void FStreetMapSceneProxy::Init(const UStreetMapComponent* InComponent, const TArray<FStreetMapVertex>& Vertices, const TArray<uint32>& Indices)
{
	// Copy index buffer
	IndexBuffer32.Indices = Indices;

	MaterialInterface = nullptr;
	this->MaterialRelevance = InComponent->GetMaterialRelevance(GetScene().GetFeatureLevel());


	// Copy vertex data
	const int32 NumVerts = Vertices.Num();
	TArray<FDynamicMeshVertex> DynamicVertices;
	DynamicVertices.SetNumUninitialized(NumVerts);

	for (int VertIdx = 0; VertIdx < NumVerts; VertIdx++)
	{
		const FStreetMapVertex& StreetMapVert = Vertices[VertIdx];
		FDynamicMeshVertex& Vert = DynamicVertices[VertIdx];
		Vert.Position = StreetMapVert.Position;
		Vert.Color = StreetMapVert.Color;
		Vert.TextureCoordinate[0] = StreetMapVert.TextureCoordinate;
		Vert.TangentX = StreetMapVert.TangentX;
		Vert.TangentZ = StreetMapVert.TangentZ;
	}

	VertexBuffer.InitFromDynamicVertex(&VertexFactory, DynamicVertices);

	// Enqueue initialization of render resource
	InitResources();

	// Set a material
	{
		if (InComponent->GetNumMaterials() > 0)
		{
			MaterialInterface = InComponent->GetMaterial(0);
		}

		// Use the default material if we don't have one set
		if (MaterialInterface == nullptr)
		{
			MaterialInterface = UMaterial::GetDefaultMaterial(MD_Surface);
		}
	}
}

FStreetMapSceneProxy::~FStreetMapSceneProxy()
{
	VertexBuffer.PositionVertexBuffer.ReleaseResource();
	VertexBuffer.StaticMeshVertexBuffer.ReleaseResource();
	VertexBuffer.ColorVertexBuffer.ReleaseResource();
	IndexBuffer32.ReleaseResource();
	VertexFactory.ReleaseResource();
}


SIZE_T FStreetMapSceneProxy::GetTypeHash() const
{
	static size_t UniquePointer;
	return reinterpret_cast<size_t>(&UniquePointer);
}

void FStreetMapSceneProxy::InitResources()
{
	// Start initializing our vertex buffer, index buffer, and vertex factory.  This will be kicked off on the render thread.
	BeginInitResource(&VertexBuffer.PositionVertexBuffer);
	BeginInitResource(&VertexBuffer.StaticMeshVertexBuffer);
	BeginInitResource(&VertexBuffer.ColorVertexBuffer);
	BeginInitResource(&IndexBuffer32);
	BeginInitResource(&VertexFactory);
}


bool FStreetMapSceneProxy::MustDrawMeshDynamically( const FSceneView& View ) const
{
	return ( AllowDebugViewmodes() && View.Family->EngineShowFlags.Wireframe ) || IsSelected();
}


bool FStreetMapSceneProxy::IsInCollisionView(const FEngineShowFlags& EngineShowFlags) const
{
	return  EngineShowFlags.CollisionVisibility || EngineShowFlags.CollisionPawn;
}

FPrimitiveViewRelevance FStreetMapSceneProxy::GetViewRelevance( const FSceneView* View ) const
{
	FPrimitiveViewRelevance Result;
	Result.bDrawRelevance = IsShown(View);
	Result.bShadowRelevance = IsShadowCast(View);
	
	// Only draw dynamically if we're drawing in wireframe or we're selected in the editor
	Result.bDynamicRelevance = MustDrawMeshDynamically( *View );
	Result.bStaticRelevance = !MustDrawMeshDynamically( *View );
	
	MaterialRelevance.SetPrimitiveViewRelevance(Result);
	return Result;
}


bool FStreetMapSceneProxy::CanBeOccluded() const
{
	return !MaterialRelevance.bDisableDepthTest;
}


void FStreetMapSceneProxy::MakeMeshBatch(FMeshBatch& Mesh, FMaterialRenderProxy* MaterialRenderProxy, bool bInWireframe) const
{
	FMaterialRenderProxy* MaterialProxy = MaterialRenderProxy != nullptr ? MaterialRenderProxy : StreetMapComp->GetDefaultMaterial()->GetRenderProxy();

	FMeshBatchElement& BatchElement = Mesh.Elements[0];
	BatchElement.IndexBuffer = &IndexBuffer32;
	Mesh.bWireframe = bInWireframe;
	Mesh.VertexFactory = &VertexFactory;
	Mesh.MaterialRenderProxy = MaterialProxy;
	Mesh.CastShadow = true;
	BatchElement.FirstIndex = 0;
	const int IndexCount = IndexBuffer32.Indices.Num();
	BatchElement.NumPrimitives = IndexCount / 3;
	BatchElement.MinVertexIndex = 0;
	BatchElement.MaxVertexIndex = VertexBuffer.PositionVertexBuffer.GetNumVertices() - 1;
	Mesh.ReverseCulling = IsLocalToWorldDeterminantNegative();
	Mesh.Type = PT_TriangleList;
	Mesh.DepthPriorityGroup = SDPG_World;
	Mesh.LODIndex = 0;
}


void FStreetMapSceneProxy::DrawStaticElements( FStaticPrimitiveDrawInterface* PDI )
{
	const int IndexCount = IndexBuffer32.Indices.Num();
	if( VertexBuffer.PositionVertexBuffer.GetNumVertices() > 0 && IndexCount > 0 )
	{
		const float ScreenSize = 1.0f;

		// Use the component's actually-assigned material (falls back to the plugin default
		// inside MakeMeshBatch only if none is set) -- passing nullptr here always renders
		// with the default material regardless of what's assigned, and since that default
		// isn't in GetUsedMaterials(), the engine silently drops the whole mesh batch.
		FMaterialRenderProxy* MaterialProxy = MaterialInterface ? MaterialInterface->GetRenderProxy() : nullptr;

		FMeshBatch MeshBatch;
		MakeMeshBatch(MeshBatch, MaterialProxy, false);
		PDI->DrawMesh( MeshBatch, ScreenSize );
	}
}


void FStreetMapSceneProxy::GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, class FMeshElementCollector& Collector) const
{
	const int IndexCount = IndexBuffer32.Indices.Num();
	if (VertexBuffer.PositionVertexBuffer.GetNumVertices() > 0 && IndexCount > 0)
	{
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
		{
			const FSceneView& View = *Views[ViewIndex];

			const bool bIsWireframe = AllowDebugViewmodes() && View.Family->EngineShowFlags.Wireframe;

			FMaterialRenderProxy* WireframeMaterialRenderProxy = nullptr;
			if (GEngine->WireframeMaterial && bIsWireframe)
			{
				FColoredMaterialRenderProxy* NewWireframeMaterialRenderProxy = new FColoredMaterialRenderProxy(GEngine->WireframeMaterial->GetRenderProxy(), FLinearColor(0, 0.5f, 1.f));
				Collector.RegisterOneFrameMaterialProxy(NewWireframeMaterialRenderProxy);
				WireframeMaterialRenderProxy = NewWireframeMaterialRenderProxy;
			}

			if (MustDrawMeshDynamically(View))
			{
				const bool bInCollisionView = IsInCollisionView(ViewFamily.EngineShowFlags);
				const bool bCanDrawCollision = bInCollisionView && IsCollisionEnabled();

				if (!IsCollisionEnabled() && bInCollisionView)
				{
					continue;
				}

				FMaterialRenderProxy* MaterialProxy = WireframeMaterialRenderProxy;
				if (MaterialProxy == nullptr && bCanDrawCollision)
				{
					FColoredMaterialRenderProxy* NewCollisionMaterialProxy = new FColoredMaterialRenderProxy(GEngine->ShadedLevelColorationUnlitMaterial->GetRenderProxy(), FColor::Cyan);
					Collector.RegisterOneFrameMaterialProxy(NewCollisionMaterialProxy);
					MaterialProxy = NewCollisionMaterialProxy;
				}
				if (MaterialProxy == nullptr)
				{
					// Fall back to the component's actually-assigned material -- see comment in DrawStaticElements.
					MaterialProxy = MaterialInterface ? MaterialInterface->GetRenderProxy() : nullptr;
				}

				// Draw the mesh!
				FMeshBatch& MeshBatch = Collector.AllocateMesh();
				MakeMeshBatch(MeshBatch, MaterialProxy, bIsWireframe);
				Collector.AddMesh(ViewIndex, MeshBatch);
			}
		}
	}
}


uint32 FStreetMapSceneProxy::GetMemoryFootprint( void ) const
{ 
	return sizeof( *this ) + GetAllocatedSize();
}
