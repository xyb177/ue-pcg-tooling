param(
    [string]$EngineRoot = "F:\UnrealEngine-5.7.3-release",
    [string]$ProjectPath = "F:\Unreal Projects\ElectricDreamsEnv\ElectricDreamsEnv.uproject",
    [string]$Map = "/Game/Levels/PCG/ElectricDreams_PCG",
    [int]$IterationsPerCase = 3,
    [switch]$UseNullRHI = $true
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$OutputEncoding = [System.Text.UTF8Encoding]::new($false)

function Get-TopNodeP95([object]$JsonObj) {
    $values = @()
    foreach ($n in @($JsonObj.nodes)) {
        if ($null -ne $n.p95_ms) {
            $values += [double]$n.p95_ms
        }
    }
    if ($values.Count -eq 0) { return 0.0 }
    return ($values | Measure-Object -Maximum).Maximum
}

function Get-ThreadValue([object]$JsonObj, [string]$name) {
    if ($null -eq $JsonObj.thread_load) { return 0.0 }
    $member = $JsonObj.thread_load.PSObject.Properties[$name]
    if ($null -eq $member) { return 0.0 }
    return [double]$member.Value
}

function Get-CacheHitRate([object]$JsonObj) {
    if ($null -eq $JsonObj.cache_analysis) { return 0.0 }
    $member = $JsonObj.cache_analysis.PSObject.Properties["graph_cache_hit_rate"]
    if ($null -eq $member) { return 0.0 }
    return [double]$member.Value
}

function New-CaseWrapper(
    [string]$WrapperPath,
    [bool]$SamplingEnabled,
    [string]$MemoryMode,
    [int]$PrewarmCount
) {
    $sampling = if ($SamplingEnabled) { "true" } else { "false" }
    $mode = if ($MemoryMode -eq "fast") { "fast" } else { "accurate" }

    $content = @"
import runpy
import unreal

editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = editor.get_editor_world() if editor else None
if not world:
    raise SystemExit(2)

unreal.SystemLibrary.execute_console_command(world, "PCGProfiler.SetSamplingEnabled $sampling")
unreal.SystemLibrary.execute_console_command(world, "PCGProfiler.SetMemoryMode $mode")

for _ in range($PrewarmCount):
    runpy.run_path("F:/Unreal Projects/ElectricDreamsEnv/Plugins/PCGProfiler/Scripts/pcg_start_run.py", run_name="__main__")
    runpy.run_path("F:/Unreal Projects/ElectricDreamsEnv/Plugins/PCGProfiler/Scripts/pcg_wait_until_idle.py", run_name="__main__")
    runpy.run_path("F:/Unreal Projects/ElectricDreamsEnv/Plugins/PCGProfiler/Scripts/pcg_finish_export.py", run_name="__main__")

runpy.run_path("F:/Unreal Projects/ElectricDreamsEnv/Plugins/PCGProfiler/Scripts/pcg_start_run.py", run_name="__main__")
runpy.run_path("F:/Unreal Projects/ElectricDreamsEnv/Plugins/PCGProfiler/Scripts/pcg_wait_until_idle.py", run_name="__main__")
runpy.run_path("F:/Unreal Projects/ElectricDreamsEnv/Plugins/PCGProfiler/Scripts/pcg_finish_export.py", run_name="__main__")

unreal.SystemLibrary.execute_console_command(world, "QUIT_EDITOR")
"@

    Set-Content -LiteralPath $WrapperPath -Value $content -Encoding utf8
}

function Invoke-CaseRun(
    [string]$CaseName,
    [bool]$SamplingEnabled,
    [string]$MemoryMode,
    [int]$PrewarmCount,
    [int]$Iteration
) {
    $projectDir = Split-Path -Parent $ProjectPath
    $profileDir = Join-Path $projectDir "Saved\Profiling\PCG"
    New-Item -ItemType Directory -Force -Path $profileDir | Out-Null

    $before = Get-ChildItem -LiteralPath $profileDir -Filter "PCGProfiler_*.json" -File -ErrorAction SilentlyContinue |
        Select-Object -ExpandProperty FullName

    $wrapperPath = Join-Path $EngineRoot ("Saved\pcg_case_wrapper_{0}_{1}.py" -f $CaseName, $Iteration)
    New-CaseWrapper -WrapperPath $wrapperPath -SamplingEnabled $SamplingEnabled -MemoryMode $MemoryMode -PrewarmCount $PrewarmCount

    $editorCmd = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
    $plugin = Join-Path (Split-Path -Parent $ProjectPath) "Plugins\PCGProfiler\PCGProfiler.uplugin"
    $log = Join-Path $profileDir ("case_{0}_{1}.log" -f $CaseName, $Iteration)

    $args = @(
        $ProjectPath,
        $Map,
        "-PLUGIN=$plugin",
        "-Unattended",
        "-NoSplash",
        "-ExecutePythonScript=$wrapperPath",
        "-AbsLog=$log"
    )
    if ($UseNullRHI) { $args += "-NullRHI" }

    & $editorCmd @args | Out-Null

    $after = Get-ChildItem -LiteralPath $profileDir -Filter "PCGProfiler_*.json" -File -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending
    $latestNew = $after | Where-Object { $_.FullName -notin $before } | Select-Object -First 1
    if (-not $latestNew) {
        throw "No new JSON for case=$CaseName iteration=$Iteration"
    }

    $obj = Get-Content -LiteralPath $latestNew.FullName -Raw | ConvertFrom-Json
    $gt = Get-ThreadValue -JsonObj $obj -name "game_thread_total_ms"
    $worker = Get-ThreadValue -JsonObj $obj -name "worker_total_ms"

    return [pscustomobject]@{
        case = $CaseName
        iteration = $Iteration
        json_path = $latestNew.FullName
        run_duration_ms = [double]$obj.run_duration_ms
        top_node_p95_ms = [double](Get-TopNodeP95 -JsonObj $obj)
        cpu_total_ms = [double]($gt + $worker)
        cpu_gt_ratio = [double](Get-ThreadValue -JsonObj $obj -name "game_thread_ratio")
        cache_hit_rate = [double](Get-CacheHitRate -JsonObj $obj)
        node_count = [int]$obj.node_count
    }
}

function Get-Stats([double[]]$vals) {
    if (-not $vals -or $vals.Count -eq 0) {
        return [ordered]@{ mean = 0.0; p95 = 0.0; min = 0.0; max = 0.0 }
    }
    $sorted = $vals | Sort-Object
    $count = $sorted.Count
    $idx95 = [int][Math]::Floor(0.95 * ($count - 1))
    return [ordered]@{
        mean = [double](($sorted | Measure-Object -Average).Average)
        p95 = [double]$sorted[$idx95]
        min = [double]$sorted[0]
        max = [double]$sorted[$count - 1]
    }
}

$cases = @(
    [pscustomobject]@{ name = "baseline"; sampling = $true; memory_mode = "accurate"; prewarm = 0 },
    [pscustomobject]@{ name = "warm_cache"; sampling = $true; memory_mode = "accurate"; prewarm = 1 },
    [pscustomobject]@{ name = "warm_cache_fastmem"; sampling = $true; memory_mode = "fast"; prewarm = 1 }
)

$all = @()
foreach ($c in $cases) {
    for ($i = 1; $i -le [Math]::Max(1, $IterationsPerCase); $i++) {
        Write-Host "Running case=$($c.name) iter=$i ..."
        $all += Invoke-CaseRun -CaseName $c.name -SamplingEnabled $c.sampling -MemoryMode $c.memory_mode -PrewarmCount $c.prewarm -Iteration $i
    }
}

$summary = @()
foreach ($name in ($cases.name)) {
    $rows = @($all | Where-Object { $_.case -eq $name })
    $summary += [pscustomobject]@{
        case = $name
        run_duration = Get-Stats -vals @($rows | ForEach-Object { $_.run_duration_ms })
        top_node_p95 = Get-Stats -vals @($rows | ForEach-Object { $_.top_node_p95_ms })
        cpu_total = Get-Stats -vals @($rows | ForEach-Object { $_.cpu_total_ms })
        cpu_gt_ratio = Get-Stats -vals @($rows | ForEach-Object { $_.cpu_gt_ratio })
        cache_hit_rate = Get-Stats -vals @($rows | ForEach-Object { $_.cache_hit_rate })
    }
}

$baseline = $summary | Where-Object { $_.case -eq "baseline" } | Select-Object -First 1
$compare = @()
foreach ($row in $summary) {
    if ($row.case -eq "baseline") { continue }
    $compare += [pscustomobject]@{
        case = $row.case
        run_duration_mean_delta_pct = if ($baseline.run_duration.mean -ne 0) { (($row.run_duration.mean - $baseline.run_duration.mean) / $baseline.run_duration.mean) * 100.0 } else { 0.0 }
        top_node_p95_mean_delta_pct = if ($baseline.top_node_p95.mean -ne 0) { (($row.top_node_p95.mean - $baseline.top_node_p95.mean) / $baseline.top_node_p95.mean) * 100.0 } else { 0.0 }
        cpu_total_mean_delta_pct = if ($baseline.cpu_total.mean -ne 0) { (($row.cpu_total.mean - $baseline.cpu_total.mean) / $baseline.cpu_total.mean) * 100.0 } else { 0.0 }
    }
}

$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$outDir = Join-Path (Join-Path (Split-Path -Parent $ProjectPath) "Saved\Profiling\PCG\CaseStudies") $stamp
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$payload = [ordered]@{
    project = $ProjectPath
    map = $Map
    iterations_per_case = $IterationsPerCase
    use_null_rhi = [bool]$UseNullRHI
    cases = $cases
    runs = $all
    summary = $summary
    compare_vs_baseline = $compare
}

$jsonPath = Join-Path $outDir "case_study.json"
$mdPath = Join-Path $outDir "case_study.md"
$payload | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $jsonPath -Encoding utf8

$lines = @()
$lines += "# PCG Profiler Case Study"
$lines += ""
$lines += "- Map: $Map"
$lines += "- Iterations per case: $IterationsPerCase"
$lines += "- NullRHI: $UseNullRHI"
$lines += ""
$lines += "## Summary (mean)"
foreach ($s in $summary) {
    $lines += "- $($s.case): run=$([Math]::Round($s.run_duration.mean,2)) ms, p95=$([Math]::Round($s.top_node_p95.mean,2)) ms, cpu_total=$([Math]::Round($s.cpu_total.mean,2)) ms, gt_ratio=$([Math]::Round($s.cpu_gt_ratio.mean*100,2))%, cache_hit=$([Math]::Round($s.cache_hit_rate.mean*100,2))%"
}
$lines += ""
$lines += "## Compare vs baseline (delta %)"
foreach ($c in $compare) {
    $lines += "- $($c.case): run_mean=$([Math]::Round($c.run_duration_mean_delta_pct,2))%, top_p95_mean=$([Math]::Round($c.top_node_p95_mean_delta_pct,2))%, cpu_total_mean=$([Math]::Round($c.cpu_total_mean_delta_pct,2))%"
}

Set-Content -LiteralPath $mdPath -Value ($lines -join "`r`n") -Encoding utf8

Write-Host "Case study json: $jsonPath"
Write-Host "Case study md:   $mdPath"
