[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$clientId = '{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}'
$bootstrapperUrl = 'https://go.microsoft.com/fwlink/p/?LinkId=2124703'

function Get-WebView2RuntimeVersion {
    $registryPaths = @(
        "HKLM:\SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\$clientId",
        "HKLM:\SOFTWARE\Microsoft\EdgeUpdate\Clients\$clientId",
        "HKCU:\Software\Microsoft\EdgeUpdate\Clients\$clientId"
    )

    foreach ($registryPath in $registryPaths) {
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
                return $value
            }
        }
        catch {
            continue
        }
    }

    return $null
}

$installedVersion = Get-WebView2RuntimeVersion
if ($installedVersion) {
    Write-Host "Microsoft Edge WebView2 Runtime $installedVersion is already installed."
    exit 0
}

$installerPath = Join-Path `
    ([System.IO.Path]::GetTempPath()) `
    "MicrosoftEdgeWebview2Setup-$PID.exe"

try {
    Write-Host 'Microsoft Edge WebView2 Runtime was not found. Installing the Evergreen Runtime...'
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

    for ($attempt = 0; $attempt -lt 30; $attempt++) {
        $installedVersion = Get-WebView2RuntimeVersion
        if ($installedVersion) {
            break
        }
        Start-Sleep -Seconds 1
    }

    if (-not $installedVersion) {
        throw "WebView2 Runtime installation did not complete successfully (bootstrapper exit code $($process.ExitCode))."
    }

    Write-Host "Microsoft Edge WebView2 Runtime $installedVersion was installed."
}
finally {
    Remove-Item -LiteralPath $installerPath -Force -ErrorAction SilentlyContinue
}
