param(
    [string]$EngineRoot = "",
    [string]$ProjectPath = "",
    [string[]]$Maps = @(
        "/Game/Levels/PCG/ElectricDreams_PCG",
        "/Game/Levels/PCG/ElectricDreams_PCGCloseRange"
    ),
    [int]$IterationsPerMap = 5,
    [switch]$UseNullRHI = $true,
    [int]$TimeoutPerRunMinutes = 12,
    [int]$RetryCount = 1,
    [ValidateSet("custom", "quick", "full")]
    [string]$Preset = "custom",
    [string]$BaselineSummary = "",
    [switch]$CleanupResidualEditorCmd = $true
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "PCGProfiler.ScriptCommon.ps1")
$EngineRoot = Resolve-PCGProfilerEngineRoot -PreferredEngineRoot $EngineRoot
$ProjectPath = Resolve-PCGProfilerProjectPath -PreferredProjectPath $ProjectPath

function Get-Stats([double[]]$Values) {
    if (-not $Values -or $Values.Count -eq 0) {
        return [ordered]@{ mean = 0; min = 0; max = 0; p50 = 0; p95 = 0; stddev = 0; cv = 0 }
    }

    $sorted = $Values | Sort-Object
    $count = $sorted.Count
    $mean = ($sorted | Measure-Object -Average).Average
    $min = $sorted[0]
    $max = $sorted[$count - 1]
    $idx50 = [Math]::Floor(0.50 * ($count - 1))
    $idx95 = [Math]::Floor(0.95 * ($count - 1))
    $p50 = $sorted[[int]$idx50]
    $p95 = $sorted[[int]$idx95]

    $sumSq = 0.0
    foreach ($v in $sorted) { $sumSq += [Math]::Pow(($v - $mean), 2) }
    $stddev = [Math]::Sqrt($sumSq / $count)
    $cv = if ($mean -gt 0) { $stddev / $mean } else { 0 }

    return [ordered]@{ mean = $mean; min = $min; max = $max; p50 = $p50; p95 = $p95; stddev = $stddev; cv = $cv }
}

function Get-FailureCategory([string]$Message) {
    $m = ""
    if ($null -ne $Message) { $m = [string]$Message }
    $m = $m.ToLowerInvariant()

    if ($m.Contains("timeout")) { return "timeout" }
    if ($m.Contains("exitcode")) { return "command_failed" }
    if ($m.Contains("no new") -or $m.Contains("pcgprofiler json")) { return "no_export_file" }
    if ($m.Contains("convertfrom-json") -or $m.Contains("json")) { return "json_parse_failed" }
    return "unknown"
}

function Cleanup-ResidualEditorCmd {
    param([string]$ProjectPath)

    $killed = 0
    $projectLower = $ProjectPath.ToLowerInvariant()
    $procs = Get-CimInstance Win32_Process -Filter "Name='UnrealEditor-Cmd.exe'" -ErrorAction SilentlyContinue
    foreach ($p in $procs) {
        $cmd = [string]$p.CommandLine
        if (-not $cmd) { continue }
        if ($cmd.ToLowerInvariant().Contains($projectLower)) {
            try {
                Stop-Process -Id $p.ProcessId -Force -ErrorAction Stop
                $killed++
            }
            catch {}
        }
    }
    return $killed
}

function Invoke-OneRun {
    param(
        [string]$EngineRoot,
        [string]$EditorCmd,
        [string]$ProjectPath,
        [string]$Map,
        [string]$PluginPath,
        [bool]$UseNullRHI,
        [string]$LogPath,
        [int]$TimeoutMinutes
    )

    $profileDir = Join-Path (Join-Path (Split-Path -Parent $ProjectPath) "Saved") "Profiling\PCG"
    New-Item -ItemType Directory -Force -Path $profileDir | Out-Null

    $before = @{}
    Get-ChildItem $profileDir -Recurse -Filter "*.json" -ErrorAction SilentlyContinue | ForEach-Object { $before[$_.FullName] = $true }

    $startPy = Join-Path $EngineRoot "Plugins\PCGProfiler\Scripts\pcg_start_run.py"
    $waitPy = Join-Path $EngineRoot "Plugins\PCGProfiler\Scripts\pcg_wait_until_idle.py"
    $finishPy = Join-Path $EngineRoot "Plugins\PCGProfiler\Scripts\pcg_finish_export.py"
    $execCmds = "py `"$startPy`",py `"$waitPy`",py `"$finishPy`",Quit"

    $args = @(
        "`"$ProjectPath`"",
        "`"$Map`"",
        "-PLUGIN=`"$PluginPath`"",
        "-Unattended",
        "-NoSplash",
        "-ExecCmds=`"$execCmds`"",
        "-AbsLog=`"$LogPath`""
    )
    if ($UseNullRHI) { $args += "-NullRHI" }

    $proc = Start-Process -FilePath $EditorCmd -ArgumentList $args -PassThru -NoNewWindow
    $deadline = (Get-Date).AddMinutes($TimeoutMinutes)
    $exportedPathFromLog = $null

    while (-not $proc.HasExited) {
        Start-Sleep -Seconds 2

        if ((Test-Path $LogPath) -and -not $exportedPathFromLog) {
            $tail = Get-Content -Path $LogPath -Tail 80 -ErrorAction SilentlyContinue
            foreach ($line in $tail) {
                if ($line -match "PCGProfiler JSON exported:\s*(.+)$") {
                    $candidate = $matches[1].Trim()
                    if ($candidate -and -not [System.IO.Path]::IsPathRooted($candidate)) {
                        $candidate = [System.IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $ProjectPath) $candidate))
                    } else {
                        $candidate = [System.IO.Path]::GetFullPath($candidate)
                    }
                    if (Test-Path -LiteralPath $candidate) {
                        $exportedPathFromLog = $candidate
                        break
                    }
                }
            }
        }

        if ($exportedPathFromLog) {
            try { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue } catch {}
            break
        }

        if ((Get-Date) -ge $deadline) {
            try { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue } catch {}
            throw "Single run timeout (${TimeoutMinutes} min), process terminated. PID=$($proc.Id)"
        }
    }

    if (-not $exportedPathFromLog -and $proc.ExitCode -ne 0) {
        throw "UnrealEditor-Cmd exited with non-zero code: $($proc.ExitCode)"
    }

    if ($exportedPathFromLog) { return $exportedPathFromLog }

    $after = Get-ChildItem $profileDir -Recurse -Filter "*.json" -ErrorAction SilentlyContinue |
        Where-Object { -not $before.ContainsKey($_.FullName) } |
        Sort-Object LastWriteTime -Descending

    if (-not $after -or $after.Count -eq 0) {
        throw "No new PCGProfiler JSON generated for this run."
    }

    foreach ($candidate in $after) {
        try {
            $obj = Get-Content -Path $candidate.FullName -Raw | ConvertFrom-Json
            if ($null -ne $obj.run_duration_ms -and $null -ne $obj.thread_load) {
                return $candidate.FullName
            }
        }
        catch {}
    }

    throw "New JSON found but missing required fields: run_duration_ms/thread_load."
}

$effectiveIterations = $IterationsPerMap
$effectiveTimeout = $TimeoutPerRunMinutes
$effectiveRetry = $RetryCount
$effectiveUseNullRHI = [bool]$UseNullRHI

switch ($Preset) {
    "quick" {
        $effectiveIterations = 2
        $effectiveTimeout = 8
        $effectiveRetry = 0
        $effectiveUseNullRHI = $true
    }
    "full" {
        $effectiveIterations = [Math]::Max($IterationsPerMap, 5)
        $effectiveTimeout = [Math]::Max($TimeoutPerRunMinutes, 12)
        $effectiveRetry = [Math]::Max($RetryCount, 1)
    }
    default {}
}

$editorCmd = Get-PCGProfilerEditorCmdPath -EngineRoot $EngineRoot
$pluginPath = Get-PCGProfilerPluginUpluginPath -EngineRoot $EngineRoot
$projectDir = Split-Path -Parent $ProjectPath
$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$totalRuns = $Maps.Count * $effectiveIterations
$currentRun = 0

$reportDir = Join-Path $EngineRoot "Plugins\PCGProfiler\Reports"
New-Item -ItemType Directory -Force -Path $reportDir | Out-Null

$projectSaved = Join-Path $projectDir "Saved"
$projectBenchmarkDir = Join-Path $projectSaved "Profiling\PCG\Benchmark\$timestamp"
New-Item -ItemType Directory -Force -Path $projectBenchmarkDir | Out-Null

if ($CleanupResidualEditorCmd) {
    $killedCount = Cleanup-ResidualEditorCmd -ProjectPath $ProjectPath
    if ($killedCount -gt 0) {
        Write-Host "Cleaned residual UnrealEditor-Cmd processes: $killedCount"
    }
}

$results = @()
$failures = @()

foreach ($map in $Maps) {
    $mapSafe = $map.Replace("/", "_").Replace(":", "_")

    for ($i = 1; $i -le $effectiveIterations; $i++) {
        $currentRun++
        $progress = [int](($currentRun / [double]$totalRuns) * 100)
        Write-Progress -Id 1 -Activity "PCG Profiler Benchmark" -Status "Map $map (iter $i/$effectiveIterations, total $currentRun/$totalRuns)" -PercentComplete $progress
        Write-Host "[$currentRun/$totalRuns] Start: $map (iter $i)"

        $logAbs = Join-Path $projectBenchmarkDir ("run_{0}_{1}.log" -f $mapSafe, $i)
        $attempt = 0
        $jsonPath = $null

        while ($attempt -le $effectiveRetry -and -not $jsonPath) {
            try {
                $attempt++
                if ($attempt -gt 1) {
                    Write-Host "  Retry attempt $attempt..."
                }

                $jsonPath = Invoke-OneRun -EngineRoot $EngineRoot -EditorCmd $editorCmd -ProjectPath $ProjectPath -Map $map -PluginPath $pluginPath -UseNullRHI:$effectiveUseNullRHI -LogPath $logAbs -TimeoutMinutes $effectiveTimeout
                $json = Get-Content -Path $jsonPath -Raw | ConvertFrom-Json
                $threadLoad = $json.thread_load
                $topNodeP95Ms = 0.0
                if ($json.nodes) {
                    $p95Candidates = @($json.nodes | ForEach-Object { if ($null -ne $_.p95_ms) { [double]$_.p95_ms } else { 0.0 } })
                    if ($p95Candidates.Count -gt 0) { $topNodeP95Ms = ($p95Candidates | Measure-Object -Maximum).Maximum }
                }

                $results += [pscustomobject]@{
                    map = $map
                    iteration = $i
                    json_path = $jsonPath
                    run_duration_ms = [double]$json.run_duration_ms
                    node_count = [int]$json.node_count
                    game_thread_total_ms = [double]$threadLoad.game_thread_total_ms
                    worker_total_ms = [double]$threadLoad.worker_total_ms
                    parallel_efficiency_score = [double]$threadLoad.parallel_efficiency.score
                    top_node_p95_ms = [double]$topNodeP95Ms
                    attempt = $attempt
                }
            }
            catch {
                $msg = $_.Exception.Message
                if ($attempt -gt $effectiveRetry) {
                    Write-Host "  Failed: $msg"
                    $failures += [pscustomobject]@{
                        map = $map
                        iteration = $i
                        error = $msg
                        category = Get-FailureCategory -Message $msg
                        log = $logAbs
                    }
                }
            }
        }
    }
}
Write-Progress -Id 1 -Activity "PCG Profiler Benchmark" -Completed

$summary = @()
foreach ($group in ($results | Group-Object map)) {
    $validSamples = @($group.Group | Where-Object { $_.node_count -gt 0 })
    $hasValidNodeData = $validSamples.Count -gt 0
    $durStats = Get-Stats (($group.Group | Select-Object -ExpandProperty run_duration_ms))
    $gtStats = Get-Stats (($group.Group | Select-Object -ExpandProperty game_thread_total_ms))
    $wkStats = Get-Stats (($group.Group | Select-Object -ExpandProperty worker_total_ms))
    $peStats = Get-Stats (($group.Group | Select-Object -ExpandProperty parallel_efficiency_score))
    $topNodeP95Stats = Get-Stats (($group.Group | Select-Object -ExpandProperty top_node_p95_ms))

    $durationStable = $durStats.cv -le 0.20
    $gtStable = $hasValidNodeData -and ($gtStats.cv -le 0.25)
    $wkStable = $hasValidNodeData -and ($wkStats.cv -le 0.25)
    $topNodeP95Stable = $hasValidNodeData -and ($topNodeP95Stats.cv -le 0.30)

    $summary += [pscustomobject]@{
        map = $group.Name
        sample_count = $group.Count
        valid_node_samples = $validSamples.Count
        has_valid_node_data = $hasValidNodeData
        duration_mean_ms = [Math]::Round($durStats.mean, 3)
        duration_p50_ms = [Math]::Round($durStats.p50, 3)
        duration_p95_ms = [Math]::Round($durStats.p95, 3)
        duration_cv = [Math]::Round($durStats.cv, 4)
        gt_mean_ms = [Math]::Round($gtStats.mean, 3)
        gt_cv = [Math]::Round($gtStats.cv, 4)
        worker_mean_ms = [Math]::Round($wkStats.mean, 3)
        worker_cv = [Math]::Round($wkStats.cv, 4)
        parallel_efficiency_mean = [Math]::Round($peStats.mean, 3)
        top_node_p95_mean_ms = [Math]::Round($topNodeP95Stats.mean, 3)
        top_node_p95_cv = [Math]::Round($topNodeP95Stats.cv, 4)
        stable_duration = $durationStable
        stable_gt = $gtStable
        stable_worker = $wkStable
        stable_top_node_p95 = $topNodeP95Stable
        stable_overall = ($durationStable -and $gtStable -and $wkStable -and $topNodeP95Stable)
    }
}

$payload = [pscustomobject]@{
    generated_at = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss")
    project = $ProjectPath
    maps = $Maps
    preset = $Preset
    iterations_per_map = $effectiveIterations
    timeout_minutes = $effectiveTimeout
    retry_count = $effectiveRetry
    null_rhi = $effectiveUseNullRHI
    results = $results
    failures = $failures
    summary = $summary
    baseline_compare = $null
}

if ($BaselineSummary -and (Test-Path -LiteralPath $BaselineSummary)) {
    try {
        $baseline = Get-Content -Path $BaselineSummary -Raw | ConvertFrom-Json
        $comparisons = @()
        foreach ($s in $summary) {
            $base = @($baseline.summary | Where-Object { $_.map -eq $s.map }) | Select-Object -First 1
            if ($null -eq $base) { continue }
            $comparisons += [pscustomobject]@{
                map = $s.map
                duration_regression_pct = if ([double]$base.duration_mean_ms -gt 0) { [Math]::Round((([double]$s.duration_mean_ms - [double]$base.duration_mean_ms) / [double]$base.duration_mean_ms) * 100.0, 2) } else { 0 }
                top_node_p95_regression_pct = if ([double]$base.top_node_p95_mean_ms -gt 0) { [Math]::Round((([double]$s.top_node_p95_mean_ms - [double]$base.top_node_p95_mean_ms) / [double]$base.top_node_p95_mean_ms) * 100.0, 2) } else { 0 }
            }
        }
        $payload.baseline_compare = $comparisons
    }
    catch {
        Write-Warning "Failed to parse baseline summary: $BaselineSummary"
    }
}

$jsonOut = Join-Path $reportDir ("pcg_benchmark_summary_{0}.json" -f $timestamp)
$mdOut = Join-Path $reportDir ("pcg_benchmark_summary_{0}.md" -f $timestamp)

$payload | ConvertTo-Json -Depth 12 | Set-Content -Path $jsonOut -Encoding UTF8

$lines = @()
$lines += "# PCG Profiler Benchmark"
$lines += ""
$lines += "- Generated: $((Get-Date).ToString('yyyy-MM-dd HH:mm:ss'))"
$lines += "- Project: $ProjectPath"
$lines += "- Preset: $Preset"
$lines += "- Iterations/Map: $effectiveIterations"
$lines += ""
$lines += "| Map | Samples | Valid Node Samples | Duration Mean(ms) | Duration CV | GT CV | Worker CV | TopNodeP95 CV | Stable |"
$lines += "|---|---:|---:|---:|---:|---:|---:|---:|---|"
foreach ($s in $summary) {
    $lines += "| $($s.map) | $($s.sample_count) | $($s.valid_node_samples) | $($s.duration_mean_ms) | $($s.duration_cv) | $($s.gt_cv) | $($s.worker_cv) | $($s.top_node_p95_cv) | $($s.stable_overall) |"
}
$lines += ""
$lines += "Failures: $($failures.Count)"
Set-Content -Path $mdOut -Value ($lines -join "`r`n") -Encoding UTF8

Write-Output "SUMMARY_JSON=$jsonOut"
Write-Output "SUMMARY_MD=$mdOut"
Write-Output "RUNS_OK=$($results.Count)"
Write-Output "RUNS_FAILED=$($failures.Count)"
