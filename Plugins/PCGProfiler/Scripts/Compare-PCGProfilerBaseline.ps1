param(
    [Parameter(Mandatory = $true)]
    [string]$BaselineSummary,

    [Parameter(Mandatory = $true)]
    [string]$CurrentSummary,

    [double]$DurationRegressionThresholdPercent = 10.0,
    [double]$TopNodeP95RegressionThresholdPercent = 15.0,
    [double]$ParallelEfficiencyDropThresholdPoints = 8.0
)

$ErrorActionPreference = "Stop"

function Index-ByMap($SummaryItems) {
    $index = @{}
    foreach ($item in $SummaryItems) {
        $index[$item.map] = $item
    }
    return $index
}

function PercentChange([double]$baseline, [double]$current) {
    if ($baseline -eq 0) { return $null }
    return (($current - $baseline) / $baseline) * 100.0
}

$baseline = Get-Content -Path $BaselineSummary -Raw | ConvertFrom-Json
$current = Get-Content -Path $CurrentSummary -Raw | ConvertFrom-Json

$baseIndex = Index-ByMap $baseline.summary
$currIndex = Index-ByMap $current.summary
$maps = @($currIndex.Keys | Sort-Object)

$results = @()
$globalPass = $true

foreach ($map in $maps) {
    $b = $baseIndex[$map]
    $c = $currIndex[$map]

    if (-not $b) {
        $results += [pscustomobject]@{
            map = $map
            status = "FAIL"
            reason = "baseline missing"
        }
        $globalPass = $false
        continue
    }

    $durationDelta = PercentChange $b.duration_mean_ms $c.duration_mean_ms
    $peDrop = $b.parallel_efficiency_mean - $c.parallel_efficiency_mean
    $hasNodeData = ($b.has_valid_node_data -and $c.has_valid_node_data)

    $durationPass = ($durationDelta -ne $null) -and ($durationDelta -le $DurationRegressionThresholdPercent)
    $pePass = $peDrop -le $ParallelEfficiencyDropThresholdPoints
    $topNodeP95Delta = PercentChange $b.top_node_p95_mean_ms $c.top_node_p95_mean_ms
    $topNodeP95Pass = $hasNodeData -and ($topNodeP95Delta -ne $null) -and ($topNodeP95Delta -le $TopNodeP95RegressionThresholdPercent)
    $topNodeP95Status = if ($hasNodeData) {
        if ($topNodeP95Delta -eq $null) { "invalid (baseline top_node_p95_mean_ms = 0)" } else { "OK" }
    } else {
        "invalid (missing node data)"
    }

    $mapPass = $durationPass -and $pePass -and $topNodeP95Pass
    if (-not $mapPass) { $globalPass = $false }

    $results += [pscustomobject]@{
        map = $map
        status = if ($mapPass) { "PASS" } else { "FAIL" }
        duration_delta_percent = if ($durationDelta -ne $null) { [Math]::Round($durationDelta, 3) } else { $null }
        duration_threshold_percent = $DurationRegressionThresholdPercent
        duration_check = if ($durationPass) { "PASS" } else { "FAIL" }
        parallel_efficiency_drop = [Math]::Round($peDrop, 3)
        parallel_efficiency_drop_threshold = $ParallelEfficiencyDropThresholdPoints
        parallel_efficiency_check = if ($pePass) { "PASS" } else { "FAIL" }
        top_node_p95_delta_percent = if ($topNodeP95Delta -ne $null) { [Math]::Round($topNodeP95Delta, 3) } else { $null }
        top_node_p95_threshold_percent = $TopNodeP95RegressionThresholdPercent
        top_node_p95_check = if ($topNodeP95Pass) { "PASS" } else { "FAIL" }
        top_node_p95_note = $topNodeP95Status
        baseline_valid_node_samples = $b.valid_node_samples
        current_valid_node_samples = $c.valid_node_samples
    }
}

$report = [pscustomobject]@{
    generated_at = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss")
    baseline_summary = $BaselineSummary
    current_summary = $CurrentSummary
    thresholds = [pscustomobject]@{
        duration_regression_percent = $DurationRegressionThresholdPercent
        top_node_p95_regression_percent = $TopNodeP95RegressionThresholdPercent
        parallel_efficiency_drop_points = $ParallelEfficiencyDropThresholdPoints
    }
    overall_status = if ($globalPass) { "PASS" } else { "FAIL" }
    per_map = $results
}

$reportDir = Split-Path -Parent $CurrentSummary
$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$outJson = Join-Path $reportDir ("pcg_regression_report_{0}.json" -f $stamp)
$outMd = Join-Path $reportDir ("pcg_regression_report_{0}.md" -f $stamp)

$report | ConvertTo-Json -Depth 8 | Set-Content -Path $outJson -Encoding UTF8

$lines = @()
$lines += "# PCG Regression Report"
$lines += ""
$lines += "- Generated at: $((Get-Date).ToString('yyyy-MM-dd HH:mm:ss'))"
$lines += "- Baseline: $BaselineSummary"
$lines += "- Current: $CurrentSummary"
$lines += "- Overall: $($report.overall_status)"
$lines += ""
$lines += "| Map | Status | Duration Delta | Duration Check | PE Drop | PE Check | TopNodeP95 Delta | TopNodeP95 Check | Note |"
$lines += "|---|---|---:|---|---:|---|---:|---|---|"
foreach ($r in $results) {
    $lines += "| $($r.map) | $($r.status) | $($r.duration_delta_percent)% | $($r.duration_check) | $($r.parallel_efficiency_drop) | $($r.parallel_efficiency_check) | $($r.top_node_p95_delta_percent)% | $($r.top_node_p95_check) | $($r.top_node_p95_note) |"
}

Set-Content -Path $outMd -Value ($lines -join "`r`n") -Encoding UTF8

Write-Output "REGRESSION_REPORT_JSON=$outJson"
Write-Output "REGRESSION_REPORT_MD=$outMd"
Write-Output "REGRESSION_STATUS=$($report.overall_status)"
