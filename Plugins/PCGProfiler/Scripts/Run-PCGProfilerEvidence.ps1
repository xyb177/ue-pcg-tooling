param(
    [string]$ProjectRoot = "",
    [string]$InputJsonA = "",
    [string]$InputJsonB = "",
    [string]$OutputDir = ""
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$bAIsCurrentWhenBMissing = $false

if (-not $ProjectRoot) {
    $cwd = (Get-Location).Path
    if (Test-Path -LiteralPath (Join-Path $cwd "Saved\Profiling\PCG")) {
        $ProjectRoot = $cwd
    } else {
        throw "ProjectRoot is empty and could not be inferred from current working directory. Pass -ProjectRoot explicitly."
    }
}

function Resolve-LatestJsonFiles {
    param([string]$RootDir)

    $profileDir = Join-Path $RootDir "Saved\Profiling\PCG"
    if (-not (Test-Path -LiteralPath $profileDir)) {
        throw "Cannot find profiling directory: $profileDir"
    }

    $files = Get-ChildItem -LiteralPath $profileDir -Filter "*.json" -File |
        Sort-Object LastWriteTime -Descending

    if ($files.Count -lt 2) {
        throw "Need at least two JSON files in $profileDir"
    }

    return @($files[0].FullName, $files[1].FullName)
}

function Test-JsonSampleValid([object]$Json) {
    if ($null -eq $Json) { return $false }
    if (-not [bool]$Json.sampling_enabled) { return $false }
    $nodeCount = [int]($Json.node_count)
    $eventsTotal = [int]($Json.events_total_seen)
    $runDuration = [double]($Json.run_duration_ms)
    if ($runDuration -le 0.0) { return $false }
    if ($nodeCount -le 0 -and $eventsTotal -le 0) { return $false }
    return $true
}

function Resolve-BaseJsonForCurrent {
    param(
        [string]$RootDir,
        [string]$CurrentJson
    )

    $profileDir = Join-Path $RootDir "Saved\Profiling\PCG"
    if (-not (Test-Path -LiteralPath $profileDir)) {
        throw "Cannot find profiling directory: $profileDir"
    }
    if (-not (Test-Path -LiteralPath $CurrentJson)) {
        throw "Current JSON not found: $CurrentJson"
    }

    $currentTs = (Get-Item -LiteralPath $CurrentJson).LastWriteTimeUtc
    $candidates = Get-ChildItem -LiteralPath $profileDir -Filter "*.json" -File |
        Where-Object { $_.FullName -ne $CurrentJson -and $_.LastWriteTimeUtc -le $currentTs } |
        Sort-Object LastWriteTimeUtc -Descending

    foreach ($file in $candidates) {
        try {
            $obj = Get-JsonObject -Path $file.FullName
            if (Test-JsonSampleValid $obj) {
                return $file.FullName
            }
        } catch {
            # Skip malformed/legacy JSON files and continue searching older candidates.
            continue
        }
    }

    throw "No valid base JSON found before current: $CurrentJson"
}

function Get-JsonObject([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Missing JSON file: $Path"
    }

    return Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
}

function Get-PercentDelta([double]$Base, [double]$Current) {
    if ($Base -eq 0) { return $null }
    return (($Current - $Base) / $Base) * 100.0
}

function Get-TopP95([object]$Json) {
    $values = @()
    foreach ($node in @($Json.nodes)) {
        if ($null -ne $node.p95_ms) {
            $values += [double]$node.p95_ms
        }
    }

    if ($values.Count -eq 0) {
        return 0.0
    }

    return (($values | Measure-Object -Maximum).Maximum)
}

function Get-ThreadMetric([object]$ThreadLoad, [string]$Name) {
    if ($null -eq $ThreadLoad) { return 0.0 }
    $member = $ThreadLoad.PSObject.Properties[$Name]
    if ($null -eq $member) { return 0.0 }
    $value = $member.Value
    if ($Name -eq "parallel_efficiency" -and $null -ne $value) {
        $score = $value.PSObject.Properties["score"]
        if ($null -ne $score) {
            return [double]$score.Value
        }
    }
    return [double]$value
}

function Get-CacheHitRate([object]$Json) {
    if ($null -eq $Json.cache_analysis) { return 0.0 }
    $member = $Json.cache_analysis.PSObject.Properties["graph_cache_hit_rate"]
    if ($null -eq $member) { return 0.0 }
    return [double]$member.Value
}

if ([string]::IsNullOrWhiteSpace($InputJsonA) -or [string]::IsNullOrWhiteSpace($InputJsonB)) {
    $latest = Resolve-LatestJsonFiles -RootDir $ProjectRoot
    if ([string]::IsNullOrWhiteSpace($InputJsonA)) { $InputJsonA = $latest[0] }
    if ([string]::IsNullOrWhiteSpace($InputJsonB)) {
        $InputJsonB = Resolve-BaseJsonForCurrent -RootDir $ProjectRoot -CurrentJson $InputJsonA
        $bAIsCurrentWhenBMissing = $true
    }
} elseif ([string]::IsNullOrWhiteSpace($InputJsonB)) {
    $InputJsonB = Resolve-BaseJsonForCurrent -RootDir $ProjectRoot -CurrentJson $InputJsonA
    $bAIsCurrentWhenBMissing = $true
}

if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $stamp = Get-Date -Format "yyyyMMdd_HHmmss"
    $OutputDir = Join-Path (Join-Path $ProjectRoot "Saved\Profiling\PCG\Evidence") $stamp
}

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

$basePath = $InputJsonA
$currentPath = $InputJsonB
if ($bAIsCurrentWhenBMissing) {
    $basePath = $InputJsonB
    $currentPath = $InputJsonA
}

$base = Get-JsonObject -Path $basePath
$current = Get-JsonObject -Path $currentPath
if (-not (Test-JsonSampleValid $base)) {
    throw "Base JSON is not a valid sample for evidence: $basePath"
}
if (-not (Test-JsonSampleValid $current)) {
    throw "Current JSON is not a valid sample for evidence: $currentPath"
}

$baseTopP95 = Get-TopP95 -Json $base
$currentTopP95 = Get-TopP95 -Json $current
$baseThread = $base.thread_load
$currentThread = $current.thread_load
$baseGTRatio = Get-ThreadMetric -ThreadLoad $baseThread -Name "game_thread_ratio"
$currentGTRatio = Get-ThreadMetric -ThreadLoad $currentThread -Name "game_thread_ratio"
$baseCacheHitRate = Get-CacheHitRate -Json $base
$currentCacheHitRate = Get-CacheHitRate -Json $current

$summary = [ordered]@{
    base_json = $basePath
    current_json = $currentPath
    base_sampling_enabled = [bool]$base.sampling_enabled
    current_sampling_enabled = [bool]$current.sampling_enabled
    run_duration_ms = [ordered]@{
        base = [double]$base.run_duration_ms
        current = [double]$current.run_duration_ms
        delta = [double]$current.run_duration_ms - [double]$base.run_duration_ms
        delta_pct = (Get-PercentDelta -Base ([double]$base.run_duration_ms) -Current ([double]$current.run_duration_ms))
    }
    node_count = [ordered]@{
        base = [int]$base.node_count
        current = [int]$current.node_count
        delta = [int]$current.node_count - [int]$base.node_count
        delta_pct = (Get-PercentDelta -Base ([double]$base.node_count) -Current ([double]$current.node_count))
    }
    top_node_p95_ms = [ordered]@{
        base = [double]$baseTopP95
        current = [double]$currentTopP95
        delta = [double]$currentTopP95 - [double]$baseTopP95
        delta_pct = (Get-PercentDelta -Base ([double]$baseTopP95) -Current ([double]$currentTopP95))
    }
    game_thread_total_ms = [ordered]@{
        base = (Get-ThreadMetric -ThreadLoad $baseThread -Name "game_thread_total_ms")
        current = (Get-ThreadMetric -ThreadLoad $currentThread -Name "game_thread_total_ms")
        delta = (Get-ThreadMetric -ThreadLoad $currentThread -Name "game_thread_total_ms") - (Get-ThreadMetric -ThreadLoad $baseThread -Name "game_thread_total_ms")
        delta_pct = (Get-PercentDelta -Base (Get-ThreadMetric -ThreadLoad $baseThread -Name "game_thread_total_ms") -Current (Get-ThreadMetric -ThreadLoad $currentThread -Name "game_thread_total_ms"))
    }
    worker_total_ms = [ordered]@{
        base = (Get-ThreadMetric -ThreadLoad $baseThread -Name "worker_total_ms")
        current = (Get-ThreadMetric -ThreadLoad $currentThread -Name "worker_total_ms")
        delta = (Get-ThreadMetric -ThreadLoad $currentThread -Name "worker_total_ms") - (Get-ThreadMetric -ThreadLoad $baseThread -Name "worker_total_ms")
        delta_pct = (Get-PercentDelta -Base (Get-ThreadMetric -ThreadLoad $baseThread -Name "worker_total_ms") -Current (Get-ThreadMetric -ThreadLoad $currentThread -Name "worker_total_ms"))
    }
    parallel_efficiency = [ordered]@{
        base = (Get-ThreadMetric -ThreadLoad $baseThread -Name "parallel_efficiency")
        current = (Get-ThreadMetric -ThreadLoad $currentThread -Name "parallel_efficiency")
        delta = (Get-ThreadMetric -ThreadLoad $currentThread -Name "parallel_efficiency") - (Get-ThreadMetric -ThreadLoad $baseThread -Name "parallel_efficiency")
        delta_pct = (Get-PercentDelta -Base (Get-ThreadMetric -ThreadLoad $baseThread -Name "parallel_efficiency") -Current (Get-ThreadMetric -ThreadLoad $currentThread -Name "parallel_efficiency"))
    }
    game_thread_ratio = [ordered]@{
        base = [double]$baseGTRatio
        current = [double]$currentGTRatio
        delta = [double]$currentGTRatio - [double]$baseGTRatio
        delta_pct = (Get-PercentDelta -Base ([double]$baseGTRatio) -Current ([double]$currentGTRatio))
    }
    cache_hit_rate = [ordered]@{
        base = [double]$baseCacheHitRate
        current = [double]$currentCacheHitRate
        delta = [double]$currentCacheHitRate - [double]$baseCacheHitRate
        delta_pct = (Get-PercentDelta -Base ([double]$baseCacheHitRate) -Current ([double]$currentCacheHitRate))
    }
}

$report = @"
# PCG Profiler Evidence Report

## Inputs
- Base JSON: $basePath
- Current JSON: $currentPath

## Key Evidence
- Sampling enabled (base/current): $($summary.base_sampling_enabled) / $($summary.current_sampling_enabled)
- Run duration ms: $([math]::Round($summary.run_duration_ms.base, 3)) -> $([math]::Round($summary.run_duration_ms.current, 3)) ($([math]::Round($summary.run_duration_ms.delta_pct, 2))%)
- Node count: $($summary.node_count.base) -> $($summary.node_count.current) ($([math]::Round($summary.node_count.delta_pct, 2))%)
- Top node p95 ms: $([math]::Round($summary.top_node_p95_ms.base, 3)) -> $([math]::Round($summary.top_node_p95_ms.current, 3)) ($([math]::Round($summary.top_node_p95_ms.delta_pct, 2))%)
- Game thread total ms: $([math]::Round($summary.game_thread_total_ms.base, 3)) -> $([math]::Round($summary.game_thread_total_ms.current, 3)) ($([math]::Round($summary.game_thread_total_ms.delta_pct, 2))%)
- Worker total ms: $([math]::Round($summary.worker_total_ms.base, 3)) -> $([math]::Round($summary.worker_total_ms.current, 3)) ($([math]::Round($summary.worker_total_ms.delta_pct, 2))%)
- Parallel efficiency: $([math]::Round($summary.parallel_efficiency.base, 3)) -> $([math]::Round($summary.parallel_efficiency.current, 3)) ($([math]::Round($summary.parallel_efficiency.delta_pct, 2))%)
- GT ratio: $([math]::Round($summary.game_thread_ratio.base * 100.0, 2))% -> $([math]::Round($summary.game_thread_ratio.current * 100.0, 2))% ($([math]::Round($summary.game_thread_ratio.delta_pct, 2))%)
- Cache hit rate: $([math]::Round($summary.cache_hit_rate.base * 100.0, 2))% -> $([math]::Round($summary.cache_hit_rate.current * 100.0, 2))% ($([math]::Round($summary.cache_hit_rate.delta_pct, 2))%)

## Interpretation
- Use the JSON pair together with an Unreal Insights capture for the same runs to show trace scopes in the engine profiler.
- The `PCGProfiler_*` trace scopes are emitted from the plugin and can be searched in Insights once a trace session is recorded.
"@

$summaryPath = Join-Path $OutputDir "evidence_summary.json"
$reportPath = Join-Path $OutputDir "evidence_report.md"

$summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $summaryPath -Encoding utf8
$report | Set-Content -LiteralPath $reportPath -Encoding utf8

Write-Host "Evidence summary: $summaryPath"
Write-Host "Evidence report:  $reportPath"
Write-Host "Base JSON:        $basePath"
Write-Host "Current JSON:     $currentPath"
