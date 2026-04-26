#include "RSBPoolManager.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Interfaces/RSBPoolableInterface.h"
#include "RSBConfig.h"

void URSBPoolManager::Initialize(UWorld* InWorld, const URSBConfig* InConfig)
{
    World = InWorld;
    if (InConfig)
    {
        DefaultCapacity = FMath::Max(0, InConfig->DefaultPoolCapacity);
        IdleCullSeconds = FMath::Max(1.0f, InConfig->PoolIdleCullSeconds);
    }
}

int32 URSBPoolManager::GetEffectiveCapacityForClass(TSubclassOf<AActor> ActorClass, FName PoolKey) const
{
    const URSBConfig* Config = GetDefault<URSBConfig>();
    const FName Key = !PoolKey.IsNone() ? PoolKey : (ActorClass ? ActorClass->GetFName() : NAME_None);
    if (Config)
    {
        if (const int32* OverrideCapacity = Config->PerClassPoolCapacityOverrides.Find(Key))
        {
            return FMath::Max(0, *OverrideCapacity);
        }
    }
    return FMath::Max(0, DefaultCapacity);
}

AActor* URSBPoolManager::Acquire(const FRSBSpawnRequest& Request, bool& bOutPoolHit)
{
    bOutPoolHit = false;

    if (!Request.bAllowPooling)
    {
        return nullptr;
    }

    const FName PoolKey = ResolvePoolKey(Request);
    if (PoolKey.IsNone())
    {
        return nullptr;
    }

    FRSBPoolBucket& Bucket = GetOrCreateBucket(PoolKey, Request.ActorClass);
    Bucket.LastTouchedSeconds = FPlatformTime::Seconds();

    while (!Bucket.InactiveActors.IsEmpty())
    {
        TWeakObjectPtr<AActor> Candidate = Bucket.InactiveActors.Pop();
        AActor* Actor = Candidate.Get();
        if (!Actor)
        {
            continue;
        }

        Actor->SetActorTransform(Request.Transform, false, nullptr, ETeleportType::TeleportPhysics);
        Actor->SetActorHiddenInGame(false);
        Actor->SetActorEnableCollision(true);
        Actor->SetActorTickEnabled(true);
        TInlineComponentArray<UActorComponent*> Components;
        Actor->GetComponents(Components);
        for (UActorComponent* Component : Components)
        {
            if (!Component)
            {
                continue;
            }

            Component->SetComponentTickEnabled(true);
            if (UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(Component))
            {
                PrimitiveComponent->SetGenerateOverlapEvents(true);
                PrimitiveComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
            }
        }

        if (Actor->GetClass()->ImplementsInterface(URSBPoolableInterface::StaticClass()))
        {
            IRSBPoolableInterface::Execute_OnPooledAcquire(Actor);
        }

        ++Bucket.HitCount;
        bOutPoolHit = true;
        return Actor;
    }

    ++Bucket.MissCount;
    return nullptr;
}

void URSBPoolManager::Release(AActor* Actor, FName PoolKey, bool bForceDestroy)
{
    if (!Actor)
    {
        return;
    }

    if (bForceDestroy)
    {
        Actor->Destroy();
        return;
    }

    const FName EffectiveKey = PoolKey.IsNone() ? Actor->GetClass()->GetFName() : PoolKey;
    FRSBPoolBucket& Bucket = GetOrCreateBucket(EffectiveKey, Actor->GetClass());
    if (Bucket.InactiveActors.Num() >= Bucket.Capacity)
    {
        Actor->Destroy();
        return;
    }

    if (Actor->GetClass()->ImplementsInterface(URSBPoolableInterface::StaticClass()))
    {
        IRSBPoolableInterface::Execute_OnPooledReset(Actor);
        IRSBPoolableInterface::Execute_OnPooledRelease(Actor);
    }

    Actor->SetActorHiddenInGame(true);
    Actor->SetActorEnableCollision(false);
    Actor->SetActorTickEnabled(false);
    TInlineComponentArray<UActorComponent*> Components;
    Actor->GetComponents(Components);
    for (UActorComponent* Component : Components)
    {
        if (!Component)
        {
            continue;
        }

        Component->SetComponentTickEnabled(false);
        if (UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(Component))
        {
            PrimitiveComponent->SetGenerateOverlapEvents(false);
            PrimitiveComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        }
    }

    Bucket.InactiveActors.Add(Actor);
    Bucket.LastTouchedSeconds = FPlatformTime::Seconds();
}

void URSBPoolManager::Prewarm(TSubclassOf<AActor> ActorClass, FName PoolKey, int32 Count)
{
    UWorld* LocalWorld = World.Get();
    if (!LocalWorld || !ActorClass || Count <= 0)
    {
        return;
    }

    FRSBPoolBucket& Bucket = GetOrCreateBucket(PoolKey.IsNone() ? ActorClass->GetFName() : PoolKey, ActorClass);

    const int32 CreateCount = FMath::Min(Count, FMath::Max(0, Bucket.Capacity - Bucket.InactiveActors.Num()));
    for (int32 Index = 0; Index < CreateCount; ++Index)
    {
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        AActor* Spawned = LocalWorld->SpawnActor<AActor>(ActorClass, FTransform::Identity, Params);
        if (!Spawned)
        {
            continue;
        }

        Release(Spawned, Bucket.PoolKey, false);
    }
}

void URSBPoolManager::CullIdlePools(double NowSeconds)
{
    TArray<FName> KeysToCull;
    for (const TPair<FName, FRSBPoolBucket>& KV : Buckets)
    {
        if ((NowSeconds - KV.Value.LastTouchedSeconds) > static_cast<double>(IdleCullSeconds))
        {
            KeysToCull.Add(KV.Key);
        }
    }

    for (FName Key : KeysToCull)
    {
        if (FRSBPoolBucket* Bucket = Buckets.Find(Key))
        {
            for (TWeakObjectPtr<AActor> WeakActor : Bucket->InactiveActors)
            {
                if (AActor* Actor = WeakActor.Get())
                {
                    Actor->Destroy();
                }
            }
        }
        Buckets.Remove(Key);
    }
}

FName URSBPoolManager::ResolvePoolKey(const FRSBSpawnRequest& Request) const
{
    if (!Request.PoolKey.IsNone())
    {
        return Request.PoolKey;
    }
    return Request.ActorClass ? Request.ActorClass->GetFName() : NAME_None;
}

FRSBPoolBucket& URSBPoolManager::GetOrCreateBucket(FName PoolKey, TSubclassOf<AActor> ActorClass)
{
    FRSBPoolBucket& Bucket = Buckets.FindOrAdd(PoolKey);
    if (Bucket.PoolKey.IsNone())
    {
        Bucket.PoolKey = PoolKey;
    }

    if (Bucket.ActorClass == nullptr)
    {
        Bucket.ActorClass = ActorClass;
    }

    if (Bucket.Capacity <= 0)
    {
        Bucket.Capacity = GetEffectiveCapacityForClass(ActorClass, PoolKey);
    }

    if (Bucket.LastTouchedSeconds <= 0.0)
    {
        Bucket.LastTouchedSeconds = FPlatformTime::Seconds();
    }

    return Bucket;
}
