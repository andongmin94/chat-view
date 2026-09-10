[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateNotNullOrEmpty()]
    [string]$PackageRoot,

    [Parameter(Mandatory)]
    [ValidatePattern('^[0-9a-fA-F]{40}$')]
    [string]$Commit
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$manifestFileName = 'package-manifest.json'
$root = [System.IO.Path]::GetFullPath($PackageRoot)
if (-not (Test-Path -LiteralPath $root -PathType Container)) {
    throw "Package root was not found: '$root'."
}

$manifestPath = Join-Path $root $manifestFileName
Remove-Item -LiteralPath $manifestPath -Force -ErrorAction SilentlyContinue

$trimCharacters = [char[]]@(
    [System.IO.Path]::DirectorySeparatorChar,
    [System.IO.Path]::AltDirectorySeparatorChar
)
$rootPrefix = $root.TrimEnd($trimCharacters) +
    [System.IO.Path]::DirectorySeparatorChar

$entries = @(
    Get-ChildItem -LiteralPath $root -File -Recurse -Force |
        ForEach-Object {
            $fullPath = [System.IO.Path]::GetFullPath($_.FullName)
            if (-not $fullPath.StartsWith(
                    $rootPrefix,
                    [System.StringComparison]::OrdinalIgnoreCase)) {
                throw "Package file escaped the package root: '$fullPath'."
            }

            $relativePath =
                $fullPath.Substring($rootPrefix.Length).Replace('\', '/')
            [ordered]@{
                path = $relativePath
                sha256 = (Get-FileHash `
                    -Algorithm SHA256 `
                    -LiteralPath $fullPath).Hash.ToLowerInvariant()
            }
        } |
        Sort-Object { $_.path }
)

if ($entries.Count -eq 0) {
    throw 'The package root contains no files.'
}

$manifest = [ordered]@{
    schemaVersion = 1
    commit = $Commit.ToLowerInvariant()
    files = $entries
}
$json = $manifest | ConvertTo-Json -Depth 4
[System.IO.File]::WriteAllText(
    $manifestPath,
    $json + [Environment]::NewLine,
    [System.Text.UTF8Encoding]::new($false))

Write-Host "Created package manifest for $($entries.Count) files at '$manifestPath'."
