param(
    [Parameter(Mandatory = $true)]
    [string[]]$SummaryFiles,

    [double]$DurationCvThreshold = 0.20,
    [double]$ThreadCvThreshold = 0.25,
    [double]$TopNodeP95CvThreshold = 0.30,
    [int]$MinValidNodeSamples = 1,
    [string]$OutputDir = ""
)

$ErrorActionPreference = "Stop"

function Read-SummaryFile([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Summary file not found: $Path"
    }
    return (Get-Content -Path $Path -Raw | ConvertFrom-Json)
}

function Normalize-Bool($Value) {
    if ($null -eq $Value) { return $false }
    if ($Value -is [bool]) { return [bool]$Value }
    return ([string]$Value).ToLowerInvariant() -eq "true"
}

function Get-OrDefault($Value, $DefaultValue) {
    if ($null -eq $Value) { return $DefaultValue }
    return $Value
}

if (-not $OutputDir) {
    $OutputDir = Join-Path (Split-Path -Parent $SummaryFiles[0]) "quality_gate"
}
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

$layerResults = @()
$overallPass = $true

foreach ($summaryPath in $SummaryFiles) {
    $obj = Read-SummaryFile $summaryPath
    $layerName = if ($obj.layer_name) { [string]$obj.layer_name } else { [IO.Path]::GetFileNameWithoutExtension($summaryPath) }
    $entries = @($obj.summary)
    $failures = @($obj.failures)

    $perMap = @()
    foreach ($entry in $entries) {
        $validSamples = [int](Get-OrDefault $entry.valid_node_samples 0)
        $hasNodeData = Normalize-Bool (Get-OrDefault $entry.has_valid_node_data ($validSamples -ge $MinValidNodeSamples))
        $durationCv = [double](Get-OrDefault $entry.duration_cv 0.0)
        $gtCv = [double](Get-OrDefault $entry.gt_cv 0.0)
        $workerCv = [double](Get-OrDefault $entry.worker_cv 0.0)
        $topCv = [double](Get-OrDefault $entry.top_node_p95_cv 0.0)

        $durationPass = $durationCv -le $DurationCvThreshold
        $gtPass = $hasNodeData -and ($gtCv -le $ThreadCvThreshold)
        $workerPass = $hasNodeData -and ($workerCv -le $ThreadCvThreshold)
        $topPass = $hasNodeData -and ($topCv -le $TopNodeP95CvThreshold)
        $mapPass = $hasNodeData -and $durationPass -and $gtPass -and $workerPass -and $topPass

        if (-not $mapPass) { $overallPass = $false }

        $perMap += [pscustomobject]@{
            map = [string]$entry.map
            status = if ($mapPass) { "PASS" } else { "FAIL" }
            valid_node_samples = $validSamples
            has_valid_node_data = $hasNodeData
            duration_cv = [Math]::Round($durationCv, 4)
            gt_cv = [Math]::Round($gtCv, 4)
            worker_cv = [Math]::Round($workerCv, 4)
            top_node_p95_cv = [Math]::Round($topCv, 4)
            checks = [pscustomobject]@{
                duration = if ($durationPass) { "PASS" } else { "FAIL" }
                gt = if ($gtPass) { "PASS" } else { "FAIL" }
                worker = if ($workerPass) { "PASS" } else { "FAIL" }
                top_node_p95 = if ($topPass) { "PASS" } else { "FAIL" }
            }
        }
    }

    $failureCount = $failures.Count
    if ($failureCount -gt 0) { $overallPass = $false }

    $layerResults += [pscustomobject]@{
        layer = $layerName
        source_summary = $summaryPath
        map_count = $perMap.Count
        run_failures = $failureCount
        status = if (($failureCount -eq 0) -and (@($perMap | Where-Object { $_.status -eq "FAIL" }).Count -eq 0)) { "PASS" } else { "FAIL" }
        per_map = $perMap
    }
}

$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$jsonOut = Join-Path $OutputDir ("pcg_quality_gate_{0}.json" -f $timestamp)
$mdOut = Join-Path $OutputDir ("pcg_quality_gate_{0}.md" -f $timestamp)

$report = [pscustomobject]@{
    generated_at = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss")
    overall_status = if ($overallPass) { "PASS" } else { "FAIL" }
    thresholds = [pscustomobject]@{
        duration_cv = $DurationCvThreshold
        thread_cv = $ThreadCvThreshold
        top_node_p95_cv = $TopNodeP95CvThreshold
        min_valid_node_samples = $MinValidNodeSamples
    }
    layers = $layerResults
}

$report | ConvertTo-Json -Depth 12 | Set-Content -Path $jsonOut -Encoding UTF8

$lines = @()
$lines += "# PCG Profiler Quality Gate"
$lines += ""
$lines += "- Generated: $((Get-Date).ToString('yyyy-MM-dd HH:mm:ss'))"
$lines += "- Overall: $($report.overall_status)"
$lines += ""
$lines += "| Layer | Status | Run Failures | Maps |"
$lines += "|---|---|---:|---:|"
foreach ($layer in $layerResults) {
    $lines += "| $($layer.layer) | $($layer.status) | $($layer.run_failures) | $($layer.map_count) |"
}

foreach ($layer in $layerResults) {
    $lines += ""
    $lines += "## Layer: $($layer.layer)"
    $lines += ""
    $lines += "- Source: $($layer.source_summary)"
    $lines += ""
    $lines += "| Map | Status | Valid Samples | Duration CV | GT CV | Worker CV | TopNodeP95 CV |"
    $lines += "|---|---|---:|---:|---:|---:|---:|"
    foreach ($m in $layer.per_map) {
        $lines += "| $($m.map) | $($m.status) | $($m.valid_node_samples) | $($m.duration_cv) | $($m.gt_cv) | $($m.worker_cv) | $($m.top_node_p95_cv) |"
    }
}

Set-Content -Path $mdOut -Value ($lines -join "`r`n") -Encoding UTF8

Write-Output "QUALITY_GATE_JSON=$jsonOut"
Write-Output "QUALITY_GATE_MD=$mdOut"
Write-Output "QUALITY_GATE_STATUS=$($report.overall_status)"
