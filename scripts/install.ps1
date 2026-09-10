[CmdletBinding()]
param(
    [Parameter()]
    [ValidateNotNullOrEmpty()]
    [string]$ObsPath = (Join-Path $env:ProgramFiles 'obs-studio')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$SupportedObsVersion = '32.2.2'
$MinimumWindowsBuild = 19041

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

function Assert-SupportedWindows {
    if (-not [Environment]::Is64BitOperatingSystem) {
        throw 'ChatView OBS requires 64-bit Windows.'
    }

    $version = [Environment]::OSVersion.Version
    if ($version.Major -lt 10 -or $version.Build -lt $MinimumWindowsBuild) {
        throw "ChatView OBS requires Windows 10 build $MinimumWindowsBuild or newer. Detected $version."
    }
}

function Resolve-ObsRoot {
    param([string]$Path)

    $root = [System.IO.Path]::GetFullPath($Path)
    $executable = Join-Path $root 'bin\64bit\obs64.exe'
    if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
        throw "OBS Studio was not found at '$root'. Expected '$executable'."
    }

    $versionInfo =
        [System.Diagnostics.FileVersionInfo]::GetVersionInfo($executable)
    $versionText = "$($versionInfo.ProductVersion) $($versionInfo.FileVersion)"
    $escapedVersion = [regex]::Escape($SupportedObsVersion)
    if ($versionText -notmatch "(^|[^0-9])$escapedVersion([^0-9]|$)") {
        throw "ChatView OBS currently supports OBS Studio $SupportedObsVersion x64 only. Detected '$versionText'."
    }

    return $root
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
            (Join-Path $ObsRoot 'obs-plugins\64bit\chat-view-config.exe'))
    )

    $running = @()
    foreach ($processName in @('chat-view-hud', 'chat-view-config')) {
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
                    Path = $path
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
    throw "Close the installed ChatView HUD and settings processes before continuing: $($details -join ', ')."
}

function Invoke-NativePreflight {
    param(
        [Parameter(Mandatory)]
        [string]$Executable,

        [Parameter(Mandatory)]
        [string[]]$Arguments,

        [Parameter(Mandatory)]
        [string]$FailureMessage
    )

    & $Executable @Arguments
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        throw "$FailureMessage (exit code $exitCode). No ChatView files were installed."
    }
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
    $failure = $null

    try {
        foreach ($file in $Files) {
            $destinationDirectory = Split-Path $file.Destination -Parent
            New-Item `
                -ItemType Directory `
                -Path $destinationDirectory `
                -Force | Out-Null

            $stagePath =
                "$($file.Destination).chatview-new-$transactionId"
            $stage = [pscustomobject]@{
                Path = $stagePath
                Destination = $file.Destination
                SourceHash = $null
            }
            $staged += $stage

            Copy-Item `
                -LiteralPath $file.Source `
                -Destination $stagePath `
                -Force

            $stage.SourceHash =
                (Get-FileHash `
                    -Algorithm SHA256 `
                    -LiteralPath $file.Source).Hash
            $stageHash =
                (Get-FileHash `
                    -Algorithm SHA256 `
                    -LiteralPath $stagePath).Hash
            if ($stage.SourceHash -ne $stageHash) {
                throw "Staged file verification failed: '$($file.Source)'."
            }
        }

        foreach ($file in $staged) {
            if (Test-Path -LiteralPath $file.Destination -PathType Leaf) {
                $backupPath =
                    "$($file.Destination).chatview-old-$transactionId"
                Move-Item `
                    -LiteralPath $file.Destination `
                    -Destination $backupPath `
                    -Force
                $backups += [pscustomobject]@{
                    Path = $backupPath
                    Destination = $file.Destination
                }
            }

            Move-Item `
                -LiteralPath $file.Path `
                -Destination $file.Destination `
                -Force
            $installed += $file.Destination

            $destinationHash =
                (Get-FileHash `
                    -Algorithm SHA256 `
                    -LiteralPath $file.Destination).Hash
            if ($destinationHash -ne $file.SourceHash) {
                throw "Installed file verification failed: '$($file.Destination)'."
            }
        }

        $completed = $true
    }
    catch {
        $failure = $_
    }

    if ($completed) {
        foreach ($backup in $backups) {
            try {
                Remove-Item `
                    -LiteralPath $backup.Path `
                    -Force `
                    -ErrorAction Stop
            }
            catch {
                Write-Warning "Installed successfully, but an old backup could not be removed: '$($backup.Path)'."
            }
        }
        foreach ($file in $staged) {
            if (Test-Path -LiteralPath $file.Path) {
                try {
                    Remove-Item `
                        -LiteralPath $file.Path `
                        -Force `
                        -ErrorAction Stop
                }
                catch {
                    Write-Warning "Installed successfully, but a staging file could not be removed: '$($file.Path)'."
                }
            }
        }
        return
    }

    $rollbackErrors =
        [System.Collections.Generic.List[string]]::new()

    for ($index = $installed.Count - 1; $index -ge 0; --$index) {
        $destination = $installed[$index]
        try {
            if (Test-Path -LiteralPath $destination) {
                Remove-Item `
                    -LiteralPath $destination `
                    -Force `
                    -ErrorAction Stop
            }
        }
        catch {
            $rollbackErrors.Add(
                "could not remove new file '$destination': $($_.Exception.Message)")
        }
    }

    for ($index = $backups.Count - 1; $index -ge 0; --$index) {
        $backup = $backups[$index]
        try {
            if (-not (Test-Path -LiteralPath $backup.Path -PathType Leaf)) {
                throw "backup file is missing"
            }
            Move-Item `
                -LiteralPath $backup.Path `
                -Destination $backup.Destination `
                -Force `
                -ErrorAction Stop
        }
        catch {
            $rollbackErrors.Add(
                "could not restore '$($backup.Destination)' from '$($backup.Path)': $($_.Exception.Message)")
        }
    }

    foreach ($file in $staged) {
        try {
            if (Test-Path -LiteralPath $file.Path) {
                Remove-Item `
                    -LiteralPath $file.Path `
                    -Force `
                    -ErrorAction Stop
            }
        }
        catch {
            $rollbackErrors.Add(
                "could not remove staging file '$($file.Path)': $($_.Exception.Message)")
        }
    }

    if ($rollbackErrors.Count -gt 0) {
        $originalMessage =
            if ($null -ne $failure) {
                $failure.Exception.Message
            }
            else {
                'unknown installation failure'
            }
        throw "Installation failed: $originalMessage Rollback was incomplete: $($rollbackErrors -join '; '). Backup files were preserved where possible."
    }

    if ($null -ne $failure) {
        throw $failure
    }
    throw 'Installation failed for an unknown reason.'
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

$runtimeInstaller =
    Join-Path $PSScriptRoot 'ensure-webview2-runtime.ps1'
$selfTest = Join-Path $PSScriptRoot 'chat-view-self-test.exe'
$pluginPreflight =
    Join-Path $PSScriptRoot 'chat-view-plugin-preflight.exe'
$requiredPackageFiles = @(
    $runtimeInstaller,
    $selfTest,
    $pluginPreflight
) + @($packageFiles | ForEach-Object { $_.Source })

foreach ($file in $requiredPackageFiles) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
        throw "Package file is missing: $file"
    }
}

if (-not (Test-Administrator)) {
    Invoke-ElevatedSelf
}

Assert-SupportedWindows

if (Get-Process -Name 'obs64' -ErrorAction SilentlyContinue) {
    throw 'Close OBS Studio before installing ChatView OBS.'
}

$obsRoot = Resolve-ObsRoot -Path $ObsPath
Assert-InstalledChatViewProcessesStopped -ObsRoot $obsRoot
& $runtimeInstaller

Write-Host 'Running the local transparent-HUD preflight...'
Invoke-NativePreflight `
    -Executable $selfTest `
    -Arguments @(
        (Join-Path $PSScriptRoot 'obs-plugins\64bit\chat-view-hud.exe')
    ) `
    -FailureMessage 'The ChatView HUD preflight failed on this PC'

Write-Host 'Checking the plugin against the installed OBS runtime...'
Invoke-NativePreflight `
    -Executable $pluginPreflight `
    -Arguments @(
        (Join-Path $PSScriptRoot 'obs-plugins\64bit\chat-view-obs.dll'),
        (Join-Path $obsRoot 'bin\64bit')
    ) `
    -FailureMessage 'The ChatView plugin could not be loaded against this OBS installation'

$files = foreach ($file in $packageFiles) {
    [pscustomobject]@{
        Source = $file.Source
        Destination = Join-Path $obsRoot $file.RelativeDestination
    }
}
Copy-ChatViewFilesTransactionally -Files $files

Write-Host "ChatView OBS $SupportedObsVersion-compatible build was installed to '$obsRoot'."
Write-Host 'All local preflight checks passed. Start OBS Studio, then open Tools > ChatView Settings.'
