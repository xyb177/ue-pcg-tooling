# RuntimeSpawnBudget

`RuntimeSpawnBudget` is a UE5 runtime plugin for controlling high-frequency actor lifecycle operations with:

- per-frame spawn/destroy budget scheduling
- object pooling and prewarm/cull management
- queue-delay and spawn-cost metrics collection
- lightweight automation tests for regression safety

## Why this project

In gameplay-heavy scenarios, direct `Spawn/Destroy` bursts can cause frame spikes.
This plugin provides a reusable runtime layer to smooth workload and make results measurable.

## Core capabilities

1. Budget scheduler (`UWorldSubsystem`)
- Centralized `EnqueueSpawn` / `EnqueueDestroy`
- Per-frame op limits and time budget limits
- Priority queues: `Critical`, `High`, `Normal`, `Low`

2. Pool manager
- Reuse pooled actors before fallback spawning
- Prewarm support
- Idle bucket culling

3. Metrics
- Per-frame stats: processed counts, dropped requests, phase time
- Window stats: avg/p95/p99 queue delay and spawn time
- Pool hit rate tracking

4. Tests
- Queue defaults
- Policy config projection
- Pool initialization behavior
- Metrics window sanity

## Structure

```text
Plugins/RuntimeSpawnBudget/
  RuntimeSpawnBudget.uplugin
  Config/DefaultRuntimeSpawnBudget.ini
  Source/RuntimeSpawnBudget/
    Public/
      RSBSpawnBudgetSubsystem.h
      RSBPoolManager.h
      RSBPolicyManager.h
      RSBSpawnExecutor.h
      RSBMetricsCollector.h
      RSBTypes.h
      RSBConfig.h
      Interfaces/
    Private/
      Subsystems/
      Managers/
      Debug/
      Tests/
```

## Build and test

Build plugin package:

```powershell
Engine\Build\BatchFiles\RunUAT.bat BuildPlugin `
  -Plugin="F:\UnrealEngine-5.7.3-release\Plugins\RuntimeSpawnBudget\RuntimeSpawnBudget.uplugin" `
  -Package="F:\UnrealEngine-5.7.3-release\tmp\RuntimeSpawnBudget_Package" `
  -TargetPlatforms=Win64
```

Run automation tests in a project:

```powershell
Engine\Binaries\Win64\UnrealEditor-Cmd.exe "<YourProject>.uproject" `
  -unattended -nop4 -nosplash -NullRHI `
  -ExecCmds="Automation RunTests RuntimeSpawnBudget; Quit" `
  -TestExit="Automation Test Queue Empty"
```

## Notes

- Current test set focuses on core runtime logic and regressions.
- For portfolio/demo usage, pair this with an A/B stress scene to report `P95 frame time`, `P95 queue delay`, and `pool hit rate`.

## Quick stress scene

1. Add a `RuntimeSpawnBudget` enabled project or plugin build.
2. Place an `ARSBPressureSpawnerActor` in a test map, or call `URSBSpawnBudgetSubsystem::SpawnPressureActor` from a level blueprint.
3. Set `SpawnActorClass`, `BurstSize`, `BurstCount`, `BurstIntervalSeconds`, and optionally `PoolKey`.
4. Use `bUseAsyncSpawn=true` to exercise the async path and watch the debug tab for:
   - queue depth by priority
   - pending async count
   - per-class average/P95 spawn cost
   - pool hit rate
5. Compare against direct `Spawn/Destroy` in the same map to collect A/B evidence.
