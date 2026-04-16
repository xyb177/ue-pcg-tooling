param(
    [string]$EngineRoot = "F:\UnrealEngine-5.7.3-release",
    [string]$ProjectPath = "F:\Unreal Projects\ElectricDreamsEnv\ElectricDreamsEnv.uproject",
    [string]$Map = "/Game/Levels/PCG/ElectricDreams_PCG",
    [switch]$KeepOpen = $true
)

$ErrorActionPreference = "Stop"

$editor = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor.exe"
$pipelinePy = Join-Path $EngineRoot "Plugins\PCGProfiler\Scripts\pcg_run_start_wait_finish.py"
$log = "F:\Unreal Projects\ElectricDreamsEnv\Saved\Logs\pcg_start_wait_finish.log"

if (-not (Test-Path $editor)) { throw "UnrealEditor.exe not found: $editor" }
if (-not (Test-Path $ProjectPath)) { throw "uproject not found: $ProjectPath" }
if (-not (Test-Path $pipelinePy)) { throw "pipeline script not found: $pipelinePy" }

$args = @(
    "-project=$ProjectPath",
    $Map,
    "-NoSplash",
    "-ExecutePythonScript=$pipelinePy",
    "-AbsLog=$log"
)

if ($KeepOpen) {
    Start-Process -FilePath $editor -ArgumentList $args | Out-Null
    Write-Host "LAUNCHED: visible editor"
    Write-Host "LOG=$log"
} else {
    & $editor @args
    Write-Host "DONE: editor process exited"
    Write-Host "LOG=$log"
}
