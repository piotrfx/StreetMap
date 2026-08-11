#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "StreetMapBuildingDetailsComponent.generated.h"

class UStreetMap;
class UStaticMesh;
class UHierarchicalInstancedStaticMeshComponent;

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

	/** Mesh instanced for windows. Assumed to face +X with its pivot roughly centered on the pane, sitting flush against a wall when placed at the wall surface. */
	UPROPERTY(EditAnywhere, Category = "StreetMap")
	UStaticMesh* WindowMesh = nullptr;

	/** Mesh instanced for doors. Assumed to face +X with its pivot at the base (ground level). */
	UPROPERTY(EditAnywhere, Category = "StreetMap")
	UStaticMesh* DoorMesh = nullptr;

	/** Only place details on residential buildings (see FStreetMapBuilding::bIsResidential). Commercial buildings can get their own pass later. */
	UPROPERTY(EditAnywhere, Category = "StreetMap")
	bool bResidentialOnly = true;

	/** Horizontal distance between window instances along a wall edge. */
	UPROPERTY(EditAnywhere, Category = "StreetMap|Tuning")
	float WindowSpacing = 300.0f;

	/** Minimum gap kept between a window instance and either end (corner) of its wall edge. */
	UPROPERTY(EditAnywhere, Category = "StreetMap|Tuning")
	float CornerMargin = 100.0f;

	/** Wall edges shorter than this get no windows at all -- avoids cramming instances onto tiny segments. */
	UPROPERTY(EditAnywhere, Category = "StreetMap|Tuning")
	float MinEdgeLengthForWindows = 250.0f;

	/** Height of a window instance above its floor's base Z. */
	UPROPERTY(EditAnywhere, Category = "StreetMap|Tuning")
	float WindowSillHeightOffset = 100.0f;

	/** Vertical height of one building floor/level. Should match the walls/roof component's BuildingLevelFloorFactor to keep windows lined up with the actual floors. */
	UPROPERTY(EditAnywhere, Category = "StreetMap|Tuning")
	float FloorHeight = 300.0f;

	/** Fraction (0-1) of window slots that get randomly skipped, for a less uniform look. Deterministic per slot, not re-rolled between generations. */
	UPROPERTY(EditAnywhere, Category = "StreetMap|Tuning", meta = (ClampMin = "0", ClampMax = "1"))
	float WindowSkipProbability = 0.15f;

	/** Uniform scale applied to each window instance, on top of WindowMesh's own size. */
	UPROPERTY(EditAnywhere, Category = "StreetMap|Tuning")
	float WindowMeshScale = 1.0f;

	/** Uniform scale applied to each door instance, on top of DoorMesh's own size. */
	UPROPERTY(EditAnywhere, Category = "StreetMap|Tuning")
	float DoorMeshScale = 1.0f;

	/** Half-width of the span kept clear of window instances around a placed door, since the door's
	 *  real mesh width/orientation isn't known until an asset is assigned -- simpler and safer than
	 *  guessing which axis of DoorMesh's bounds corresponds to width after rotation. */
	UPROPERTY(EditAnywhere, Category = "StreetMap|Tuning")
	float DoorClearance = 120.0f;

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
