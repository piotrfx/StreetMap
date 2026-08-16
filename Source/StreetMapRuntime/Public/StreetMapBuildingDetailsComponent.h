#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "StreetMapBuildingDetailsComponent.generated.h"

class UStreetMap;
class UStaticMesh;
class UHierarchicalInstancedStaticMeshComponent;
class ALandscapeProxy;

/** Scatters window and door static mesh instances across a UStreetMap's building walls, using
 *  HierarchicalInstancedStaticMeshComponents (not individual actors) since a real map can have
 *  thousands of residential buildings and tens of thousands of window instances. Positions are
 *  glued onto the flat wall face -- not boolean-cut openings, which would need a per-building
 *  geometry-script pass far beyond what this needs at city scale. */
UCLASS(ClassGroup = (StreetMap), meta = (BlueprintSpawnableComponent))
class STREETMAPRUNTIME_API UStreetMapBuildingDetailsComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UStreetMapBuildingDetailsComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Called once by AStreetMapBuildingDetailsActor's constructor, which owns/attaches the actual HISM
	 *  subobjects. They're created and attached there (sibling components under the same actor's root)
	 *  rather than inside this component's own constructor -- attaching a child component to "this" from
	 *  within a non-Actor component's constructor does not reliably propagate the parent's world transform
	 *  (confirmed: instances rendered at an unmodified local-space Z instead of the actor's placed Z). */
	void InitializeInstanceComponents(UHierarchicalInstancedStaticMeshComponent* InWindowInstances, UHierarchicalInstancedStaticMeshComponent* InDoorInstances)
	{
		WindowInstances = InWindowInstances;
		DoorInstances = InDoorInstances;
	}

	/** The street map to read building footprints/heights from. */
	UPROPERTY(EditAnywhere, Category = "StreetMap")
	UStreetMap* StreetMap = nullptr;

	/** Optional Landscape to conform window/door placement to. Sampled once per building at the
	 *  footprint centroid -- independently of any StreetMapComponent's own sample, since this
	 *  component lives on its own actor with its own transform. Left unset, falls back to the
	 *  original flat local Z=0 base. */
	UPROPERTY(EditAnywhere, Category = "StreetMap|Terrain")
	TSoftObjectPtr<ALandscapeProxy> TerrainLandscape;

	/** Mesh instanced for windows. Assumed to face +X with its pivot roughly centered on the pane, sitting flush against a wall when placed at the wall surface. */
	UPROPERTY(EditAnywhere, Category = "StreetMap")
	UStaticMesh* WindowMesh = nullptr;

	/** Mesh instanced for doors. Assumed to face +X with its pivot at the base (ground level). */
	UPROPERTY(EditAnywhere, Category = "StreetMap")
	UStaticMesh* DoorMesh = nullptr;

	/** Only place details on residential buildings (see FStreetMapBuilding::bIsResidential). Commercial buildings can get their own pass later. */
	UPROPERTY(EditAnywhere, Category = "StreetMap")
	bool bResidentialOnly = true;

	/** Width of one house's frontage, used to subdivide the building's front wall into repeating
	 *  house-sized segments -- a single building polygon often represents a whole terrace row sharing
	 *  one wall (confirmed by the roof generator: it draws exactly one gable per building record, so
	 *  multiple visible roof peaks along a wall mean multiple actual houses), not a single house. */
	UPROPERTY(EditAnywhere, Category = "StreetMap|Tuning")
	float HouseWidth = 600.0f;

	/** Distance from a house segment's center to each flanking window. Each side is only used if it
	 *  clears CornerMargin from that house segment's own boundaries (not the whole wall's). */
	UPROPERTY(EditAnywhere, Category = "StreetMap|Tuning")
	float WindowSpacing = 150.0f;

	/** Minimum gap kept between a window instance and either end (corner) of its own house segment. */
	UPROPERTY(EditAnywhere, Category = "StreetMap|Tuning")
	float CornerMargin = 100.0f;

	/** A house segment must be at least this wide to get any windows at all -- avoids cramming instances onto a tiny frontage. */
	UPROPERTY(EditAnywhere, Category = "StreetMap|Tuning")
	float MinEdgeLengthForWindows = 250.0f;

	/** Height of a window instance above the building's base Z. */
	UPROPERTY(EditAnywhere, Category = "StreetMap|Tuning")
	float WindowSillHeightOffset = 100.0f;

	/** Height of a door instance above the building's base Z. DoorMesh is documented as pivoting at
	 *  its own base (ground level), so this is normally 0 -- only needed as a correction if a stand-in
	 *  mesh with a different (e.g. centered) pivot is assigned instead of a real door asset. */
	UPROPERTY(EditAnywhere, Category = "StreetMap|Tuning")
	float DoorSillHeightOffset = 0.0f;

	/** Used only as a per-level height when deriving a building's total height from BuildingLevels (see FStreetMapBuilding::Height/BuildingLevels). Should match the walls/roof component's BuildingLevelFloorFactor. */
	UPROPERTY(EditAnywhere, Category = "StreetMap|Tuning")
	float FloorHeight = 300.0f;

	/** Uniform scale applied to each window instance, on top of WindowMesh's own size. */
	UPROPERTY(EditAnywhere, Category = "StreetMap|Tuning")
	float WindowMeshScale = 1.0f;

	/** Uniform scale applied to each door instance, on top of DoorMesh's own size. */
	UPROPERTY(EditAnywhere, Category = "StreetMap|Tuning")
	float DoorMeshScale = 1.0f;

	/** Extra yaw applied on top of the computed outward-facing rotation for windows, to correct for
	 *  WindowMesh's actual front axis not matching the assumed local +X (e.g. 90 if the mesh really
	 *  faces +Y). Tune this directly and regenerate -- no recompile needed. */
	UPROPERTY(EditAnywhere, Category = "StreetMap|Tuning")
	float WindowRotationOffsetYaw = 0.0f;

	/** Extra roll applied on top of the computed outward-facing rotation for windows -- corrects for
	 *  WindowMesh's authored "up" axis not actually being local +Z (yaw alone can only change which
	 *  compass direction the mesh faces, it can't fix a mesh that's twisted around its own facing axis;
	 *  that symptom looks like one edge of the window sitting proud of the wall and the opposite edge
	 *  sunk into it, identically on every instance regardless of which wall it's on). */
	UPROPERTY(EditAnywhere, Category = "StreetMap|Tuning")
	float WindowRotationOffsetRoll = 0.0f;

	/** Extra pitch applied on top of the computed outward-facing rotation for windows. See WindowRotationOffsetRoll. */
	UPROPERTY(EditAnywhere, Category = "StreetMap|Tuning")
	float WindowRotationOffsetPitch = 0.0f;

	/** Same as WindowRotationOffsetYaw, but for DoorMesh. */
	UPROPERTY(EditAnywhere, Category = "StreetMap|Tuning")
	float DoorRotationOffsetYaw = 0.0f;

	/** Same as WindowRotationOffsetRoll, but for DoorMesh. */
	UPROPERTY(EditAnywhere, Category = "StreetMap|Tuning")
	float DoorRotationOffsetRoll = 0.0f;

	/** Same as WindowRotationOffsetPitch, but for DoorMesh. */
	UPROPERTY(EditAnywhere, Category = "StreetMap|Tuning")
	float DoorRotationOffsetPitch = 0.0f;

	/** Clears and repopulates the window/door instance components from the current StreetMap data and tunables. */
	UFUNCTION(CallInEditor, Category = "StreetMap")
	void GenerateBuildingDetails();

	/** Removes all placed instances without regenerating. */
	UFUNCTION(CallInEditor, Category = "StreetMap")
	void ClearBuildingDetails();

protected:
	UPROPERTY(VisibleAnywhere, Category = "StreetMap")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> WindowInstances;

	UPROPERTY(VisibleAnywhere, Category = "StreetMap")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> DoorInstances;
};
