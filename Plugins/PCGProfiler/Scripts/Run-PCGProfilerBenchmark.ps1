param(
    [string]$EngineRoot = "F:\UnrealEngine-5.7.3-release",
    [string]$ProjectPath = "F:\Unreal Projects\ElectricDreamsEnv\ElectricDreamsEnv.uproject",
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

$ErrorActionPreference = "Stop"

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
    $m = ($Message ?? "").ToLowerInvariant()
    if ($m.Contains("超时") -or $m.Contains("timeout")) { return "timeout" }
    if ($m.Contains("退出码") -or $m.Contains("exitcode")) { return "command_failed" }
    if ($m.Contains("未检测到本轮新导出的") -or $m.Contains("no new") -or $m.Contains("没有找到包含")) { return "no_export_file" }
    if ($m.Contains("convertfrom-json") -or $m.Contains("json") -or $m.Contains("解析")) { return "json_parse_failed" }
    return "unknown"
}

function Cleanup-ResidualEditorCmd {
    param(
        [string]$ProjectPath
    )

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
            throw "单轮超时（>$TimeoutMinutes 分钟），已终止进程 PID=$($proc.Id)"
        }
    }

    if (-not $exportedPathFromLog -and $proc.ExitCode -ne 0) {
        throw "UnrealEditor-Cmd 退出码异常：$($proc.ExitCode)"
    }

    if ($exportedPathFromLog) {
        return $exportedPathFromLog
    }

    $after = Get-ChildItem $profileDir -Recurse -Filter "*.json" -ErrorAction SilentlyContinue |
        Where-Object { -not $before.ContainsKey($_.FullName) } |
        Sort-Object LastWriteTime -Descending

    if (-not $after -or $after.Count -eq 0) {
        throw "未检测到本轮新导出的 PCGProfiler JSON"
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

    throw "检测到新 JSON，但没有找到包含 run_duration_ms/thread_load 的 PCGProfiler 导出结果"
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

$editorCmd = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$pluginPath = Join-Path $EngineRoot "Plugins\PCGProfiler\PCGProfiler.uplugin"
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
        Write-Host "已清理残留 UnrealEditor-Cmd 进程：$killedCount"
    }
}

$results = @()
$failures = @()

foreach ($map in $Maps) {
    $mapSafe = $map.Replace("/", "_").Replace(":", "_")

    for ($i = 1; $i -le $effectiveIterations; $i++) {
        $currentRun++
        $progress = [int](($currentRun / [double]$totalRuns) * 100)
        Write-Progress -Id 1 -Activity "PCG Profiler 基准测试" -Status "地图 $map（第 $i/$effectiveIterations 轮，总进度 $currentRun/$totalRuns）" -PercentComplete $progress
        Write-Host "[$currentRun/$totalRuns] 开始：$map (第 $i 轮)"

        $logAbs = Join-Path $projectBenchmarkDir ("run_{0}_{1}.log" -f $mapSafe, $i)
        $attempt = 0
        $jsonPath = $null

        while ($attempt -le $effectiveRetry -and -not $jsonPath) {
            try {
                $attempt++
                if ($attempt -gt 1) {
                    Write-Host "  重试第 $attempt 次..."
                }

                $jsonPath = Invoke-OneRun -EngineRoot $EngineRoot -EditorCmd $editorCmd -ProjectPath $ProjectPath -Map $map -PluginPath $pluginPath -UseNullRHI:$effectiveUseNullRHI -LogPath $logAbs -TimeoutMinutes $effectiveTimeout
                $json = Get-Content -Path $jsonPath -Raw | ConvertFrom-Json
                $threadLoad = $json.thread_load
                $topNodeP95Ms = 0.0
                if ($json.nodes) {
                    $p95Candidates = @($json.nodes | ForEach-Object {
                        if ($null -ne $_.p95_ms) { [double]$_.p95_ms } else { 0.0 }
                    })
                    if ($p95Candidates.Count -gt 0) {
                        $topNodeP95Ms = ($p95Candidates | Measure-Object -Maximum).Maximum
                    }
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
                Write-Host "  完成：$jsonPath"
            }
            catch {
                $msg = $_.Exception.Message
                if ($attempt -gt $effectiveRetry) {
                    Write-Host "  失败：$msg"
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
Write-Progress -Id 1 -Activity "PCG Profiler 基准测试" -Completed

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

$topVolatileNodes = @()
foreach ($map in $Maps) {
    $mapRuns = @($results | Where-Object { $_.map -eq $map })
    if ($mapRuns.Count -eq 0) { continue }
    $bucket = @{}
    foreach ($r in $mapRuns) {
        try {
            $obj = Get-Content -Path $r.json_path -Raw | ConvertFrom-Json
            foreach ($n in ($obj.nodes ?? @())) {
                $key = "$($map)|$($n.node_id)|$($n.node_name)"
                if (-not $bucket.ContainsKey($key)) {
                    $bucket[$key] = [System.Collections.Generic.List[double]]::new()
                }
                $bucket[$key].Add([double]($n.total_ms ?? 0))
            }
        }
        catch {}
    }
    foreach ($k in $bucket.Keys) {
        $vals = @($bucket[$k].ToArray())
        if ($vals.Count -lt 2) { continue }
        $s = Get-Stats $vals
        $parts = $k.Split("|", 3)
        $topVolatileNodes += [pscustomobject]@{
            map = $parts[0]
            node_id = $parts[1]
            node_name = $parts[2]
            total_ms_mean = [Math]::Round($s.mean, 3)
            total_ms_cv = [Math]::Round($s.cv, 4)
            sample_count = $vals.Count
        }
    }
}
$topVolatileNodes = @($topVolatileNodes | Sort-Object total_ms_cv -Descending | Select-Object -First 20)

$criticalPathVolatility = @()
foreach ($map in $Maps) {
    $cpTotals = [System.Collections.Generic.List[double]]::new()
    foreach ($r in @($results | Where-Object { $_.map -eq $map })) {
        try {
            $obj = Get-Content -Path $r.json_path -Raw | ConvertFrom-Json
            $sum = 0.0
            foreach ($cp in ($obj.critical_path ?? @())) {
                $sum += [double]($cp.inclusive_ms ?? 0)
            }
            $cpTotals.Add($sum)
        }
        catch {}
    }
    if ($cpTotals.Count -gt 0) {
        $s = Get-Stats @($cpTotals.ToArray())
        $criticalPathVolatility += [pscustomobject]@{
            map = $map
            critical_path_total_mean_ms = [Math]::Round($s.mean, 3)
            critical_path_total_cv = [Math]::Round($s.cv, 4)
            sample_count = $cpTotals.Count
        }
    }
}

$summaryPath = Join-Path $reportDir ("基准结果汇总_{0}.json" -f $timestamp)
[pscustomobject]@{
    generated_at = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss")
    project = $ProjectPath
    maps = $Maps
    iterations_per_map = $effectiveIterations
    use_null_rhi = [bool]$effectiveUseNullRHI
    timeout_per_run_minutes = $effectiveTimeout
    retry_count = $effectiveRetry
    preset = $Preset
    raw_results = $results
    failures = $failures
    top_volatile_nodes = $topVolatileNodes
    critical_path_volatility = $criticalPathVolatility
    summary = $summary
} | ConvertTo-Json -Depth 10 | Set-Content -Path $summaryPath -Encoding UTF8

$reportPath = Join-Path $reportDir ("大规模真实关卡_统计稳定性性能回归报告_{0}.md" -f $timestamp)

$lines = @()
$lines += "# 大规模真实关卡统计稳定性/性能回归报告"
$lines += ""
$lines += "- 生成时间：$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
$lines += "- 项目：$ProjectPath"
$lines += "- 地图数量：$($Maps.Count)"
$lines += "- 配置模板：$Preset"
$lines += "- 每图轮次：$effectiveIterations"
$lines += "- 执行模式：$(if($effectiveUseNullRHI){'NullRHI'}else{'Render'})"
$lines += "- 单轮超时：$effectiveTimeout 分钟"
$lines += "- 重试次数：$effectiveRetry"
$lines += "- 原始汇总：`$summaryPath"
$lines += ""
$lines += "## 结果总览"
$lines += ""
$lines += "| 地图 | 样本数 | Duration Mean(ms) | Duration P95(ms) | Duration CV | GT Mean(ms) | Worker Mean(ms) | TopNodeP95 Mean(ms) | 并行效率均值 | 稳定性结论 |"
$lines += "|---|---:|---:|---:|---:|---:|---:|---:|---:|---|"

foreach ($s in $summary) {
    $resultText = if ($s.stable_overall) { "通过" } else { "未通过" }
    if (-not $s.has_valid_node_data) { $resultText = "无效样本(无节点数据)" }
    $lines += "| $($s.map) | $($s.sample_count) | $($s.duration_mean_ms) | $($s.duration_p95_ms) | $($s.duration_cv) | $($s.gt_mean_ms) | $($s.worker_mean_ms) | $($s.top_node_p95_mean_ms) | $($s.parallel_efficiency_mean) | $resultText |"
}

$lines += ""
$lines += "## 失败样本"
if ($failures.Count -eq 0) {
    $lines += ""
    $lines += "无。"
} else {
    $lines += ""
    foreach ($f in $failures) {
        $lines += "- 地图：$($f.map) 轮次：$($f.iteration) 分类：$($f.category) 错误：$($f.error) 日志：$($f.log)"
    }
}

$lines += ""
$lines += "## Top 波动节点（按 total_ms CV）"
if ($topVolatileNodes.Count -eq 0) {
    $lines += ""
    $lines += "无（样本不足或节点无可比数据）。"
} else {
    $lines += ""
    $lines += "| Map | NodeId | NodeName | Mean(ms) | CV | Samples |"
    $lines += "|---|---|---|---:|---:|---:|"
    foreach ($n in $topVolatileNodes) {
        $lines += "| $($n.map) | $($n.node_id) | $($n.node_name) | $($n.total_ms_mean) | $($n.total_ms_cv) | $($n.sample_count) |"
    }
}

$lines += ""
$lines += "## 关键路径波动摘要"
if ($criticalPathVolatility.Count -eq 0) {
    $lines += ""
    $lines += "无（样本不足或critical_path缺失）。"
} else {
    $lines += ""
    $lines += "| Map | CriticalPath Mean(ms) | CriticalPath CV | Samples |"
    $lines += "|---|---:|---:|---:|"
    foreach ($cpv in $criticalPathVolatility) {
        $lines += "| $($cpv.map) | $($cpv.critical_path_total_mean_ms) | $($cpv.critical_path_total_cv) | $($cpv.sample_count) |"
    }
}

$lines += ""
$lines += "## 判定阈值"
$lines += ""
$lines += "- Duration 稳定：CV <= 0.20"
$lines += "- GT 稳定：CV <= 0.25"
$lines += "- Worker 稳定：CV <= 0.25"
$lines += "- TopNodeP95 稳定：CV <= 0.30"

$regressionMd = $null
$regressionJson = $null
if ($BaselineSummary -and (Test-Path -LiteralPath $BaselineSummary)) {
    $compareScript = Join-Path $EngineRoot "Plugins\PCGProfiler\Scripts\Compare-PCGProfilerBaseline.ps1"
    if (Test-Path -LiteralPath $compareScript) {
        try {
            $compareOut = & powershell -NoProfile -ExecutionPolicy Bypass -File $compareScript -BaselineSummary $BaselineSummary -CurrentSummary $summaryPath
            foreach ($line in $compareOut) {
                if ($line -match "^REGRESSION_REPORT_MD=(.+)$") { $regressionMd = $matches[1].Trim() }
                if ($line -match "^REGRESSION_REPORT_JSON=(.+)$") { $regressionJson = $matches[1].Trim() }
            }
            if ($regressionMd) {
                $lines += ""
                $lines += "## 基线回归对比"
                $lines += ""
                $lines += "- 基线：$BaselineSummary"
                $lines += "- 回归报告（MD）：$regressionMd"
                if ($regressionJson) { $lines += "- 回归报告（JSON）：$regressionJson" }
            }
        }
        catch {
            $lines += ""
            $lines += "## 基线回归对比"
            $lines += ""
            $lines += "- 回归脚本执行失败：$($_.Exception.Message)"
        }
    }
}

Set-Content -Path $reportPath -Value ($lines -join "`r`n") -Encoding UTF8

Write-Output "BENCHMARK_REPORT=$reportPath"
Write-Output "BENCHMARK_SUMMARY=$summaryPath"
Write-Output "BENCHMARK_RESULTS=$($results.Count)"
Write-Output "BENCHMARK_FAILURES=$($failures.Count)"
