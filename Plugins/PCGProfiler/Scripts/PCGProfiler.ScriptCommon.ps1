Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-PCGProfilerEngineRoot {
    param(
        [string]$PreferredEngineRoot = ""
    )

    if ($PreferredEngineRoot -and (Test-Path -LiteralPath $PreferredEngineRoot)) {
        return (Resolve-Path -LiteralPath $PreferredEngineRoot).Path
    }

    if ($PSScriptRoot) {
        $candidate = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\.."))
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }

    throw "Unable to resolve EngineRoot. Please pass -EngineRoot explicitly."
}

function Resolve-PCGProfilerProjectPath {
    param(
        [string]$PreferredProjectPath = "",
        [string]$DefaultProjectPath = "F:\Unreal Projects\ElectricDreamsEnv\ElectricDreamsEnv.uproject"
    )

    if ($PreferredProjectPath -and (Test-Path -LiteralPath $PreferredProjectPath)) {
        return (Resolve-Path -LiteralPath $PreferredProjectPath).Path
    }

    if (Test-Path -LiteralPath $DefaultProjectPath) {
        return (Resolve-Path -LiteralPath $DefaultProjectPath).Path
    }

    throw "Unable to resolve ProjectPath. Please pass -ProjectPath explicitly."
}

function Get-PCGProfilerEditorCmdPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$EngineRoot
    )

    $cmd = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
    if (-not (Test-Path -LiteralPath $cmd)) {
        throw "UnrealEditor-Cmd not found: $cmd"
    }
    return $cmd
}

function Get-PCGProfilerPluginUpluginPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$EngineRoot
    )

    $pluginPath = Join-Path $EngineRoot "Plugins\PCGProfiler\PCGProfiler.uplugin"
    if (-not (Test-Path -LiteralPath $pluginPath)) {
        throw "PCGProfiler.uplugin not found: $pluginPath"
    }
    return $pluginPath
}

