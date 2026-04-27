# PCGProfiler Scripts

## Common Principles
- Prefer explicit parameters in CI: pass `-EngineRoot` and `-ProjectPath` (or `-ProjectRoot` for evidence).
- If omitted, scripts try to infer local defaults through `PCGProfiler.ScriptCommon.ps1`.
- Output artifacts are written under project `Saved/Profiling/PCG` and plugin `Plugins/PCGProfiler/Reports`.

## Main Entry Scripts

1. `Run-PCGProfilerBenchmark.ps1`
- Purpose: multi-map, multi-iteration benchmark for stability and regression summary.
- Core outputs: `pcg_benchmark_summary_*.json` and `pcg_benchmark_summary_*.md`.
- Example:
```powershell
powershell -ExecutionPolicy Bypass -File .\Run-PCGProfilerBenchmark.ps1 `
  -EngineRoot "F:\UnrealEngine-5.7.3-release" `
  -ProjectPath "F:\Unreal Projects\ElectricDreamsEnv\ElectricDreamsEnv.uproject" `
  -IterationsPerMap 5 -Preset custom
```

2. `Run-PCGProfilerThreeLayerBenchmark.ps1`
- Purpose: layered benchmark (`editor_fixed_camera`, `pie_or_standalone_fixed_path`, optional packaged).
- Output: layer summary jsons in `Plugins/PCGProfiler/Reports`.

3. `Run-PCGProfilerQualityGate.ps1`
- Purpose: evaluate one or more layer summaries against CV thresholds.
- Output: quality gate `json` + `md`.

4. `Run-PCGProfilerStartWaitFinish.ps1`
- Purpose: visible editor smoke-flow (`start -> wait -> finish export`).
- Good for manual validation of pipeline behavior.

5. `Run-PCGProfilerEvidence.ps1`
- Purpose: produce A/B evidence report from two JSON runs (or auto-pick latest two under project profiling dir).

6. `Run-PCGProfilerCaseStudies.ps1`
- Purpose: run practical optimization case studies and summarize deltas.

## Internal Helpers (not primary user entry)
- `pcg_start_run.py`, `pcg_wait_until_idle.py`, `pcg_finish_export.py`
- `generate_pcg_dashboard.py`, `generate_pcg_batch_summary.py`

## Notes
- For reproducibility, fix map list, iteration count, and runtime mode (`-UseNullRHI` vs rendered run).
- For batched runs, prefer quality gate + batch summary instead of manual per-json inspection.
