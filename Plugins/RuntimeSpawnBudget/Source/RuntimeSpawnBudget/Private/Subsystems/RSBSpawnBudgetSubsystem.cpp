#include "RSBSpawnBudgetSubsystem.h"

#include "Containers/Ticker.h"
#include "GameFramework/Actor.h"
#include "RSBConfig.h"
#include "RSBMetricsCollector.h"
#include "RSBPolicyManager.h"
#include "RSBPoolManager.h"
#include "RSBSpawnExecutor.h"
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
}

void URSBSpawnBudgetSubsystem::Deinitialize()
{
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

    SpawnExecutor = nullptr;
    PoolManager = nullptr;
    PolicyManager = nullptr;
    MetricsCollector = nullptr;
    Config = nullptr;

    Super::Deinitialize();
}

bool URSBSpawnBudgetSubsystem::EnqueueSpawn(FRSBSpawnRequest Request)
{
    if (!IsSystemEnabled() || !Request.ActorClass)
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
    SpawnQueues[PriorityToIndex(Request.Priority)].Add(MoveTemp(Request));
    return true;
}

bool URSBSpawnBudgetSubsystem::EnqueueDestroy(FRSBDestroyRequest Request)
{
    if (!IsSystemEnabled() || !Request.Actor.IsValid())
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

bool URSBSpawnBudgetSubsystem::Tick(float DeltaTime)
{
    (void)DeltaTime;

    if (!IsSystemEnabled() || !PolicyManager || !MetricsCollector || !SpawnExecutor || !PoolManager)
    {
        return true;
    }

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
                ++Dropped;
                continue;
            }

            if (Request.EnqueuedAtSeconds > 0.0)
            {
                MetricsCollector->AddQueueDelayMs(static_cast<float>((CurrentSeconds - Request.EnqueuedAtSeconds) * 1000.0));
            }

            bool bPoolHit = false;
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
                ++SpawnProcessed;
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
