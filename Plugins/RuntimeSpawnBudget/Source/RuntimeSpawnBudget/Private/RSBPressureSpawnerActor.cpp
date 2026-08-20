#include "RSBPressureSpawnerActor.h"

#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "RSBSpawnAsyncAction.h"
#include "RSBSpawnBudgetSubsystem.h"

ARSBPressureSpawnerActor::ARSBPressureSpawnerActor()
{
    PrimaryActorTick.bCanEverTick = false;
    SpawnActorClass = AActor::StaticClass();
}

void ARSBPressureSpawnerActor::BeginPlay()
{
    Super::BeginPlay();

    if (bAutoResolveSpawnActorClassFromWorld)
    {
        if (TSubclassOf<AActor> ResolvedClass = ResolveSpawnActorClassFromWorld())
        {
            SpawnActorClass = ResolvedClass;
        }
    }

    if (!SpawnActorClass)
    {
        SpawnActorClass = AActor::StaticClass();
    }

    UE_LOG(LogTemp, Display, TEXT("[RuntimeSpawnBudget] PressureActor BeginPlay=%s SpawnClass=%s BurstSize=%d BurstCount=%d Async=%s"),
        *GetName(),
        SpawnActorClass ? *SpawnActorClass->GetName() : TEXT("None"),
        BurstSize,
        BurstCount,
        bUseAsyncSpawn ? TEXT("true") : TEXT("false"));

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

void ARSBPressureSpawnerActor::HandleSpawnRequestCompleted_Implementation(int32 RequestId, AActor* SpawnedActor)
{
    (void)RequestId;

    if (SpawnedActor)
    {
        TrackedSpawnedActors.AddUnique(SpawnedActor);
    }
}

void ARSBPressureSpawnerActor::HandleSpawnRequestFailed_Implementation(int32 RequestId)
{
    (void)RequestId;
}

void ARSBPressureSpawnerActor::StartBurstLoop()
{
    RemainingBursts = FMath::Max(0, BurstCount);
    if (RemainingBursts <= 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[RuntimeSpawnBudget] PressureActor StartBurstLoop aborted: BurstCount <= 0 for %s"), *GetName());
        return;
    }

    UE_LOG(LogTemp, Display, TEXT("[RuntimeSpawnBudget] PressureActor StartBurstLoop=%s RemainingBursts=%d"), *GetName(), RemainingBursts);
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

TSubclassOf<AActor> ARSBPressureSpawnerActor::ResolveSpawnActorClassFromWorld() const
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return nullptr;
    }

    if (!bPreferPlacedActorClass)
    {
        return SpawnActorClass;
    }

    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* Candidate = *It;
        if (!Candidate || Candidate == this)
        {
            continue;
        }

        if (Candidate->IsA<ARSBPressureSpawnerActor>())
        {
            continue;
        }

        if (Candidate->IsA<AWorldSettings>())
        {
            continue;
        }

        UClass* CandidateClass = Candidate->GetClass();
        if (CandidateClass && !CandidateClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
        {
            UE_LOG(LogTemp, Display, TEXT("[RuntimeSpawnBudget] PressureActor resolved SpawnClass from world actor %s -> %s"),
                *GetNameSafe(Candidate),
                *GetNameSafe(CandidateClass));
            return CandidateClass;
        }
    }

    return SpawnActorClass;
}

void ARSBPressureSpawnerActor::ExecuteBurst()
{
    URSBSpawnBudgetSubsystem* Subsystem = ResolveSubsystem();
    if (!Subsystem)
    {
        UE_LOG(LogTemp, Warning, TEXT("[RuntimeSpawnBudget] PressureActor ExecuteBurst failed: subsystem missing for %s"), *GetName());
        RemainingBursts = 0;
        return;
    }

    if (!SpawnActorClass)
    {
        SpawnActorClass = ResolveSpawnActorClassFromWorld();
        if (!SpawnActorClass)
        {
            SpawnActorClass = AActor::StaticClass();
        }
    }

    const int32 BatchSize = FMath::Max(1, BurstSize);
    UE_LOG(LogTemp, Display, TEXT("[RuntimeSpawnBudget] PressureActor ExecuteBurst=%s BatchSize=%d Async=%s Class=%s"),
        *GetName(),
        BatchSize,
        bUseAsyncSpawn ? TEXT("true") : TEXT("false"),
        SpawnActorClass ? *SpawnActorClass->GetName() : TEXT("None"));
    for (int32 Index = 0; Index < BatchSize; ++Index)
    {
        FRSBSpawnRequest Request;
        Request.ActorClass = SpawnActorClass;
        Request.Transform = GetActorTransform();
        Request.Priority = ERSBRequestPriority::Normal;
        Request.PoolKey = PoolKey.IsNone() ? SpawnActorClass->GetFName() : PoolKey;
        Request.RequestSourceObject = this;

        if (bUseAsyncSpawn)
        {
            URSBSpawnAsyncAction* Action = URSBSpawnAsyncAction::SpawnActorAsync(this, Request);
            if (Action)
            {
                Action->Activate();
                UE_LOG(LogTemp, Display, TEXT("[RuntimeSpawnBudget] PressureActor async request queued: %s"), *GetName());
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("[RuntimeSpawnBudget] PressureActor async action creation failed: %s"), *GetName());
            }
        }
        else
        {
            const bool bEnqueued = Subsystem->EnqueueSpawn(Request);
            UE_LOG(LogTemp, Display, TEXT("[RuntimeSpawnBudget] PressureActor sync enqueue: %s result=%s"), *GetName(), bEnqueued ? TEXT("true") : TEXT("false"));
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

void ARSBPressureSpawnerActor::HandleDestroyTimer()
{
    UE_LOG(LogTemp, Display, TEXT("[RuntimeSpawnBudget] PressureActor destroy timer fired: %s tracked=%d"), *GetName(), TrackedSpawnedActors.Num());
    QueueDestroyForTrackedActors();
}

URSBSpawnBudgetSubsystem* ARSBPressureSpawnerActor::ResolveSubsystem() const
{
    UWorld* World = GetWorld();
    return World ? World->GetSubsystem<URSBSpawnBudgetSubsystem>() : nullptr;
}
