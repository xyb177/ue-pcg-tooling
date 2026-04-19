#pragma once

#include "CoreMinimal.h"
#include "RSBTypes.h"
#include "RSBSpawnExecutor.generated.h"

UCLASS()
class RUNTIMESPAWNBUDGET_API URSBSpawnExecutor : public UObject
{
    GENERATED_BODY()

public:
    AActor* ExecuteSpawn(UWorld* World, const FRSBSpawnRequest& Request);
    bool ExecuteDestroy(AActor* Actor, bool bForceDestroy);
};
