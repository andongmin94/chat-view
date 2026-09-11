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
        [string]$ObsRoot
    )

    $targetPaths = @(
        [System.IO.Path]::GetFullPath(
            (Join-Path $ObsRoot 'obs-plugins\64bit\chat-view-hud.exe')),
        [System.IO.Path]::GetFullPath(
            (Join-Path $ObsRoot 'obs-plugins\64bit\chat-view-config.exe')),
        [System.IO.Path]::GetFullPath(
            (Join-Path $ObsRoot 'obs-plugins\64bit\chat-view-diagnostics.exe'))
    )

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

        [int]$WaitMilliseconds = 5000
    )

    $deadline = [DateTime]::UtcNow.AddMilliseconds($WaitMilliseconds)
    do {
        $running = @(
            Get-RunningInstalledChatViewProcesses -ObsRoot $ObsRoot
        )
        if ($running.Count -eq 0) {
            return
        }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)

    $details = $running |
        ForEach-Object { "$($_.Name) PID $($_.Id)" }
    throw "Close the installed ChatView HUD, Control Center, and diagnostics processes before uninstalling: $($details -join ', ')."
}

if (-not (Test-Administrator)) {
    Invoke-ElevatedSelf
}

if (Get-Process -Name 'obs64' -ErrorAction SilentlyContinue) {
    throw 'Close OBS Studio before uninstalling ChatView OBS.'
}

$obsRoot = [System.IO.Path]::GetFullPath($ObsPath)
if (-not (Test-Path -LiteralPath $obsRoot -PathType Container)) {
    throw "OBS Studio directory was not found: '$obsRoot'."
}

Assert-InstalledChatViewProcessesStopped -ObsRoot $obsRoot

$files = @(
    (Join-Path $obsRoot 'obs-plugins\64bit\chat-view-obs.dll'),
    (Join-Path $obsRoot 'obs-plugins\64bit\chat-view-hud.exe'),
    (Join-Path $obsRoot 'obs-plugins\64bit\chat-view-config.exe'),
    (Join-Path $obsRoot 'obs-plugins\64bit\chat-view-diagnostics.exe'),
    (Join-Path $obsRoot 'data\obs-plugins\chat-view-obs\locale\en-US.ini'),
    (Join-Path $obsRoot 'data\obs-plugins\chat-view-obs\locale\ko-KR.ini')
)

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

$dataRoot = Join-Path $obsRoot 'data\obs-plugins\chat-view-obs'
$localeDirectory = Join-Path $dataRoot 'locale'
if ((Test-Path -LiteralPath $localeDirectory -PathType Container) -and
    -not (Get-ChildItem -LiteralPath $localeDirectory -Force |
        Select-Object -First 1)) {
    Remove-Item -LiteralPath $localeDirectory -Force
}
if ((Test-Path -LiteralPath $dataRoot -PathType Container) -and
    -not (Get-ChildItem -LiteralPath $dataRoot -Force |
        Select-Object -First 1)) {
    Remove-Item -LiteralPath $dataRoot -Force
}

if ($removalErrors.Count -gt 0) {
    throw "ChatView OBS could not be removed completely: $($removalErrors -join '; ')"
}

Write-Host "ChatView OBS was removed from '$obsRoot'."
Write-Host 'User settings in %LOCALAPPDATA%\ChatView were left intact.'
