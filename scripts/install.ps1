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


$SupportedObsVersion = '32.2.2'
$MinimumWindowsBuild = 19041
$packageVerifier = Join-Path $PSScriptRoot 'verify-package.ps1'
if (-not (Test-Path -LiteralPath $packageVerifier -PathType Leaf)) {
    throw "Package verifier is missing: '$packageVerifier'."
}
& $packageVerifier -PackageRoot $PSScriptRoot

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
        throw "Close the installed ChatView companion before continuing: $($details -join ', ')."
    }
    throw "Close the installed ChatView HUD, Control Center, and diagnostics processes before continuing: $($details -join ', ')."
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
                throw 'backup file is missing'
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

$packageFiles = if ($Companion) { @(
    @{
        Source = Join-Path $PSScriptRoot 'obs-plugins\64bit\chat-view-hud.exe'
        RelativeDestination = 'chat-view-hud.exe'
    },
    @{
        Source = Join-Path $PSScriptRoot 'start-companion.cmd'
        RelativeDestination = 'start-companion.cmd'
    },
    @{
        Source = Join-Path $PSScriptRoot 'uninstall.ps1'
        RelativeDestination = 'uninstall.ps1'
    },
    @{
        Source = Join-Path $PSScriptRoot 'uninstall-companion.cmd'
        RelativeDestination = 'uninstall-companion.cmd'
    },
    @{
        Source = Join-Path $PSScriptRoot 'LICENSE'
        RelativeDestination = 'LICENSE'
    },
    @{
        Source = Join-Path $PSScriptRoot 'README.md'
        RelativeDestination = 'README.md'
    },
    @{
        Source = Join-Path $PSScriptRoot 'data\obs-plugins\chat-view-obs\fonts\LICENSE.txt'
        RelativeDestination = 'fonts\LICENSE.txt'
    }
) }
else { @(
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
        Source = Join-Path $PSScriptRoot 'obs-plugins\64bit\chat-view-diagnostics.exe'
        RelativeDestination = 'obs-plugins\64bit\chat-view-diagnostics.exe'
    },
    @{
        Source = Join-Path $PSScriptRoot 'data\obs-plugins\chat-view-obs\locale\en-US.ini'
        RelativeDestination = 'data\obs-plugins\chat-view-obs\locale\en-US.ini'
    },
    @{
        Source = Join-Path $PSScriptRoot 'data\obs-plugins\chat-view-obs\locale\ko-KR.ini'
        RelativeDestination = 'data\obs-plugins\chat-view-obs\locale\ko-KR.ini'
    },
    @{
        Source = Join-Path $PSScriptRoot 'data\obs-plugins\chat-view-obs\fonts\LICENSE.txt'
        RelativeDestination = 'data\obs-plugins\chat-view-obs\fonts\LICENSE.txt'
    }
) }

$runtimeInstaller =
    Join-Path $PSScriptRoot 'ensure-webview2-runtime.ps1'
$selfTest = Join-Path $PSScriptRoot 'chat-view-self-test.exe'
$pluginPreflight =
    Join-Path $PSScriptRoot 'chat-view-plugin-preflight.exe'
$requiredPackageFiles = @(
    $runtimeInstaller,
    $selfTest
) + @($packageFiles | ForEach-Object { $_.Source })
if (-not $Companion) { $requiredPackageFiles += $pluginPreflight }

foreach ($file in $requiredPackageFiles) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
        throw "Package file is missing: $file"
    }
}

if ($Companion) {
    Assert-SupportedWindows
    $installRoot = [System.IO.Path]::GetFullPath($CompanionPath)
    Assert-CompanionDirectory -Path $installRoot -AllowNew
}
else {
    if (-not (Test-Administrator)) {
        Invoke-ElevatedSelf
    }

    Assert-SupportedWindows

    if (Get-Process -Name 'obs64' -ErrorAction SilentlyContinue) {
        throw 'Close OBS Studio before installing ChatView OBS.'
    }

    $obsRoot = Resolve-ObsRoot -Path $ObsPath
    $installRoot = $obsRoot
}
Assert-InstalledChatViewProcessesStopped -ObsRoot $installRoot -CompanionMode:$Companion
& $runtimeInstaller

Write-Host 'Running the local transparent-HUD preflight...'
Invoke-NativePreflight `
    -Executable $selfTest `
    -Arguments @(
        (Join-Path $PSScriptRoot 'obs-plugins\64bit\chat-view-hud.exe')
    ) `
    -FailureMessage 'The ChatView HUD preflight failed on this PC'

if (-not $Companion) {
    Write-Host 'Checking the plugin against the installed OBS runtime...'
    Invoke-NativePreflight `
        -Executable $pluginPreflight `
        -Arguments @(
            (Join-Path $PSScriptRoot 'obs-plugins\64bit\chat-view-obs.dll'),
            (Join-Path $obsRoot 'bin\64bit')
        ) `
        -FailureMessage 'The ChatView plugin could not be loaded against this OBS installation'

    if (Get-Process -Name 'obs64' -ErrorAction SilentlyContinue) {
        throw 'OBS Studio started during installation. Close it and run the installer again.'
    }
}
Assert-InstalledChatViewProcessesStopped -ObsRoot $installRoot -CompanionMode:$Companion

$files = foreach ($file in $packageFiles) {
    [pscustomobject]@{
        Source = $file.Source
        Destination = Join-Path $installRoot $file.RelativeDestination
    }
}
if ($Companion) { Assert-CompanionDirectory -Path $installRoot -AllowNew }
Copy-ChatViewFilesTransactionally -Files $files

if ($Companion) {
    Write-Host "ChatView companion was installed to '$installRoot'."
    Write-Host 'Run start-companion.cmd in that folder. OBS is not required on this PC.'
}
else {
    Write-Host "ChatView OBS $SupportedObsVersion-compatible build was installed to '$obsRoot'."
    Write-Host 'All package and local preflight checks passed. Start OBS Studio, then open Tools > ChatView Settings.'
}
