#include "RSBSpawnExecutor.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"

AActor* URSBSpawnExecutor::ExecuteSpawn(UWorld* World, const FRSBSpawnRequest& Request)
{
    if (!World || !Request.ActorClass)
    {
        return nullptr;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    return World->SpawnActor<AActor>(Request.ActorClass, Request.Transform, SpawnParams);
}

bool URSBSpawnExecutor::ExecuteDestroy(AActor* Actor, bool bForceDestroy)
{
    if (!Actor)
    {
        return false;
    }

    if (bForceDestroy || !Actor->IsPendingKillPending())
    {
        Actor->Destroy();
        return true;
    }

    return false;
}
