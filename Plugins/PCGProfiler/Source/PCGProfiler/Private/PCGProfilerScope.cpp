#include "PCGProfilerScope.h"

#include "Engine/Engine.h"
#include "PCGProfilerSubsystem.h"

FPCGProfilerScope::FPCGProfilerScope(const TCHAR* InNodeName)
    : NodeName(InNodeName ? InNodeName : TEXT("UnknownNode"))
    , StartCycles(FPlatformTime::Cycles64())
{
}

FPCGProfilerScope::FPCGProfilerScope(const FString& InNodeName)
    : NodeName(InNodeName.IsEmpty() ? TEXT("UnknownNode") : InNodeName)
    , StartCycles(FPlatformTime::Cycles64())
{
}

FPCGProfilerScope::~FPCGProfilerScope()
{
    if (!GEngine)
    {
        return;
    }

    if (UPCGProfilerSubsystem* Profiler = GEngine->GetEngineSubsystem<UPCGProfilerSubsystem>())
    {
        const uint64 EndCycles = FPlatformTime::Cycles64();
        const double DurationMs = FPlatformTime::ToMilliseconds64(EndCycles - StartCycles);
        Profiler->RecordNodeTiming(NodeName, DurationMs);
    }
}
