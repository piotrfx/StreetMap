#pragma once

#include "CoreMinimal.h"
#include "ActorFactories/ActorFactory.h"
#include "StreetMapActorFactory.generated.h"

UCLASS()
class UStreetMapActorFactory : public UActorFactory
{
        GENERATED_BODY()

public:
        UStreetMapActorFactory(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

        //~ Begin UActorFactory Interface
        virtual void PostSpawnActor(UObject* Asset, AActor* NewActor) override;
        // UActorFactory::PostCreateBlueprint was removed in UE 5.8 - no longer overrides anything,
        // so "Create Blueprint from this actor" on a StreetMap actor won't auto-populate the CDO's map data.
        virtual void PostCreateBlueprint(UObject* Asset, AActor* CDO);
        virtual bool CanCreateActorFrom(const FAssetData& AssetData, FText& OutErrorMsg) override;
	//~ End UActorFactory Interface
};
