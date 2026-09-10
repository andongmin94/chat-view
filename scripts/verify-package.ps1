[CmdletBinding()]
param(
    [Parameter()]
    [ValidateNotNullOrEmpty()]
    [string]$PackageRoot = $PSScriptRoot,

    [Parameter()]
    [ValidatePattern('^[0-9a-fA-F]{40}$')]
    [string]$ExpectedCommit
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$manifestFileName = 'package-manifest.json'
$reservedDeviceNamePattern =
    '^(?i:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$'

function Assert-SafeManifestPath {
    param(
        [Parameter(Mandatory)]
        [string]$Path
    )

    if ($Path.Length -eq 0 -or
        $Path.Length -gt 512 -or
        $Path -notmatch '^[A-Za-z0-9._/-]+$' -or
        $Path.StartsWith('/') -or
        $Path.EndsWith('/') -or
        $Path.Contains('//') -or
        $Path.Contains('\') -or
        [System.IO.Path]::IsPathRooted($Path)) {
        throw "Unsafe package manifest path: '$Path'."
    }

    foreach ($segment in $Path.Split('/')) {
        if ($segment.Length -eq 0 -or
            $segment -eq '.' -or
            $segment -eq '..' -or
            $segment.EndsWith('.') -or
            $segment.EndsWith(' ')) {
            throw "Unsafe package manifest path segment in '$Path'."
        }

        $deviceName = $segment.Split('.')[0]
        if ($deviceName -match $reservedDeviceNamePattern) {
            throw "Reserved Windows device name in package path '$Path'."
        }
    }
}

function Get-RelativePackagePath {
    param(
        [Parameter(Mandatory)]
        [string]$FullPath,

        [Parameter(Mandatory)]
        [string]$RootPrefix
    )

    $normalized = [System.IO.Path]::GetFullPath($FullPath)
    if (-not $normalized.StartsWith(
            $RootPrefix,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Package item escaped the package root: '$normalized'."
    }

    return $normalized.Substring($RootPrefix.Length).Replace('\', '/')
}

function Get-PackageFiles {
    param(
        [Parameter(Mandatory)]
        [System.IO.DirectoryInfo]$Root
    )

    $queue = [System.Collections.Generic.Queue[System.IO.DirectoryInfo]]::new()
    $files = [System.Collections.Generic.List[System.IO.FileInfo]]::new()
    $queue.Enqueue($Root)

    while ($queue.Count -gt 0) {
        $directory = $queue.Dequeue()
        foreach ($item in @(Get-ChildItem `
            -LiteralPath $directory.FullName `
            -Force)) {
            if (($item.Attributes -band
                    [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Reparse points are not allowed in the package: '$($item.FullName)'."
            }

            if ($item.PSIsContainer) {
                $queue.Enqueue([System.IO.DirectoryInfo]$item)
            }
            else {
                $files.Add([System.IO.FileInfo]$item)
            }
        }
    }

    return $files
}

$root = [System.IO.Path]::GetFullPath($PackageRoot)
if (-not (Test-Path -LiteralPath $root -PathType Container)) {
    throw "Package root was not found: '$root'."
}

$rootItem = Get-Item -LiteralPath $root -Force
if (($rootItem.Attributes -band
        [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
    throw "The package root may not be a reparse point: '$root'."
}

$trimCharacters = [char[]]@(
    [System.IO.Path]::DirectorySeparatorChar,
    [System.IO.Path]::AltDirectorySeparatorChar
)
$rootPrefix = $root.TrimEnd($trimCharacters) +
    [System.IO.Path]::DirectorySeparatorChar
$manifestPath = Join-Path $root $manifestFileName
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw "Package manifest was not found: '$manifestPath'."
}

$manifestItem = Get-Item -LiteralPath $manifestPath -Force
if (($manifestItem.Attributes -band
        [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
    throw 'The package manifest may not be a reparse point.'
}

$manifest = Get-Content `
    -LiteralPath $manifestPath `
    -Raw `
    -Encoding UTF8 | ConvertFrom-Json
if ($null -eq $manifest -or $manifest.schemaVersion -ne 1) {
    throw 'Unsupported or malformed package manifest schema.'
}

$manifestCommit = [string]$manifest.commit
if ($manifestCommit -notmatch '^[0-9a-f]{40}$') {
    throw 'The package manifest commit is malformed.'
}
if ($ExpectedCommit -and
    $manifestCommit -ne $ExpectedCommit.ToLowerInvariant()) {
    throw "Package manifest commit mismatch: expected '$ExpectedCommit', found '$manifestCommit'."
}

$entries = @($manifest.files)
if ($entries.Count -eq 0) {
    throw 'The package manifest contains no files.'
}

$expectedPaths =
    [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase)

foreach ($entry in $entries) {
    $relativePath = [string]$entry.path
    $expectedHash = [string]$entry.sha256
    Assert-SafeManifestPath -Path $relativePath

    if (-not $expectedPaths.Add($relativePath)) {
        throw "Duplicate package manifest path: '$relativePath'."
    }
    if ($relativePath -ieq $manifestFileName) {
        throw 'The package manifest may not hash itself.'
    }
    if ($expectedHash -notmatch '^[0-9a-f]{64}$') {
        throw "Malformed SHA-256 for package file '$relativePath'."
    }

    $segments = $relativePath.Split('/')
    $nativeRelativePath = [string]::Join(
        [string][System.IO.Path]::DirectorySeparatorChar,
        $segments)
    $fullPath = [System.IO.Path]::GetFullPath(
        (Join-Path $root $nativeRelativePath))
    if (-not $fullPath.StartsWith(
            $rootPrefix,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Package manifest path escaped the package root: '$relativePath'."
    }

    $currentPath = $root
    foreach ($segment in $segments) {
        $currentPath = Join-Path $currentPath $segment
        $item = Get-Item -LiteralPath $currentPath -Force
        if (($item.Attributes -band
                [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Reparse points are not allowed in package path '$relativePath'."
        }
    }

    if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
        throw "Package file is missing: '$relativePath'."
    }

    $actualHash = (Get-FileHash `
        -Algorithm SHA256 `
        -LiteralPath $fullPath).Hash.ToLowerInvariant()
    if ($actualHash -ne $expectedHash) {
        throw "SHA-256 mismatch for package file '$relativePath'."
    }
}

$actualPaths =
    [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase)
foreach ($file in Get-PackageFiles -Root $rootItem) {
    $relativePath = Get-RelativePackagePath `
        -FullPath $file.FullName `
        -RootPrefix $rootPrefix
    if ($relativePath -ieq $manifestFileName) {
        continue
    }

    Assert-SafeManifestPath -Path $relativePath
    if (-not $actualPaths.Add($relativePath)) {
        throw "Duplicate package path after Windows case folding: '$relativePath'."
    }
}

if ($actualPaths.Count -ne $expectedPaths.Count) {
    throw "Package file count mismatch: manifest $($expectedPaths.Count), package $($actualPaths.Count)."
}
foreach ($relativePath in $expectedPaths) {
    if (-not $actualPaths.Contains($relativePath)) {
        throw "Package manifest references an unavailable file: '$relativePath'."
    }
}
foreach ($relativePath in $actualPaths) {
    if (-not $expectedPaths.Contains($relativePath)) {
        throw "Package contains an unlisted file: '$relativePath'."
    }
}

Write-Host "Verified package manifest for $($actualPaths.Count) files at commit $manifestCommit."
