#include "RSBPressureSpawnerActor.h"

#include "Engine/World.h"
#include "TimerManager.h"
#include "RSBSpawnAsyncAction.h"
#include "RSBSpawnBudgetSubsystem.h"

ARSBPressureSpawnerActor::ARSBPressureSpawnerActor()
{
    PrimaryActorTick.bCanEverTick = false;
}

void ARSBPressureSpawnerActor::BeginPlay()
{
    Super::BeginPlay();
    StartBurstLoop();
}

void ARSBPressureSpawnerActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(BurstTimerHandle);
        World->GetTimerManager().ClearTimer(DestroyTimerHandle);
    }

    TrackedSpawnedActors.Reset();
    Super::EndPlay(EndPlayReason);
}

void ARSBPressureSpawnerActor::StartBurstLoop()
{
    RemainingBursts = FMath::Max(0, BurstCount);
    if (RemainingBursts <= 0)
    {
        return;
    }

    ExecuteBurst();

    if (UWorld* World = GetWorld())
    {
        if (RemainingBursts > 0)
        {
            World->GetTimerManager().SetTimer(
                BurstTimerHandle,
                this,
                &ARSBPressureSpawnerActor::ExecuteBurst,
                FMath::Max(0.01f, BurstIntervalSeconds),
                true);
        }
    }
}

void ARSBPressureSpawnerActor::ExecuteBurst()
{
    URSBSpawnBudgetSubsystem* Subsystem = ResolveSubsystem();
    if (!Subsystem || !SpawnActorClass)
    {
        RemainingBursts = 0;
        return;
    }

    const int32 BatchSize = FMath::Max(1, BurstSize);
    for (int32 Index = 0; Index < BatchSize; ++Index)
    {
        FRSBSpawnRequest Request;
        Request.ActorClass = SpawnActorClass;
        Request.Transform = GetActorTransform();
        Request.Priority = ERSBRequestPriority::Normal;
        Request.PoolKey = PoolKey.IsNone() ? SpawnActorClass->GetFName() : PoolKey;

        if (bUseAsyncSpawn)
        {
            URSBSpawnAsyncAction* Action = URSBSpawnAsyncAction::SpawnActorAsync(this, Request);
            if (Action)
            {
                Action->OnCompleted.AddDynamic(this, &ARSBPressureSpawnerActor::HandleAsyncSpawnCompleted);
                Action->OnFailed.AddDynamic(this, &ARSBPressureSpawnerActor::HandleAsyncSpawnFailed);
                Action->Activate();
            }
        }
        else
        {
            Subsystem->EnqueueSpawn(Request);
        }
    }

    --RemainingBursts;
    if (RemainingBursts <= 0)
    {
        if (UWorld* World = GetWorld())
        {
            World->GetTimerManager().ClearTimer(BurstTimerHandle);
            if (bDestroySpawnedActorsAfterDelay && DestroyDelaySeconds > 0.0f)
            {
                World->GetTimerManager().SetTimer(
                    DestroyTimerHandle,
                    this,
                    &ARSBPressureSpawnerActor::HandleDestroyTimer,
                    DestroyDelaySeconds,
                    false);
            }
        }
    }
}

void ARSBPressureSpawnerActor::QueueDestroyForTrackedActors()
{
    URSBSpawnBudgetSubsystem* Subsystem = ResolveSubsystem();
    if (!Subsystem)
    {
        return;
    }

    for (TWeakObjectPtr<AActor>& WeakActor : TrackedSpawnedActors)
    {
        if (!WeakActor.IsValid())
        {
            continue;
        }

        FRSBDestroyRequest DestroyRequest;
        DestroyRequest.Actor = WeakActor;
        DestroyRequest.Priority = ERSBRequestPriority::Normal;
        DestroyRequest.PoolKey = PoolKey.IsNone() ? SpawnActorClass->GetFName() : PoolKey;
        Subsystem->EnqueueDestroy(DestroyRequest);
    }

    TrackedSpawnedActors.Reset();
}

void ARSBPressureSpawnerActor::HandleAsyncSpawnCompleted(AActor* SpawnedActor)
{
    if (SpawnedActor)
    {
        TrackedSpawnedActors.Add(SpawnedActor);
    }
}

void ARSBPressureSpawnerActor::HandleAsyncSpawnFailed()
{
}

void ARSBPressureSpawnerActor::HandleDestroyTimer()
{
    QueueDestroyForTrackedActors();
}

URSBSpawnBudgetSubsystem* ARSBPressureSpawnerActor::ResolveSubsystem() const
{
    UWorld* World = GetWorld();
    return World ? World->GetSubsystem<URSBSpawnBudgetSubsystem>() : nullptr;
}
