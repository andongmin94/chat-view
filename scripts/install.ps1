[CmdletBinding()]
param(
    [Parameter()]
    [ValidateNotNullOrEmpty()]
    [string]$ObsPath = (Join-Path $env:ProgramFiles 'obs-studio')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Test-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Invoke-ElevatedSelf {
    $hostExecutable = (Get-Process -Id $PID).Path
    $arguments = @(
        '-NoProfile',
        '-ExecutionPolicy', 'Bypass',
        '-File', "`"$PSCommandPath`"",
        '-ObsPath', "`"$ObsPath`""
    )

    $process = Start-Process `
        -FilePath $hostExecutable `
        -Verb RunAs `
        -ArgumentList $arguments `
        -Wait `
        -PassThru
    exit $process.ExitCode
}

function Resolve-ObsRoot {
    param([string]$Path)

    $root = [System.IO.Path]::GetFullPath($Path)
    $executable = Join-Path $root 'bin\64bit\obs64.exe'
    if (-not (Test-Path $executable -PathType Leaf)) {
        throw "OBS Studio was not found at '$root'. Expected '$executable'."
    }
    return $root
}

$files = @(
    @{
        Source = Join-Path $PSScriptRoot 'obs-plugins\64bit\chat-view-obs.dll'
        RelativeDestination = 'obs-plugins\64bit\chat-view-obs.dll'
    },
    @{
        Source = Join-Path $PSScriptRoot 'obs-plugins\64bit\chat-view-hud.exe'
        RelativeDestination = 'obs-plugins\64bit\chat-view-hud.exe'
    },
    @{
        Source = Join-Path $PSScriptRoot 'obs-plugins\64bit\chat-view-config.exe'
        RelativeDestination = 'obs-plugins\64bit\chat-view-config.exe'
    },
    @{
        Source = Join-Path $PSScriptRoot 'data\obs-plugins\chat-view-obs\locale\en-US.ini'
        RelativeDestination = 'data\obs-plugins\chat-view-obs\locale\en-US.ini'
    },
    @{
        Source = Join-Path $PSScriptRoot 'data\obs-plugins\chat-view-obs\locale\ko-KR.ini'
        RelativeDestination = 'data\obs-plugins\chat-view-obs\locale\ko-KR.ini'
    }
)

foreach ($file in $files) {
    if (-not (Test-Path $file.Source -PathType Leaf)) {
        throw "Package file is missing: $($file.Source)"
    }
}

$runtimeInstaller = Join-Path $PSScriptRoot 'ensure-webview2-runtime.ps1'
if (-not (Test-Path $runtimeInstaller -PathType Leaf)) {
    throw "Package file is missing: $runtimeInstaller"
}

if (-not (Test-Administrator)) {
    Invoke-ElevatedSelf
}

if (Get-Process -Name 'obs64' -ErrorAction SilentlyContinue) {
    throw 'Close OBS Studio before installing ChatView OBS.'
}

$obsRoot = Resolve-ObsRoot -Path $ObsPath
& $runtimeInstaller

foreach ($file in $files) {
    $destination = Join-Path $obsRoot $file.RelativeDestination
    $destinationDirectory = Split-Path $destination -Parent
    New-Item -ItemType Directory -Path $destinationDirectory -Force | Out-Null
    Copy-Item -LiteralPath $file.Source -Destination $destination -Force
}

Write-Host "ChatView OBS was installed to '$obsRoot'."
Write-Host 'Start OBS Studio, then open Tools > ChatView Settings.'
