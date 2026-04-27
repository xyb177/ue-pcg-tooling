param(
    [string]$EngineRoot = "",
    [string]$ProjectPath = "",
    [string[]]$Maps = @("/Game/Levels/PCG/ElectricDreams_PCG"),
    [int]$EditorIterations = 3,
    [int]$RuntimeIterations = 3,
    [int]$PackagedIterations = 3,
    [switch]$EnablePackaged = $false,
    [string]$PackagedExePath = "",
    [int]$TimeoutPerRunMinutes = 12,
    [switch]$SkipBuild = $false,
    [switch]$QuickSelfTest = $false
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "PCGProfiler.ScriptCommon.ps1")
$EngineRoot = Resolve-PCGProfilerEngineRoot -PreferredEngineRoot $EngineRoot
$ProjectPath = Resolve-PCGProfilerProjectPath -PreferredProjectPath $ProjectPath

function Get-Stats([double[]]$Values) {
    if (-not $Values -or $Values.Count -eq 0) {
        return [ordered]@{ mean = 0; cv = 0; p95 = 0 }
    }
    $sorted = $Values | Sort-Object
    $count = $sorted.Count
    $mean = ($sorted | Measure-Object -Average).Average
    $idx95 = [Math]::Floor(0.95 * ($count - 1))
    $p95 = $sorted[[int]$idx95]
    $sumSq = 0.0
    foreach ($v in $sorted) { $sumSq += [Math]::Pow(($v - $mean), 2) }
    $std = [Math]::Sqrt($sumSq / $count)
    $cv = if ($mean -gt 0) { $std / $mean } else { 0 }
    return [ordered]@{ mean = $mean; cv = $cv; p95 = $p95 }
}

function Invoke-EditorCmdRun {
    param(
        [string]$EngineRoot,
        [string]$ProjectPath,
        [string]$Map,
        [bool]$UseNullRHI,
        [int]$TimeoutMinutes,
        [string]$LayerName
    )

    $editorCmd = Get-PCGProfilerEditorCmdPath -EngineRoot $EngineRoot
    $pluginPath = Get-PCGProfilerPluginUpluginPath -EngineRoot $EngineRoot
    $projectDir = Split-Path -Parent $ProjectPath
    $profilingDir = Join-Path $projectDir "Saved\Profiling\PCG"
    New-Item -ItemType Directory -Force -Path $profilingDir | Out-Null

    $before = @{}
    Get-ChildItem $profilingDir -Recurse -Filter "*.json" -ErrorAction SilentlyContinue | ForEach-Object { $before[$_.FullName] = $true }

    $startPy = Join-Path $EngineRoot "Plugins\PCGProfiler\Scripts\pcg_start_run.py"
    $waitPy = Join-Path $EngineRoot "Plugins\PCGProfiler\Scripts\pcg_wait_until_idle.py"
    $finishPy = Join-Path $EngineRoot "Plugins\PCGProfiler\Scripts\pcg_finish_export.py"
    $execCmds = "py `"$startPy`",py `"$waitPy`",py `"$finishPy`",Quit"

    $logPath = Join-Path $profilingDir ("three_layer_{0}_{1}.log" -f $LayerName, (Get-Date -Format "yyyyMMdd_HHmmssfff"))
    $args = @(
        "`"$ProjectPath`"",
        "`"$Map`"",
        "-PLUGIN=`"$pluginPath`"",
        "-Unattended",
        "-NoSplash",
        "-ExecCmds=`"$execCmds`"",
        "-AbsLog=`"$logPath`""
    )
    if ($UseNullRHI) { $args += "-NullRHI" }

    $proc = Start-Process -FilePath $editorCmd -ArgumentList $args -PassThru -NoNewWindow
    $deadline = (Get-Date).AddMinutes($TimeoutMinutes)
    while (-not $proc.HasExited) {
        Start-Sleep -Seconds 2
        if ((Get-Date) -ge $deadline) {
            try { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue } catch {}
            throw "timeout"
        }
    }

    $after = Get-ChildItem $profilingDir -Recurse -Filter "*.json" -ErrorAction SilentlyContinue |
        Where-Object { -not $before.ContainsKey($_.FullName) } |
        Sort-Object LastWriteTime -Descending
    if (-not $after -or $after.Count -eq 0) {
        throw "no_export_file"
    }

    foreach ($f in $after) {
        try {
            $obj = Get-Content -Path $f.FullName -Raw | ConvertFrom-Json
            if ($null -ne $obj.run_duration_ms -and $null -ne $obj.thread_load) {
                return $f.FullName
            }
        }
        catch {}
    }
    throw "json_parse_failed"
}

function Build-LayerSummary {
    param(
        [string]$LayerName,
        [string]$ProjectPath,
        [array]$Results,
        [array]$Failures,
        [string[]]$Maps,
        [int]$Iterations,
        [string]$OutputPath
    )

    $summary = @()
    foreach ($map in $Maps) {
        $group = @($Results | Where-Object { $_.map -eq $map })
        $valid = @($group | Where-Object { $_.node_count -gt 0 })
        $durStats = Get-Stats (@($group | Select-Object -ExpandProperty run_duration_ms))
        $gtStats = Get-Stats (@($group | Select-Object -ExpandProperty game_thread_total_ms))
        $wkStats = Get-Stats (@($group | Select-Object -ExpandProperty worker_total_ms))
        $topStats = Get-Stats (@($group | Select-Object -ExpandProperty top_node_p95_ms))
        $summary += [pscustomobject]@{
            map = $map
            sample_count = $group.Count
            valid_node_samples = $valid.Count
            has_valid_node_data = ($valid.Count -gt 0)
            duration_cv = [Math]::Round($durStats.cv, 4)
            gt_cv = [Math]::Round($gtStats.cv, 4)
            worker_cv = [Math]::Round($wkStats.cv, 4)
            top_node_p95_cv = [Math]::Round($topStats.cv, 4)
            stable_overall = (($durStats.cv -le 0.20) -and ($valid.Count -gt 0))
        }
    }

    $payload = [pscustomobject]@{
        generated_at = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss")
        layer_name = $LayerName
        project = $ProjectPath
        maps = $Maps
        iterations_per_map = $Iterations
        raw_results = $Results
        failures = $Failures
        summary = $summary
    }
    $payload | ConvertTo-Json -Depth 12 | Set-Content -Path $OutputPath -Encoding UTF8
}

if ($QuickSelfTest) {
    $EditorIterations = 1
    $RuntimeIterations = 1
    if ($EnablePackaged) { $PackagedIterations = 1 }
}

$buildOk = $true
if (-not $SkipBuild) {
    $buildBat = Join-Path $EngineRoot "Engine\Build\BatchFiles\Build.bat"
    & $buildBat UnrealEditor Win64 Development -NoHotReloadFromIDE -WaitMutex
    if ($LASTEXITCODE -ne 0) {
        $buildOk = $false
        throw "Build failed."
    }
}

$reportDir = Join-Path $EngineRoot "Plugins\PCGProfiler\Reports"
New-Item -ItemType Directory -Force -Path $reportDir | Out-Null
$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$layerOutputs = @()

foreach ($layer in @(
    @{ name = "editor_fixed_camera"; iterations = $EditorIterations; nullrhi = $true },
    @{ name = "pie_or_standalone_fixed_path"; iterations = $RuntimeIterations; nullrhi = $false }
)) {
    $results = @()
    $failures = @()
    Write-Host "=== Layer: $($layer.name) ==="
    foreach ($map in $Maps) {
        for ($i = 1; $i -le [int]$layer.iterations; $i++) {
            try {
                $jsonPath = Invoke-EditorCmdRun -EngineRoot $EngineRoot -ProjectPath $ProjectPath -Map $map -UseNullRHI:([bool]$layer.nullrhi) -TimeoutMinutes $TimeoutPerRunMinutes -LayerName $layer.name
                $json = Get-Content -Path $jsonPath -Raw | ConvertFrom-Json
                $topP95 = 0.0
                $p95Values = @()
                foreach ($n in @($json.nodes)) {
                    if ($null -ne $n.p95_ms) { $p95Values += [double]$n.p95_ms }
                }
                if ($p95Values.Count -gt 0) { $topP95 = ($p95Values | Measure-Object -Maximum).Maximum }
                $results += [pscustomobject]@{
                    map = $map
                    iteration = $i
                    json_path = $jsonPath
                    run_duration_ms = [double]$json.run_duration_ms
                    node_count = [int]$json.node_count
                    game_thread_total_ms = [double]$json.thread_load.game_thread_total_ms
                    worker_total_ms = [double]$json.thread_load.worker_total_ms
                    parallel_efficiency_score = [double]$json.thread_load.parallel_efficiency.score
                    top_node_p95_ms = [double]$topP95
                }
            }
            catch {
                $failures += [pscustomobject]@{
                    map = $map
                    iteration = $i
                    error = $_.Exception.Message
                }
            }
        }
    }

    $summaryPath = Join-Path $reportDir ("pcg_layer_{0}_{1}.json" -f $layer.name, $timestamp)
    Build-LayerSummary -LayerName $layer.name -ProjectPath $ProjectPath -Results $results -Failures $failures -Maps $Maps -Iterations ([int]$layer.iterations) -OutputPath $summaryPath
    $layerOutputs += [pscustomobject]@{
        layer = $layer.name
        summary = $summaryPath
        report = ""
    }
}

if ($EnablePackaged) {
    if (-not $PackagedExePath) {
        throw "EnablePackaged requires PackagedExePath."
    }
    if (-not (Test-Path -LiteralPath $PackagedExePath)) {
        throw "Packaged exe not found: $PackagedExePath"
    }

    $results = @()
    $failures = @()
    Write-Host "=== Layer: packaged_final ==="
    $projectDir = Split-Path -Parent $ProjectPath
    $profilingDir = Join-Path $projectDir "Saved\Profiling\PCG"
    foreach ($map in $Maps) {
        for ($i = 1; $i -le $PackagedIterations; $i++) {
            $before = @{}
            Get-ChildItem $profilingDir -Recurse -Filter "*.json" -ErrorAction SilentlyContinue | ForEach-Object { $before[$_.FullName] = $true }
            $proc = Start-Process -FilePath $PackagedExePath -ArgumentList @($map, "-unattended", "-nosplash", "-ExecCmds=PCGProfiler.StartRuntimeRun;PCGProfiler.WaitForRunComplete 600 0.25;PCGProfiler.EndRuntimeRunAndExport;quit") -PassThru -NoNewWindow
            $deadline = (Get-Date).AddMinutes($TimeoutPerRunMinutes)
            while (-not $proc.HasExited) {
                Start-Sleep -Seconds 2
                if ((Get-Date) -ge $deadline) {
                    try { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue } catch {}
                    $failures += [pscustomobject]@{ map = $map; iteration = $i; error = "timeout" }
                    break
                }
            }
            $newJson = Get-ChildItem $profilingDir -Recurse -Filter "*.json" -ErrorAction SilentlyContinue |
                Where-Object { -not $before.ContainsKey($_.FullName) } |
                Sort-Object LastWriteTime -Descending |
                Select-Object -First 1
            if (-not $newJson) {
                $failures += [pscustomobject]@{ map = $map; iteration = $i; error = "no_export_file" }
                continue
            }
            try {
                $json = Get-Content -Path $newJson.FullName -Raw | ConvertFrom-Json
                $results += [pscustomobject]@{
                    map = $map
                    iteration = $i
                    json_path = $newJson.FullName
                    run_duration_ms = [double]$json.run_duration_ms
                    node_count = [int]$json.node_count
                    game_thread_total_ms = [double]$json.thread_load.game_thread_total_ms
                    worker_total_ms = [double]$json.thread_load.worker_total_ms
                    parallel_efficiency_score = [double]$json.thread_load.parallel_efficiency.score
                    top_node_p95_ms = 0.0
                }
            }
            catch {
                $failures += [pscustomobject]@{ map = $map; iteration = $i; error = "json_parse_failed" }
            }
        }
    }
    $summaryPath = Join-Path $reportDir ("pcg_layer_packaged_final_{0}.json" -f $timestamp)
    Build-LayerSummary -LayerName "packaged_final" -ProjectPath $ProjectPath -Results $results -Failures $failures -Maps $Maps -Iterations $PackagedIterations -OutputPath $summaryPath
    $layerOutputs += [pscustomobject]@{
        layer = "packaged_final"
        summary = $summaryPath
        report = ""
    }
}

$qualityGateScript = Join-Path $EngineRoot "Plugins\PCGProfiler\Scripts\Run-PCGProfilerQualityGate.ps1"
$summaryFiles = @($layerOutputs | ForEach-Object { $_.summary })
$gateOut = & $qualityGateScript -SummaryFiles $summaryFiles
$gateJson = $null
$gateMd = $null
$gateStatus = $null
foreach ($line in $gateOut) {
    if ($line -match "^QUALITY_GATE_JSON=(.+)$") { $gateJson = $matches[1].Trim() }
    if ($line -match "^QUALITY_GATE_MD=(.+)$") { $gateMd = $matches[1].Trim() }
    if ($line -match "^QUALITY_GATE_STATUS=(.+)$") { $gateStatus = $matches[1].Trim() }
}

$pipelineJson = Join-Path $reportDir ("pcg_three_layer_pipeline_{0}.json" -f $timestamp)
$pipelineMd = Join-Path $reportDir ("pcg_three_layer_pipeline_{0}.md" -f $timestamp)

$pipelineObj = [pscustomobject]@{
    generated_at = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss")
    build_ok = $buildOk
    project = $ProjectPath
    maps = $Maps
    layers = $layerOutputs
    quality_gate_json = $gateJson
    quality_gate_md = $gateMd
    quality_gate_status = $gateStatus
}
$pipelineObj | ConvertTo-Json -Depth 12 | Set-Content -Path $pipelineJson -Encoding UTF8

$lines = @()
$lines += "# PCG Three-Layer Benchmark Pipeline"
$lines += ""
$lines += "- Generated: $((Get-Date).ToString('yyyy-MM-dd HH:mm:ss'))"
$lines += "- Build: $(if($buildOk){'PASS'}else{'FAIL'})"
$lines += "- Project: $ProjectPath"
$lines += ""
$lines += "| Layer | Summary |"
$lines += "|---|---|"
foreach ($l in $layerOutputs) {
    $lines += "| $($l.layer) | $($l.summary) |"
}
$lines += ""
$lines += "- Quality Gate: $gateStatus"
$lines += "- Quality Gate JSON: $gateJson"
$lines += "- Quality Gate MD: $gateMd"
Set-Content -Path $pipelineMd -Value ($lines -join "`r`n") -Encoding UTF8

Write-Output "THREE_LAYER_PIPELINE_JSON=$pipelineJson"
Write-Output "THREE_LAYER_PIPELINE_MD=$pipelineMd"
Write-Output "THREE_LAYER_GATE_STATUS=$gateStatus"
