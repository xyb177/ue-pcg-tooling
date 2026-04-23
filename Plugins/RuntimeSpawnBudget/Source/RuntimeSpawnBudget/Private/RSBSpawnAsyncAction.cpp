#include "RSBSpawnAsyncAction.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "RSBSpawnBudgetSubsystem.h"

URSBSpawnAsyncAction* URSBSpawnAsyncAction::SpawnActorAsync(UObject* InWorldContextObject, FRSBSpawnRequest InRequest)
{
    URSBSpawnAsyncAction* Action = NewObject<URSBSpawnAsyncAction>();
    Action->WorldContextObject = InWorldContextObject;
    Action->Request = InRequest;
    return Action;
}

void URSBSpawnAsyncAction::Activate()
{
    if (!WorldContextObject)
    {
        NotifyFailed();
        return;
    }

    UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
    if (!World)
    {
        NotifyFailed();
        return;
    }

    Subsystem = World->GetSubsystem<URSBSpawnBudgetSubsystem>();
    if (!Subsystem)
    {
        NotifyFailed();
        return;
    }

    const int32 EnqueuedRequestId = Subsystem->PrepareSpawnRequest(Request);
    if (EnqueuedRequestId <= 0)
    {
        NotifyFailed();
        return;
    }

    RequestId = EnqueuedRequestId;
    AddToRoot();
    Subsystem->RegisterAsyncAction(RequestId, this);
}

void URSBSpawnAsyncAction::BeginDestroy()
{
    if (!bFinished && Subsystem && RequestId > 0)
    {
        Subsystem->UnregisterAsyncAction(RequestId, this);
    }

    Super::BeginDestroy();
}

void URSBSpawnAsyncAction::NotifyCompleted(AActor* SpawnedActor)
{
    if (bFinished)
    {
        return;
    }

    OnCompleted.Broadcast(SpawnedActor);
    CompleteAndRelease();
}

void URSBSpawnAsyncAction::NotifyFailed()
{
    if (bFinished)
    {
        return;
    }

    OnFailed.Broadcast();
    CompleteAndRelease();
}

void URSBSpawnAsyncAction::CompleteAndRelease()
{
    bFinished = true;
    RemoveFromRoot();
    SetReadyToDestroy();
}
