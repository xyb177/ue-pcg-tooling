#pragma once

#include "CoreMinimal.h"
#include "RSBTypes.h"
#include "RSBPoolManager.generated.h"

class URSBConfig;

UCLASS()
class RUNTIMESPAWNBUDGET_API URSBPoolManager : public UObject
{
    GENERATED_BODY()

public:
    void Initialize(UWorld* InWorld, const URSBConfig* InConfig);

    AActor* Acquire(const FRSBSpawnRequest& Request, bool& bOutPoolHit);
    void Release(AActor* Actor, FName PoolKey, bool bForceDestroy);
    void Prewarm(TSubclassOf<AActor> ActorClass, FName PoolKey, int32 Count);
    void CullIdlePools(double NowSeconds);
    int32 GetEffectiveCapacityForClass(TSubclassOf<AActor> ActorClass, FName PoolKey = NAME_None) const;

private:
    FName ResolvePoolKey(const FRSBSpawnRequest& Request) const;
    FRSBPoolBucket& GetOrCreateBucket(FName PoolKey, TSubclassOf<AActor> ActorClass);

private:
    TWeakObjectPtr<UWorld> World;
    int32 DefaultCapacity = 128;
    float IdleCullSeconds = 60.0f;

    UPROPERTY()
    TMap<FName, FRSBPoolBucket> Buckets;
};
