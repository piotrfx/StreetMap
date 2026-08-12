#include "StreetMapBuildingDetailsActor.h"
#include "StreetMapBuildingDetailsComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"

AStreetMapBuildingDetailsActor::AStreetMapBuildingDetailsActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	BuildingDetailsComponent = CreateDefaultSubobject<UStreetMapBuildingDetailsComponent>(TEXT("BuildingDetailsComp"));
	RootComponent = BuildingDetailsComponent;

	// Created and attached here (siblings under the actor's own root, both set up in the same
	// constructor) rather than inside UStreetMapBuildingDetailsComponent's own constructor -- see
	// InitializeInstanceComponents for why that nested-attachment pattern doesn't work reliably.
	// Movable, matching the root component's default mobility -- a Static child cannot be attached
	// under a Movable parent (UE aborts the attach silently, logging only a Warning), which is why
	// these were previously never actually attached despite SetupAttachment() being called.
	UHierarchicalInstancedStaticMeshComponent* WindowInstances = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("WindowInstances"));
	WindowInstances->SetupAttachment(BuildingDetailsComponent);
	WindowInstances->SetCollisionProfileName(TEXT("BlockAll"));

	UHierarchicalInstancedStaticMeshComponent* DoorInstances = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("DoorInstances"));
	DoorInstances->SetupAttachment(BuildingDetailsComponent);
	DoorInstances->SetCollisionProfileName(TEXT("BlockAll"));

	BuildingDetailsComponent->InitializeInstanceComponents(WindowInstances, DoorInstances);
}
