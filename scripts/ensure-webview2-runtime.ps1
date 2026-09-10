[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$clientId = '{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}'
$minimumRuntimeVersion = [System.Version]'151.0.4129.50'
$bootstrapperUrl = 'https://go.microsoft.com/fwlink/p/?LinkId=2124703'

function Get-WebView2RuntimeVersion {
    $registryPaths = @(
        "HKLM:\SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\$clientId",
        "HKLM:\SOFTWARE\Microsoft\EdgeUpdate\Clients\$clientId",
        "HKCU:\Software\Microsoft\EdgeUpdate\Clients\$clientId"
    )

    $versions = foreach ($registryPath in $registryPaths) {
        $value = Get-ItemPropertyValue `
            -LiteralPath $registryPath `
            -Name 'pv' `
            -ErrorAction SilentlyContinue
        if (-not $value -or $value -eq '0.0.0.0') {
            continue
        }

        try {
            $version = [System.Version]$value
            if ($version -gt [System.Version]'0.0.0.0') {
                $version
            }
        }
        catch {
            continue
        }
    }

    return $versions | Sort-Object -Descending | Select-Object -First 1
}

$installedVersion = Get-WebView2RuntimeVersion
if ($installedVersion -and $installedVersion -ge $minimumRuntimeVersion) {
    Write-Host "Microsoft Edge WebView2 Runtime $installedVersion is already installed."
    exit 0
}

$installerPath = Join-Path `
    ([System.IO.Path]::GetTempPath()) `
    "MicrosoftEdgeWebview2Setup-$PID.exe"

try {
    if ($installedVersion) {
        Write-Host "WebView2 Runtime $installedVersion is older than the required $minimumRuntimeVersion. Updating the Evergreen Runtime..."
    }
    else {
        Write-Host "Microsoft Edge WebView2 Runtime $minimumRuntimeVersion or newer was not found. Installing the Evergreen Runtime..."
    }

    Invoke-WebRequest -Uri $bootstrapperUrl -OutFile $installerPath

    $signature = Get-AuthenticodeSignature -LiteralPath $installerPath
    if ($signature.Status -ne [System.Management.Automation.SignatureStatus]::Valid -or
        $null -eq $signature.SignerCertificate -or
        $signature.SignerCertificate.Subject -notmatch 'Microsoft') {
        throw 'The downloaded WebView2 bootstrapper does not have a valid Microsoft signature.'
    }

    $process = Start-Process `
        -FilePath $installerPath `
        -ArgumentList '/silent', '/install' `
        -Wait `
        -PassThru

    for ($attempt = 0; $attempt -lt 60; $attempt++) {
        $installedVersion = Get-WebView2RuntimeVersion
        if ($installedVersion -and $installedVersion -ge $minimumRuntimeVersion) {
            break
        }
        Start-Sleep -Seconds 1
    }

    if (-not $installedVersion -or $installedVersion -lt $minimumRuntimeVersion) {
        $detected = if ($installedVersion) { $installedVersion } else { 'none' }
        throw "WebView2 Runtime installation did not reach the required $minimumRuntimeVersion (detected $detected, bootstrapper exit code $($process.ExitCode))."
    }

    Write-Host "Microsoft Edge WebView2 Runtime $installedVersion is ready."
}
finally {
    Remove-Item -LiteralPath $installerPath -Force -ErrorAction SilentlyContinue
}
