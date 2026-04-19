#include "Debug/RSBDebugPanel.h"

#include "RSBSpawnBudgetSubsystem.h"

void URSBDebugPanel::BindSubsystem(URSBSpawnBudgetSubsystem* InSubsystem)
{
    Subsystem = InSubsystem;
}

bool URSBDebugPanel::IsBound() const
{
    return Subsystem != nullptr;
}

FRSBFrameStats URSBDebugPanel::GetLastFrameStats() const
{
    return Subsystem ? Subsystem->GetLastFrameStats() : FRSBFrameStats{};
}

FRSBWindowStats URSBDebugPanel::GetWindowStats() const
{
    return Subsystem ? Subsystem->GetWindowStatsBlueprint() : FRSBWindowStats{};
}
