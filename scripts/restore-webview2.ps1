[CmdletBinding()]
param(
    [Parameter()]
    [ValidateNotNullOrEmpty()]
    [string]$Version = '1.0.4129.50',

    [Parameter()]
    [ValidateNotNullOrEmpty()]
    [string]$Destination = (Join-Path $PSScriptRoot '..\.ci\webview2')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$destinationPath = [System.IO.Path]::GetFullPath($Destination)
$header = Join-Path $destinationPath 'build\native\include\WebView2.h'
$library = Join-Path $destinationPath 'build\native\x64\WebView2LoaderStatic.lib'
if ((Test-Path $header -PathType Leaf) -and (Test-Path $library -PathType Leaf)) {
    Write-Host "WebView2 SDK $Version is already available at '$destinationPath'."
    Write-Output $destinationPath
    exit 0
}

$downloadDirectory = Split-Path $destinationPath -Parent
New-Item -ItemType Directory -Path $downloadDirectory -Force | Out-Null
$packagePath = Join-Path $downloadDirectory "Microsoft.Web.WebView2.$Version.nupkg"
$archivePath = "$packagePath.zip"
$packageUrl = "https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/$Version/microsoft.web.webview2.$Version.nupkg"

Invoke-WebRequest -Uri $packageUrl -OutFile $packagePath
Copy-Item -LiteralPath $packagePath -Destination $archivePath -Force
Remove-Item -LiteralPath $destinationPath -Recurse -Force -ErrorAction SilentlyContinue
Expand-Archive -LiteralPath $archivePath -DestinationPath $destinationPath -Force
Remove-Item -LiteralPath $packagePath, $archivePath -Force

if (-not (Test-Path $header -PathType Leaf) -or -not (Test-Path $library -PathType Leaf)) {
    throw "The downloaded WebView2 package did not contain the expected native x64 SDK files."
}

Write-Host "Restored WebView2 SDK $Version to '$destinationPath'."
Write-Output $destinationPath
