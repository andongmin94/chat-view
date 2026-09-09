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

if (-not (Test-Administrator)) {
    Invoke-ElevatedSelf
}

if (Get-Process -Name 'obs64' -ErrorAction SilentlyContinue) {
    throw 'Close OBS Studio before uninstalling ChatView OBS.'
}

$obsRoot = [System.IO.Path]::GetFullPath($ObsPath)
if (-not (Test-Path $obsRoot -PathType Container)) {
    throw "OBS Studio directory was not found: '$obsRoot'."
}

$files = @(
    (Join-Path $obsRoot 'obs-plugins\64bit\chat-view-obs.dll'),
    (Join-Path $obsRoot 'obs-plugins\64bit\chat-view-hud.exe'),
    (Join-Path $obsRoot 'obs-plugins\64bit\chat-view-config.exe'),
    (Join-Path $obsRoot 'data\obs-plugins\chat-view-obs\locale\en-US.ini'),
    (Join-Path $obsRoot 'data\obs-plugins\chat-view-obs\locale\ko-KR.ini')
)

foreach ($file in $files) {
    if (Test-Path $file -PathType Leaf) {
        Remove-Item -LiteralPath $file -Force
    }
}

$dataRoot = Join-Path $obsRoot 'data\obs-plugins\chat-view-obs'
$localeDirectory = Join-Path $dataRoot 'locale'
if ((Test-Path $localeDirectory -PathType Container) -and
    -not (Get-ChildItem -LiteralPath $localeDirectory -Force | Select-Object -First 1)) {
    Remove-Item -LiteralPath $localeDirectory -Force
}
if ((Test-Path $dataRoot -PathType Container) -and
    -not (Get-ChildItem -LiteralPath $dataRoot -Force | Select-Object -First 1)) {
    Remove-Item -LiteralPath $dataRoot -Force
}

Write-Host "ChatView OBS was removed from '$obsRoot'."
Write-Host 'User settings in %LOCALAPPDATA%\ChatView were left intact.'
