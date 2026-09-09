[CmdletBinding()]
param(
    [Parameter()]
    [ValidateNotNullOrEmpty()]
    [string]$ObsPath = (Join-Path $env:ProgramFiles 'obs-studio')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Resolve-ObsRoot {
    param([string]$Path)

    $root = [System.IO.Path]::GetFullPath($Path)
    $executable = Join-Path $root 'bin\64bit\obs64.exe'
    if (-not (Test-Path $executable -PathType Leaf)) {
        throw "OBS Studio was not found at '$root'. Expected '$executable'."
    }

    return $root
}

if (Get-Process -Name 'obs64' -ErrorAction SilentlyContinue) {
    throw 'Close OBS Studio before installing ChatView OBS.'
}

$obsRoot = Resolve-ObsRoot -Path $ObsPath
$files = @(
    @{
        Source = Join-Path $PSScriptRoot 'obs-plugins\64bit\chat-view-obs.dll'
        Destination = Join-Path $obsRoot 'obs-plugins\64bit\chat-view-obs.dll'
    },
    @{
        Source = Join-Path $PSScriptRoot 'obs-plugins\64bit\chat-view-hud.exe'
        Destination = Join-Path $obsRoot 'obs-plugins\64bit\chat-view-hud.exe'
    },
    @{
        Source = Join-Path $PSScriptRoot 'data\obs-plugins\chat-view-obs\locale\en-US.ini'
        Destination = Join-Path $obsRoot 'data\obs-plugins\chat-view-obs\locale\en-US.ini'
    },
    @{
        Source = Join-Path $PSScriptRoot 'data\obs-plugins\chat-view-obs\locale\ko-KR.ini'
        Destination = Join-Path $obsRoot 'data\obs-plugins\chat-view-obs\locale\ko-KR.ini'
    }
)

foreach ($file in $files) {
    if (-not (Test-Path $file.Source -PathType Leaf)) {
        throw "Package file is missing: $($file.Source)"
    }
}

foreach ($file in $files) {
    $destinationDirectory = Split-Path $file.Destination -Parent
    New-Item -ItemType Directory -Path $destinationDirectory -Force | Out-Null
    Copy-Item -LiteralPath $file.Source -Destination $file.Destination -Force
}

Write-Host "ChatView OBS was installed to '$obsRoot'."
Write-Host 'Start OBS Studio to load the plugin.'
