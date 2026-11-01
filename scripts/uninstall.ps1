[CmdletBinding(DefaultParameterSetName = 'Obs')]
param(
    [Parameter(ParameterSetName = 'Obs')]
    [ValidateNotNullOrEmpty()]
    [string]$ObsPath = (Join-Path $env:ProgramFiles 'obs-studio'),

    [Parameter(Mandatory, ParameterSetName = 'Companion')]
    [switch]$Companion,

    [Parameter(ParameterSetName = 'Companion')]
    [ValidateNotNullOrEmpty()]
    [string]$CompanionPath = (Join-Path $env:LOCALAPPDATA 'Programs\ChatViewCompanion')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-CompanionDirectory {
    param(
        [Parameter(Mandatory)]
        [string]$Path,

        [switch]$AllowNew
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        if ($AllowNew) { return }
        throw "Companion installation directory was not found: '$Path'."
    }
    if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
        throw "Companion destination is not a directory: '$Path'."
    }
    foreach ($itemPath in @($Path, (Join-Path $Path 'fonts'))) {
        if ((Test-Path -LiteralPath $itemPath) -and
            ((Get-Item -LiteralPath $itemPath -Force).Attributes -band
                [IO.FileAttributes]::ReparsePoint)) {
            throw "Companion installation paths may not be reparse points: '$itemPath'."
        }
    }
    if ($AllowNew -and -not (Get-ChildItem -LiteralPath $Path -Force | Select-Object -First 1)) {
        return
    }

    $hud = Join-Path $Path 'chat-view-hud.exe'
    $launcher = Join-Path $Path 'start-companion.cmd'
    $expectedLauncher = @(
        '@echo off',
        'setlocal DisableDelayedExpansion',
        'start "" "%~dp0chat-view-hud.exe" --companion'
    ) -join "`n"
    if ((Test-Path -LiteralPath $hud -PathType Leaf) -and
        (Test-Path -LiteralPath $launcher -PathType Leaf) -and
        [IO.File]::ReadAllText($launcher).Replace("`r`n", "`n").TrimEnd([char[]]"`r`n") -ceq $expectedLauncher) {
        return
    }
    throw "Directory is not a ChatView companion installation: '$Path'."
}


function Test-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    return $principal.IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)
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

function Get-RunningInstalledChatViewProcesses {
    param(
        [Parameter(Mandatory)]
        [string]$ObsRoot,

        [switch]$CompanionMode
    )

    $targetPaths = if ($CompanionMode) {
        @([System.IO.Path]::GetFullPath(
            (Join-Path $ObsRoot 'chat-view-hud.exe')))
    }
    else { @(
        [System.IO.Path]::GetFullPath(
            (Join-Path $ObsRoot 'obs-plugins\64bit\chat-view-hud.exe')),
        [System.IO.Path]::GetFullPath(
            (Join-Path $ObsRoot 'obs-plugins\64bit\chat-view-config.exe')),
        [System.IO.Path]::GetFullPath(
            (Join-Path $ObsRoot 'obs-plugins\64bit\chat-view-diagnostics.exe'))
    ) }

    $running = @()
    foreach ($processName in @(
        'chat-view-hud',
        'chat-view-config',
        'chat-view-diagnostics'
    )) {
        foreach ($process in @(
            Get-Process -Name $processName -ErrorAction SilentlyContinue
        )) {
            try {
                $path = [System.IO.Path]::GetFullPath($process.Path)
            }
            catch {
                continue
            }

            if ($targetPaths -contains $path) {
                $running += [pscustomobject]@{
                    Name = $process.ProcessName
                    Id = $process.Id
                }
            }
        }
    }
    return $running
}

function Assert-InstalledChatViewProcessesStopped {
    param(
        [Parameter(Mandatory)]
        [string]$ObsRoot,

        [int]$WaitMilliseconds = 5000,

        [switch]$CompanionMode
    )

    $deadline = [DateTime]::UtcNow.AddMilliseconds($WaitMilliseconds)
    do {
        $running = @(
            Get-RunningInstalledChatViewProcesses -ObsRoot $ObsRoot -CompanionMode:$CompanionMode
        )
        if ($running.Count -eq 0) {
            return
        }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)

    $details = $running |
        ForEach-Object { "$($_.Name) PID $($_.Id)" }
    if ($CompanionMode) {
        throw "Close the installed ChatView companion before uninstalling: $($details -join ', ')."
    }
    throw "Close the installed ChatView HUD, Control Center, and diagnostics processes before uninstalling: $($details -join ', ')."
}

if ($Companion) {
    $installRoot = [System.IO.Path]::GetFullPath($CompanionPath)
    Assert-CompanionDirectory -Path $installRoot
}
else {
    if (-not (Test-Administrator)) {
        Invoke-ElevatedSelf
    }

    if (Get-Process -Name 'obs64' -ErrorAction SilentlyContinue) {
        throw 'Close OBS Studio before uninstalling ChatView OBS.'
    }

    $installRoot = [System.IO.Path]::GetFullPath($ObsPath)
}
if (-not (Test-Path -LiteralPath $installRoot -PathType Container)) {
    if ($Companion) {
        throw "Companion installation directory was not found: '$installRoot'."
    }
    throw "OBS Studio directory was not found: '$installRoot'."
}

Assert-InstalledChatViewProcessesStopped -ObsRoot $installRoot -CompanionMode:$Companion

$files = if ($Companion) { @(
    (Join-Path $installRoot 'chat-view-hud.exe'),
    (Join-Path $installRoot 'start-companion.cmd'),
    (Join-Path $installRoot 'uninstall.ps1'),
    (Join-Path $installRoot 'uninstall-companion.cmd'),
    (Join-Path $installRoot 'LICENSE'),
    (Join-Path $installRoot 'README.md'),
    (Join-Path $installRoot 'fonts\LICENSE.txt')
) }
else { @(
    (Join-Path $installRoot 'obs-plugins\64bit\chat-view-obs.dll'),
    (Join-Path $installRoot 'obs-plugins\64bit\chat-view-hud.exe'),
    (Join-Path $installRoot 'obs-plugins\64bit\chat-view-config.exe'),
    (Join-Path $installRoot 'obs-plugins\64bit\chat-view-diagnostics.exe'),
    (Join-Path $installRoot 'data\obs-plugins\chat-view-obs\locale\en-US.ini'),
    (Join-Path $installRoot 'data\obs-plugins\chat-view-obs\locale\ko-KR.ini'),
    (Join-Path $installRoot 'data\obs-plugins\chat-view-obs\fonts\LICENSE.txt')
) }

$removalErrors = [System.Collections.Generic.List[string]]::new()
foreach ($file in $files) {
    if (-not (Test-Path -LiteralPath $file)) {
        continue
    }

    try {
        Remove-Item -LiteralPath $file -Force -ErrorAction Stop
    }
    catch {
        $removalErrors.Add("'$file': $($_.Exception.Message)")
    }
}

foreach ($file in $files) {
    if (Test-Path -LiteralPath $file) {
        $removalErrors.Add("'$file' still exists after removal")
    }
}

$dataRoot = if ($Companion) { $installRoot }
    else { Join-Path $installRoot 'data\obs-plugins\chat-view-obs' }
if (-not $Companion) {
    $localeDirectory = Join-Path $dataRoot 'locale'
    if ((Test-Path -LiteralPath $localeDirectory -PathType Container) -and
        -not (Get-ChildItem -LiteralPath $localeDirectory -Force |
            Select-Object -First 1)) {
        Remove-Item -LiteralPath $localeDirectory -Force
    }
}
$fontDirectory = Join-Path $dataRoot 'fonts'
if ((Test-Path -LiteralPath $fontDirectory -PathType Container) -and
    -not (Get-ChildItem -LiteralPath $fontDirectory -Force |
        Select-Object -First 1)) {
    Remove-Item -LiteralPath $fontDirectory -Force
}
if ((Test-Path -LiteralPath $dataRoot -PathType Container) -and
    -not (Get-ChildItem -LiteralPath $dataRoot -Force |
        Select-Object -First 1)) {
    Remove-Item -LiteralPath $dataRoot -Force
}

if ($removalErrors.Count -gt 0) {
    if ($Companion) {
        throw "ChatView companion could not be removed completely: $($removalErrors -join '; ')"
    }
    throw "ChatView OBS could not be removed completely: $($removalErrors -join '; ')"
}

if ($Companion) {
    Write-Host "ChatView companion was removed from '$installRoot'."
}
else {
    Write-Host "ChatView OBS was removed from '$installRoot'."
}
Write-Host 'User settings in %LOCALAPPDATA%\ChatView were left intact.'
