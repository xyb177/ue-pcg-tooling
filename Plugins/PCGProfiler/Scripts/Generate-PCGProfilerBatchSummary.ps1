param(
  [string[]]$InputJson = @(),
  [string]$InputJsonList = '',
  [string]$OutputDir = '',
  [string]$ProjectRoot = '',
  [string]$EngineRoot = ''
)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$script = Join-Path $PSScriptRoot 'generate_pcg_batch_summary.py'
if (-not (Test-Path -LiteralPath $script)) {
  throw "Script not found: $script"
}

$args = @($script)
if (-not [string]::IsNullOrWhiteSpace($InputJsonList)) {
  $InputJson = @($InputJsonList -split '\|')
}
foreach ($jsonPath in $InputJson) {
  if (-not [string]::IsNullOrWhiteSpace($jsonPath)) {
    $resolved = $jsonPath
    try { $resolved = (Resolve-Path -LiteralPath $jsonPath -ErrorAction Stop).Path } catch {}
    $args += @('--input-json', $resolved)
  }
}
if (-not [string]::IsNullOrWhiteSpace($OutputDir)) { $args += @('--output-dir', $OutputDir) }
if (-not [string]::IsNullOrWhiteSpace($ProjectRoot)) { $args += @('--project-root', $ProjectRoot) }

$pyCmd = Get-Command py -ErrorAction SilentlyContinue
$pythonCmd = Get-Command python -ErrorAction SilentlyContinue

$resolvedEngineRoot = $EngineRoot
if ([string]::IsNullOrWhiteSpace($resolvedEngineRoot)) {
  $envEngine = [Environment]::GetEnvironmentVariable('UE_ENGINE_ROOT')
  if (-not [string]::IsNullOrWhiteSpace($envEngine) -and (Test-Path -LiteralPath (Join-Path $envEngine 'Engine\Binaries\Win64\UnrealEditor.exe'))) {
    $resolvedEngineRoot = $envEngine
  }
}

if ([string]::IsNullOrWhiteSpace($resolvedEngineRoot)) {
  try {
    $candidate = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..\..\..')).Path
    if (Test-Path -LiteralPath (Join-Path $candidate 'Engine\Binaries\Win64\UnrealEditor.exe')) {
      $resolvedEngineRoot = $candidate
    }
  } catch {}
}

if ([string]::IsNullOrWhiteSpace($resolvedEngineRoot) -and -not [string]::IsNullOrWhiteSpace($ProjectRoot)) {
  try {
    $drive = [System.IO.Path]::GetPathRoot((Resolve-Path $ProjectRoot).Path)
    $engineCandidates = Get-ChildItem -Path $drive -Directory -ErrorAction SilentlyContinue |
      Where-Object { $_.Name -like 'UnrealEngine*' } |
      Sort-Object Name -Descending
    foreach ($c in $engineCandidates) {
      if (Test-Path -LiteralPath (Join-Path $c.FullName 'Engine\Binaries\Win64\UnrealEditor.exe')) {
        $resolvedEngineRoot = $c.FullName
        break
      }
    }
  } catch {}
}

$uePython = if (-not [string]::IsNullOrWhiteSpace($resolvedEngineRoot)) {
  Join-Path $resolvedEngineRoot 'Engine\Binaries\ThirdParty\Python3\Win64\python.exe'
} else {
  ''
}

$invoked = $false
$exit = 9009

if ($pyCmd) {
  & py -3 @args
  $exit = $LASTEXITCODE
  $invoked = $true
}

if ($exit -ne 0 -and -not [string]::IsNullOrWhiteSpace($uePython) -and (Test-Path -LiteralPath $uePython)) {
  & $uePython @args
  $exit = $LASTEXITCODE
  $invoked = $true
}

if ($exit -ne 0 -and $pythonCmd) {
  & python @args
  $exit = $LASTEXITCODE
  $invoked = $true
}

if (-not $invoked) {
  throw "Python runtime not found."
}

if ($exit -ne 0) {
  throw "generate_pcg_batch_summary.py failed with exit code $exit"
}
