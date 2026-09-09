[CmdletBinding()]
param(
    [Parameter()]
    [ValidateNotNullOrEmpty()]
    [string]$ObsPath = (Join-Path $env:ProgramFiles 'obs-studio'),

    [Parameter()]
    [switch]$SkipObsVersionCheck
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$SupportedObsVersion = '32.2.2'

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
    if ($SkipObsVersionCheck) {
        $arguments += '-SkipObsVersionCheck'
    }

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

    if (-not $SkipObsVersionCheck) {
        $versionInfo = [System.Diagnostics.FileVersionInfo]::GetVersionInfo($executable)
        $versionText = "$($versionInfo.ProductVersion) $($versionInfo.FileVersion)"
        $escapedVersion = [regex]::Escape($SupportedObsVersion)
        if ($versionText -notmatch "(^|[^0-9])$escapedVersion([^0-9]|$)") {
            throw "ChatView OBS currently supports OBS Studio $SupportedObsVersion x64 only. Detected '$versionText'."
        }
    }

    return $root
}

function Copy-ChatViewFilesTransactionally {
    param(
        [Parameter(Mandatory)]
        [array]$Files
    )

    $transactionId = [guid]::NewGuid().ToString('N')
    $staged = @()
    $backups = @()
    $installed = @()
    $completed = $false

    try {
        foreach ($file in $Files) {
            $destinationDirectory = Split-Path $file.Destination -Parent
            New-Item -ItemType Directory -Path $destinationDirectory -Force | Out-Null

            $stagePath = "$($file.Destination).chatview-new-$transactionId"
            Copy-Item -LiteralPath $file.Source -Destination $stagePath -Force

            $sourceHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $file.Source).Hash
            $stageHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $stagePath).Hash
            if ($sourceHash -ne $stageHash) {
                throw "Staged file verification failed: '$($file.Source)'."
            }

            $staged += [pscustomobject]@{
                Path = $stagePath
                Destination = $file.Destination
            }
        }

        foreach ($file in $staged) {
            if (Test-Path $file.Destination -PathType Leaf) {
                $backupPath = "$($file.Destination).chatview-old-$transactionId"
                Move-Item -LiteralPath $file.Destination -Destination $backupPath -Force
                $backups += [pscustomobject]@{
                    Path = $backupPath
                    Destination = $file.Destination
                }
            }

            Move-Item -LiteralPath $file.Path -Destination $file.Destination -Force
            $installed += $file.Destination
        }

        $completed = $true
    }
    finally {
        if (-not $completed) {
            foreach ($destination in $installed) {
                Remove-Item -LiteralPath $destination -Force -ErrorAction SilentlyContinue
            }
            foreach ($backup in $backups) {
                if (Test-Path $backup.Path -PathType Leaf) {
                    Move-Item `
                        -LiteralPath $backup.Path `
                        -Destination $backup.Destination `
                        -Force `
                        -ErrorAction SilentlyContinue
                }
            }
        }

        foreach ($file in $staged) {
            Remove-Item -LiteralPath $file.Path -Force -ErrorAction SilentlyContinue
        }
        foreach ($backup in $backups) {
            Remove-Item -LiteralPath $backup.Path -Force -ErrorAction SilentlyContinue
        }
    }
}

$packageFiles = @(
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

foreach ($file in $packageFiles) {
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

$files = foreach ($file in $packageFiles) {
    [pscustomobject]@{
        Source = $file.Source
        Destination = Join-Path $obsRoot $file.RelativeDestination
    }
}
Copy-ChatViewFilesTransactionally -Files $files

Write-Host "ChatView OBS $SupportedObsVersion-compatible build was installed to '$obsRoot'."
Write-Host 'Start OBS Studio, then open Tools > ChatView Settings.'
