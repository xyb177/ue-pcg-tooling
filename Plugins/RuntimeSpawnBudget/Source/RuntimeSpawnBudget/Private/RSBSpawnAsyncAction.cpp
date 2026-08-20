#include "RSBSpawnAsyncAction.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Interfaces/RSBSpawnRequestSourceInterface.h"
#include "RSBSpawnBudgetSubsystem.h"

namespace
{
    void NotifyRequestSourceFailure(const FRSBSpawnRequest& Request)
    {
        UObject* RequestSourceObject = Request.RequestSourceObject.Get();
        if (!RequestSourceObject || !RequestSourceObject->GetClass()->ImplementsInterface(URSBSpawnRequestSourceInterface::StaticClass()))
        {
            return;
        }

        IRSBSpawnRequestSourceInterface::Execute_HandleSpawnRequestFailed(RequestSourceObject, 0);
    }
}

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
        NotifyRequestSourceFailure(Request);
        NotifyFailed();
        return;
    }

    UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
    if (!World)
    {
        NotifyRequestSourceFailure(Request);
        NotifyFailed();
        return;
    }

    Subsystem = World->GetSubsystem<URSBSpawnBudgetSubsystem>();
    if (!Subsystem)
    {
        NotifyRequestSourceFailure(Request);
        NotifyFailed();
        return;
    }

    const int32 EnqueuedRequestId = Subsystem->PrepareSpawnRequest(Request);
    if (EnqueuedRequestId <= 0)
    {
        NotifyRequestSourceFailure(Request);
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
