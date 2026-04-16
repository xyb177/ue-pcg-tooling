#pragma once

#include "CoreMinimal.h"

class FPCGProfilerScope
{
public:
    FPCGProfilerScope(const TCHAR* InNodeName);
    FPCGProfilerScope(const FString& InNodeName);
    ~FPCGProfilerScope();

private:
    FString NodeName;
    uint64 StartCycles = 0;
};

#define PCG_PROFILER_SCOPE(NodeLabel) FPCGProfilerScope PREPROCESSOR_JOIN(PCGProfilerScope_, __LINE__)(NodeLabel)
