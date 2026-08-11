#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "StreetMapBuildingDetailsActor.generated.h"

class UStreetMapBuildingDetailsComponent;

/** An actor that scatters window/door instances across a street map's building walls */
UCLASS(hidecategories = (Physics))
class STREETMAPRUNTIME_API AStreetMapBuildingDetailsActor : public AActor
{
	GENERATED_BODY()

public:
	AStreetMapBuildingDetailsActor(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "StreetMap")
	UStreetMapBuildingDetailsComponent* BuildingDetailsComponent;

	FORCEINLINE UStreetMapBuildingDetailsComponent* GetBuildingDetailsComponent() const { return BuildingDetailsComponent; }
};
