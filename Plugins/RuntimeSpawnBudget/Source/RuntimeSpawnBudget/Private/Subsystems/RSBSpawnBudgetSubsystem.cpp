#include "RSBSpawnBudgetSubsystem.h"

#include "Async/Async.h"
#include "Containers/Ticker.h"
#include "GameFramework/Actor.h"
#include "EngineUtils.h"
#include "RSBConfig.h"
#include "RSBMetricsCollector.h"
#include "RSBPolicyManager.h"
#include "RSBPoolManager.h"
#include "RSBSpawnAsyncAction.h"
#include "RSBSpawnExecutor.h"
#include "RSBPressureSpawnerActor.h"
#include "Interfaces/RSBPoolableInterface.h"

namespace
{
    constexpr int32 RSBPriorityCount = 4;
}

void URSBSpawnBudgetSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    Config = GetDefault<URSBConfig>();

    MetricsCollector = NewObject<URSBMetricsCollector>(this);
    PolicyManager = NewObject<URSBPolicyManager>(this);
    PoolManager = NewObject<URSBPoolManager>(this);
    SpawnExecutor = NewObject<URSBSpawnExecutor>(this);

    if (PolicyManager)
    {
        PolicyManager->Initialize(Config);
    }

    if (MetricsCollector)
    {
        MetricsCollector->Initialize(Config ? Config->MetricsWindowSize : 256);
    }

    if (PoolManager)
    {
        PoolManager->Initialize(GetWorld(), Config);
    }

    if (IsSystemEnabled())
    {
        TickHandle = FTSTicker::GetCoreTicker().AddTicker(
            FTickerDelegate::CreateUObject(this, &URSBSpawnBudgetSubsystem::Tick));
    }

    UE_LOG(LogTemp, Display, TEXT("[RuntimeSpawnBudget] Initialize World=%s Enabled=%s MaxSpawnOps=%d MaxDestroyOps=%d MaxQueue=%d AutoSpawnPIE=%s"),
        *GetNameSafe(GetWorld()),
        IsSystemEnabled() ? TEXT("true") : TEXT("false"),
        Config ? Config->MaxSpawnOpsPerFrame : -1,
        Config ? Config->MaxDestroyOpsPerFrame : -1,
        Config ? Config->MaxQueueLength : -1,
        (Config && Config->bAutoSpawnPressureActorInPIE) ? TEXT("true") : TEXT("false"));
}

void URSBSpawnBudgetSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);
    UE_LOG(LogTemp, Display, TEXT("[RuntimeSpawnBudget] OnWorldBeginPlay World=%s Type=%d PIE=%s"), *InWorld.GetName(), static_cast<int32>(InWorld.WorldType), InWorld.IsPlayInEditor() ? TEXT("true") : TEXT("false"));
    TryAutoSpawnPressureActor();

    if (Config && Config->bAutoPrewarmOnWorldBeginPlay && Config->AutoPrewarmCount > 0)
    {
        TSubclassOf<AActor> TargetClass = nullptr;
        FName PoolKey = NAME_None;

        if (UWorld* World = GetWorld())
        {
            for (TActorIterator<ARSBPressureSpawnerActor> It(World); It; ++It)
            {
                if (ARSBPressureSpawnerActor* PressureActor = *It)
                {
                    if (PressureActor->SpawnActorClass)
                    {
                        TargetClass = PressureActor->SpawnActorClass;
                        PoolKey = PressureActor->PoolKey.IsNone() ? PressureActor->SpawnActorClass->GetFName() : PressureActor->PoolKey;
                        break;
                    }
                }
            }
        }

        if (TargetClass)
        {
            PrewarmPool(TargetClass, PoolKey, Config->AutoPrewarmCount);
            UE_LOG(LogTemp, Display, TEXT("[RuntimeSpawnBudget] AutoPrewarm class=%s count=%d"),
                *GetNameSafe(TargetClass.Get()),
                Config->AutoPrewarmCount);
        }
        else
        {
            UE_LOG(LogTemp, Display, TEXT("[RuntimeSpawnBudget] AutoPrewarm skipped: no target class found on pressure actors"));
        }
    }
}

void URSBSpawnBudgetSubsystem::Deinitialize()
{
    UE_LOG(LogTemp, Display, TEXT("[RuntimeSpawnBudget] Deinitialize World=%s"), *GetNameSafe(GetWorld()));

    if (TickHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
        TickHandle.Reset();
    }

    SpawnQueues[0].Reset();
    SpawnQueues[1].Reset();
    SpawnQueues[2].Reset();
    SpawnQueues[3].Reset();
    DestroyQueues[0].Reset();
    DestroyQueues[1].Reset();
    DestroyQueues[2].Reset();
    DestroyQueues[3].Reset();

    TArray<int32> PendingIds;
    PendingSpawnPromises.GenerateKeyArray(PendingIds);
    for (int32 RequestId : PendingIds)
    {
        ResolveAsyncSpawnFailure(RequestId);
    }

    TArray<int32> PendingActionIds;
    PendingSpawnActions.GenerateKeyArray(PendingActionIds);
    for (int32 RequestId : PendingActionIds)
    {
        ResolveAsyncSpawnFailure(RequestId);
    }

    SpawnExecutor = nullptr;
    PoolManager = nullptr;
    PolicyManager = nullptr;
    MetricsCollector = nullptr;
    Config = nullptr;

    Super::Deinitialize();
}

bool URSBSpawnBudgetSubsystem::EnqueueSpawn(FRSBSpawnRequest Request)
{
    if (!IsSystemEnabled())
    {
        return false;
    }

    if (IsInGameThread())
    {
        return EnqueueSpawnInternal(MoveTemp(Request));
    }

    FRSBSpawnRequest Copy = MoveTemp(Request);
    AsyncTask(ENamedThreads::GameThread, [WeakThis = TWeakObjectPtr<URSBSpawnBudgetSubsystem>(this), Copy = MoveTemp(Copy)]() mutable
    {
        if (URSBSpawnBudgetSubsystem* StrongThis = WeakThis.Get())
        {
            StrongThis->EnqueueSpawnInternal(MoveTemp(Copy));
        }
    });
    return true;
}

TFuture<AActor*> URSBSpawnBudgetSubsystem::EnqueueSpawnFuture(FRSBSpawnRequest Request)
{
    TSharedPtr<TPromise<AActor*>> Promise = MakeShared<TPromise<AActor*>>();
    TFuture<AActor*> Future = Promise->GetFuture();

    if (!IsSystemEnabled())
    {
        Promise->SetValue(nullptr);
        return Future;
    }

    if (IsInGameThread())
    {
        return EnqueueSpawnFutureInternal(MoveTemp(Request), Promise);
    }

    FRSBSpawnRequest Copy = MoveTemp(Request);
    AsyncTask(ENamedThreads::GameThread, [WeakThis = TWeakObjectPtr<URSBSpawnBudgetSubsystem>(this), Copy = MoveTemp(Copy), Promise]() mutable
    {
        if (URSBSpawnBudgetSubsystem* StrongThis = WeakThis.Get())
        {
            StrongThis->EnqueueSpawnFutureInternal(MoveTemp(Copy), Promise);
        }
        else
        {
            Promise->SetValue(nullptr);
        }
    });
    return Future;
}

bool URSBSpawnBudgetSubsystem::EnqueueDestroy(FRSBDestroyRequest Request)
{
    if (!IsSystemEnabled())
    {
        return false;
    }

    if (IsInGameThread())
    {
        return EnqueueDestroyInternal(MoveTemp(Request));
    }

    FRSBDestroyRequest Copy = MoveTemp(Request);
    AsyncTask(ENamedThreads::GameThread, [WeakThis = TWeakObjectPtr<URSBSpawnBudgetSubsystem>(this), Copy = MoveTemp(Copy)]() mutable
    {
        if (URSBSpawnBudgetSubsystem* StrongThis = WeakThis.Get())
        {
            StrongThis->EnqueueDestroyInternal(MoveTemp(Copy));
        }
    });
    return true;
}

void URSBSpawnBudgetSubsystem::PrewarmPool(TSubclassOf<AActor> ActorClass, FName PoolKey, int32 Count)
{
    if (!IsSystemEnabled() || !PoolManager || !ActorClass || Count <= 0)
    {
        return;
    }

    PoolManager->Prewarm(ActorClass, PoolKey, Count);
}

FRSBFrameStats URSBSpawnBudgetSubsystem::GetLastFrameStats() const
{
    return MetricsCollector ? MetricsCollector->GetLastFrameStats() : FRSBFrameStats{};
}

FRSBWindowStats URSBSpawnBudgetSubsystem::GetWindowStatsBlueprint() const
{
    return GetWindowStats_Implementation();
}

FRSBWindowStats URSBSpawnBudgetSubsystem::GetWindowStats_Implementation() const
{
    return MetricsCollector ? MetricsCollector->BuildWindowStats(GetPendingQueueLength()) : FRSBWindowStats{};
}

FRSBQueueSnapshot URSBSpawnBudgetSubsystem::GetQueueSnapshot() const
{
    FRSBQueueSnapshot Snapshot;
    Snapshot.SpawnCritical = SpawnQueues[0].Num();
    Snapshot.SpawnHigh = SpawnQueues[1].Num();
    Snapshot.SpawnNormal = SpawnQueues[2].Num();
    Snapshot.SpawnLow = SpawnQueues[3].Num();
    Snapshot.DestroyCritical = DestroyQueues[0].Num();
    Snapshot.DestroyHigh = DestroyQueues[1].Num();
    Snapshot.DestroyNormal = DestroyQueues[2].Num();
    Snapshot.DestroyLow = DestroyQueues[3].Num();
    Snapshot.PendingAsyncCount = PendingSpawnPromises.Num() + PendingSpawnActions.Num();
    return Snapshot;
}

TArray<FRSBWorldActorClassEntry> URSBSpawnBudgetSubsystem::GetWorldActorClassCatalog() const
{
    TArray<FRSBWorldActorClassEntry> Result;
    UWorld* World = GetWorld();
    if (!World)
    {
        return Result;
    }

    TMap<FName, FRSBWorldActorClassEntry> Entries;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        const AActor* Actor = *It;
        if (!Actor || Actor->IsA<ARSBPressureSpawnerActor>() || Actor->IsA<AWorldSettings>())
        {
            continue;
        }

        UClass* ActorClass = Actor->GetClass();
        if (!ActorClass || ActorClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
        {
            continue;
        }

        const FName ClassName = ActorClass->GetFName();
        FRSBWorldActorClassEntry& Entry = Entries.FindOrAdd(ClassName);
        Entry.ActorClassName = ClassName;
        Entry.Count += 1;
        Entry.bIsBlueprintClass = ActorClass->HasAnyClassFlags(CLASS_CompiledFromBlueprint);
    }

    for (const TPair<FName, FRSBWorldActorClassEntry>& Pair : Entries)
    {
        Result.Add(Pair.Value);
    }

    Result.Sort([](const FRSBWorldActorClassEntry& Left, const FRSBWorldActorClassEntry& Right)
    {
        if (Left.Count != Right.Count)
        {
            return Left.Count > Right.Count;
        }
        return Left.ActorClassName.LexicalLess(Right.ActorClassName);
    });

    return Result;
}

ARSBPressureSpawnerActor* URSBSpawnBudgetSubsystem::SpawnPressureActor(TSubclassOf<ARSBPressureSpawnerActor> PressureActorClass, const FTransform& SpawnTransform)
{
    if (!IsSystemEnabled() || !GetWorld() || !PressureActorClass)
    {
        return nullptr;
    }

    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    return GetWorld()->SpawnActor<ARSBPressureSpawnerActor>(PressureActorClass, SpawnTransform, Params);
}

void URSBSpawnBudgetSubsystem::RegisterAsyncAction(int32 RequestId, URSBSpawnAsyncAction* Action)
{
    if (RequestId <= 0 || !Action)
    {
        return;
    }

    PendingSpawnActions.FindOrAdd(RequestId).Add(Action);
}

void URSBSpawnBudgetSubsystem::UnregisterAsyncAction(int32 RequestId, URSBSpawnAsyncAction* Action)
{
    if (RequestId <= 0 || !Action)
    {
        return;
    }

    if (TArray<TWeakObjectPtr<URSBSpawnAsyncAction>>* Actions = PendingSpawnActions.Find(RequestId))
    {
        Actions->RemoveAll([Action](const TWeakObjectPtr<URSBSpawnAsyncAction>& Item)
        {
            return !Item.IsValid() || Item.Get() == Action;
        });

        if (Actions->IsEmpty())
        {
            PendingSpawnActions.Remove(RequestId);
        }
    }
}

int32 URSBSpawnBudgetSubsystem::PrepareSpawnRequest(FRSBSpawnRequest& Request)
{
    check(IsInGameThread());
    if (!IsSystemEnabled() || !Request.ActorClass)
    {
        return 0;
    }

    const int32 PendingCount = GetPendingQueueLength();
    if (Config && PendingCount >= Config->MaxQueueLength)
    {
        return 0;
    }

    Request.RequestId = NextRequestId++;
    Request.EnqueuedAtSeconds = FPlatformTime::Seconds();
    SpawnQueues[PriorityToIndex(Request.Priority)].Add(Request);
    return Request.RequestId;
}

bool URSBSpawnBudgetSubsystem::EnqueueSpawnInternal(FRSBSpawnRequest Request)
{
    return PrepareSpawnRequest(Request) > 0;
}

TFuture<AActor*> URSBSpawnBudgetSubsystem::EnqueueSpawnFutureInternal(FRSBSpawnRequest Request, TSharedPtr<TPromise<AActor*>> Promise)
{
    TFuture<AActor*> Future = Promise->GetFuture();
    const int32 RequestId = PrepareSpawnRequest(Request);
    if (RequestId <= 0)
    {
        Promise->SetValue(nullptr);
        return Future;
    }

    PendingSpawnPromises.Add(RequestId, Promise);
    return Future;
}

bool URSBSpawnBudgetSubsystem::EnqueueDestroyInternal(FRSBDestroyRequest Request)
{
    check(IsInGameThread());

    if (!Request.Actor.IsValid())
    {
        return false;
    }

    const int32 PendingCount = GetPendingQueueLength();
    if (Config && PendingCount >= Config->MaxQueueLength)
    {
        return false;
    }

    Request.RequestId = NextRequestId++;
    Request.EnqueuedAtSeconds = FPlatformTime::Seconds();
    DestroyQueues[PriorityToIndex(Request.Priority)].Add(MoveTemp(Request));
    return true;
}

void URSBSpawnBudgetSubsystem::ResolveAsyncSpawnResult(int32 RequestId, AActor* Actor)
{
    if (RequestId <= 0)
    {
        return;
    }

    if (TSharedPtr<TPromise<AActor*>>* Promise = PendingSpawnPromises.Find(RequestId))
    {
        if (Promise->IsValid())
        {
            (*Promise)->SetValue(Actor);
        }
        PendingSpawnPromises.Remove(RequestId);
    }

    if (TArray<TWeakObjectPtr<URSBSpawnAsyncAction>>* Actions = PendingSpawnActions.Find(RequestId))
    {
        for (TWeakObjectPtr<URSBSpawnAsyncAction> WeakAction : *Actions)
        {
            if (URSBSpawnAsyncAction* Action = WeakAction.Get())
            {
                if (Actor)
                {
                    Action->NotifyCompleted(Actor);
                }
                else
                {
                    Action->NotifyFailed();
                }
            }
        }
        PendingSpawnActions.Remove(RequestId);
    }
}

void URSBSpawnBudgetSubsystem::ResolveAsyncSpawnFailure(int32 RequestId)
{
    ResolveAsyncSpawnResult(RequestId, nullptr);
}

bool URSBSpawnBudgetSubsystem::Tick(float DeltaTime)
{
    (void)DeltaTime;

    if (!IsSystemEnabled() || !PolicyManager || !MetricsCollector || !SpawnExecutor || !PoolManager)
    {
        return true;
    }

#if WITH_EDITOR
    TryAutoSpawnPressureActor();
#endif

    const double FrameStartSeconds = FPlatformTime::Seconds();
    const int32 PendingQueueLength = GetPendingQueueLength();
    MetricsCollector->BeginFrame(GFrameCounter, PendingQueueLength);

    const FRSBBudgetSnapshot Budget = PolicyManager->GetBudget();
    const double NowSeconds = FPlatformTime::Seconds();
    PoolManager->CullIdlePools(NowSeconds);

    int32 SpawnProcessed = 0;
    int32 DestroyProcessed = 0;
    int32 Dropped = 0;
    double SpawnEndSeconds = FrameStartSeconds;

    auto ProcessSpawnQueue = [&](int32 QueueIndex)
    {
        TArray<FRSBSpawnRequest>& Queue = SpawnQueues[QueueIndex];
        while (!Queue.IsEmpty() && SpawnProcessed < Budget.MaxSpawnOpsPerFrame)
        {
            const double CurrentSeconds = FPlatformTime::Seconds();
            if (Budget.MaxSpawnTimeMsPerFrame > 0.0f &&
                ((CurrentSeconds - FrameStartSeconds) * 1000.0) >= static_cast<double>(Budget.MaxSpawnTimeMsPerFrame))
            {
                break;
            }

            FRSBSpawnRequest Request = MoveTemp(Queue[0]);
            Queue.RemoveAt(0, 1, EAllowShrinking::No);

            if (Request.DeadlineSeconds >= 0.0f && CurrentSeconds > Request.DeadlineSeconds)
            {
                ResolveAsyncSpawnFailure(Request.RequestId);
                ++Dropped;
                continue;
            }

            if (Request.EnqueuedAtSeconds > 0.0)
            {
                MetricsCollector->AddQueueDelayMs(static_cast<float>((CurrentSeconds - Request.EnqueuedAtSeconds) * 1000.0));
            }

            bool bPoolHit = false;
            const double SpawnStartSeconds = FPlatformTime::Seconds();
            AActor* SpawnedActor = PoolManager->Acquire(Request, bPoolHit);
            if (SpawnedActor)
            {
                if (bPoolHit)
                {
                    MetricsCollector->AddPoolHit();
                }
                else
                {
                    MetricsCollector->AddPoolMiss();
                    SpawnedActor = SpawnExecutor->ExecuteSpawn(GetWorld(), Request);
                    if (SpawnedActor)
                    {
                        if (SpawnedActor->GetClass()->ImplementsInterface(URSBPoolableInterface::StaticClass()))
                        {
                            IRSBPoolableInterface::Execute_OnPooledAcquire(SpawnedActor);
                        }
                    }
                }
            }
            else
            {
                MetricsCollector->AddPoolMiss();
                SpawnedActor = SpawnExecutor->ExecuteSpawn(GetWorld(), Request);
                if (SpawnedActor && SpawnedActor->GetClass()->ImplementsInterface(URSBPoolableInterface::StaticClass()))
                {
                    IRSBPoolableInterface::Execute_OnPooledAcquire(SpawnedActor);
                }
            }

            if (SpawnedActor)
            {
                const float SpawnCostMs = static_cast<float>((FPlatformTime::Seconds() - SpawnStartSeconds) * 1000.0);
                MetricsCollector->AddActorClassSpawnSample(Request.ActorClass.Get(), SpawnCostMs, bPoolHit);
                ResolveAsyncSpawnResult(Request.RequestId, SpawnedActor);
                ++SpawnProcessed;
            }
            else
            {
                ResolveAsyncSpawnFailure(Request.RequestId);
            }
        }
    };

    for (int32 Index = 0; Index < RSBPriorityCount; ++Index)
    {
        ProcessSpawnQueue(Index);
    }

    SpawnEndSeconds = FPlatformTime::Seconds();

    auto ProcessDestroyQueue = [&](int32 QueueIndex)
    {
        TArray<FRSBDestroyRequest>& Queue = DestroyQueues[QueueIndex];
        while (!Queue.IsEmpty() && DestroyProcessed < Budget.MaxDestroyOpsPerFrame)
        {
            const double CurrentSeconds = FPlatformTime::Seconds();
            if (Budget.MaxDestroyTimeMsPerFrame > 0.0f &&
                ((CurrentSeconds - SpawnEndSeconds) * 1000.0) >= static_cast<double>(Budget.MaxDestroyTimeMsPerFrame))
            {
                break;
            }

            FRSBDestroyRequest Request = MoveTemp(Queue[0]);
            Queue.RemoveAt(0, 1, EAllowShrinking::No);

            if (!Request.Actor.IsValid())
            {
                ++Dropped;
                continue;
            }

            if (Request.EnqueuedAtSeconds > 0.0)
            {
                MetricsCollector->AddQueueDelayMs(static_cast<float>((CurrentSeconds - Request.EnqueuedAtSeconds) * 1000.0));
            }

            if (PoolManager)
            {
                PoolManager->Release(Request.Actor.Get(), Request.PoolKey, Request.bForceDestroy);
            }
            else
            {
                SpawnExecutor->ExecuteDestroy(Request.Actor.Get(), Request.bForceDestroy);
            }

            ++DestroyProcessed;
        }
    };

    for (int32 Index = 0; Index < RSBPriorityCount; ++Index)
    {
        ProcessDestroyQueue(Index);
    }

    const float SpawnPhaseMs = static_cast<float>((SpawnEndSeconds - FrameStartSeconds) * 1000.0);
    const float TotalPhaseMs = static_cast<float>((FPlatformTime::Seconds() - FrameStartSeconds) * 1000.0);
    const float DestroyPhaseMs = FMath::Max(0.0f, TotalPhaseMs - SpawnPhaseMs);

    MetricsCollector->EndFrame(SpawnProcessed, DestroyProcessed, Dropped, SpawnPhaseMs, DestroyPhaseMs);
    return true;
}

int32 URSBSpawnBudgetSubsystem::PriorityToIndex(ERSBRequestPriority Priority) const
{
    switch (Priority)
    {
    case ERSBRequestPriority::Critical:
        return 0;
    case ERSBRequestPriority::High:
        return 1;
    case ERSBRequestPriority::Normal:
        return 2;
    case ERSBRequestPriority::Low:
    default:
        return 3;
    }
}

int32 URSBSpawnBudgetSubsystem::GetPendingQueueLength() const
{
    int32 Total = 0;
    for (const TArray<FRSBSpawnRequest>& Queue : SpawnQueues)
    {
        Total += Queue.Num();
    }

    for (const TArray<FRSBDestroyRequest>& Queue : DestroyQueues)
    {
        Total += Queue.Num();
    }

    return Total;
}

bool URSBSpawnBudgetSubsystem::IsSystemEnabled() const
{
    return Config ? Config->bEnableSystem : true;
}

void URSBSpawnBudgetSubsystem::TryAutoSpawnPressureActor()
{
    if (bAutoSpawnPressureActorAttempted)
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!World || !Config || !Config->bAutoSpawnPressureActorInPIE)
    {
        return;
    }

    const EWorldType::Type WorldType = World->WorldType;
    const bool bCanAutoSpawn = (WorldType == EWorldType::PIE || WorldType == EWorldType::GamePreview || WorldType == EWorldType::Game);
    if (!bCanAutoSpawn)
    {
        return;
    }

    bAutoSpawnPressureActorAttempted = true;

    bool bHasPressureActor = false;
    for (TActorIterator<ARSBPressureSpawnerActor> It(World); It; ++It)
    {
        bHasPressureActor = true;
        break;
    }

    UE_LOG(LogTemp, Display, TEXT("[RuntimeSpawnBudget] TryAutoSpawnPressureActor World=%s Type=%d HasPressureActor=%s"),
        *World->GetName(), static_cast<int32>(WorldType), bHasPressureActor ? TEXT("true") : TEXT("false"));

    if (bHasPressureActor)
    {
        return;
    }

    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ARSBPressureSpawnerActor* PressureActor = World->SpawnActor<ARSBPressureSpawnerActor>(
        ARSBPressureSpawnerActor::StaticClass(),
        FTransform::Identity,
        Params);

    UE_LOG(LogTemp, Display, TEXT("[RuntimeSpawnBudget] AutoSpawnPressureActor Result=%s"), PressureActor ? TEXT("success") : TEXT("failed"));

    if (PressureActor)
    {
#if WITH_EDITOR
        PressureActor->SetActorLabel(TEXT("RSB_AutoPressureSpawner"));
#endif
    }
}
