#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "RSBConfig.generated.h"

UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Runtime Spawn Budget Settings"))
class RUNTIMESPAWNBUDGET_API URSBConfig : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    UPROPERTY(Config, EditAnywhere, Category="General")
    bool bEnableSystem = true;

    UPROPERTY(Config, EditAnywhere, Category="Budget", meta=(ClampMin="1"))
    int32 MaxSpawnOpsPerFrame = 32;

    UPROPERTY(Config, EditAnywhere, Category="Budget", meta=(ClampMin="1"))
    int32 MaxDestroyOpsPerFrame = 32;

    UPROPERTY(Config, EditAnywhere, Category="Budget", meta=(ClampMin="0.0"))
    float MaxSpawnTimeMsPerFrame = 1.5f;

    UPROPERTY(Config, EditAnywhere, Category="Budget", meta=(ClampMin="0.0"))
    float MaxDestroyTimeMsPerFrame = 1.0f;

    UPROPERTY(Config, EditAnywhere, Category="Queue", meta=(ClampMin="64"))
    int32 MaxQueueLength = 4096;

    UPROPERTY(Config, EditAnywhere, Category="Pool", meta=(ClampMin="0"))
    int32 DefaultPoolCapacity = 128;

    UPROPERTY(Config, EditAnywhere, Category="Pool")
    TMap<FName, int32> PerClassPoolCapacityOverrides;

    UPROPERTY(Config, EditAnywhere, Category="Pool", meta=(ClampMin="1.0"))
    float PoolIdleCullSeconds = 60.0f;

    UPROPERTY(Config, EditAnywhere, Category="Metrics", meta=(ClampMin="16"))
    int32 MetricsWindowSize = 256;

    UPROPERTY(Config, EditAnywhere, Category="Debug")
    bool bAutoSpawnPressureActorInPIE = true;

    UPROPERTY(Config, EditAnywhere, Category="Prewarm")
    bool bAutoPrewarmOnWorldBeginPlay = true;

    UPROPERTY(Config, EditAnywhere, Category="Prewarm", meta=(ClampMin="1"))
    int32 AutoPrewarmCount = 32;

    virtual FName GetCategoryName() const override { return FName(TEXT("Plugins")); }
};
