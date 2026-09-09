[CmdletBinding()]
param(
    [Parameter()]
    [string]$Channel,

    [Parameter()]
    [string]$Username,

    [Parameter()]
    [switch]$Anonymous,

    [Parameter()]
    [switch]$Disable
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Normalize-TwitchName {
    param(
        [Parameter(Mandatory)]
        [string]$Value,

        [Parameter()]
        [switch]$AllowHash
    )

    $normalized = $Value.Trim().ToLowerInvariant()
    if ($AllowHash -and $normalized.StartsWith('#')) {
        $normalized = $normalized.Substring(1)
    }

    if ($normalized -notmatch '^[a-z0-9_]{1,25}$') {
        throw "Invalid Twitch name: '$Value'."
    }

    return $normalized
}

$chatViewDirectory = Join-Path $env:LOCALAPPDATA 'ChatView'
$configPath = Join-Path $chatViewDirectory 'twitch.ini'
New-Item -ItemType Directory -Path $chatViewDirectory -Force | Out-Null

if ($Disable) {
    @(
        '[twitch]'
        'enabled=0'
    ) | Set-Content -LiteralPath $configPath -Encoding Unicode
    Write-Host "Twitch chat was disabled in '$configPath'."
    exit 0
}

if ([string]::IsNullOrWhiteSpace($Channel)) {
    $Channel = Read-Host 'Twitch channel name'
}
$Channel = Normalize-TwitchName -Value $Channel -AllowHash

$tokenDpapi = ''
if (-not $Anonymous) {
    if ([string]::IsNullOrWhiteSpace($Username)) {
        $Username = Read-Host 'Twitch username'
    }
    $Username = Normalize-TwitchName -Value $Username

    $secureToken = Read-Host 'Twitch OAuth access token' -AsSecureString
    $bstr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secureToken)
    $plainToken = $null
    $tokenBytes = $null
    $protectedBytes = $null

    try {
        $plainToken = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($bstr)
        if ($plainToken.StartsWith('oauth:', [StringComparison]::OrdinalIgnoreCase)) {
            $plainToken = $plainToken.Substring(6)
        }
        if ([string]::IsNullOrWhiteSpace($plainToken) -or $plainToken -match '\s') {
            throw 'The Twitch token is empty or contains whitespace.'
        }

        Add-Type -AssemblyName System.Security
        $tokenBytes = [Text.Encoding]::UTF8.GetBytes($plainToken)
        $protectedBytes = [Security.Cryptography.ProtectedData]::Protect(
            $tokenBytes,
            $null,
            [Security.Cryptography.DataProtectionScope]::CurrentUser)
        $tokenDpapi = [Convert]::ToBase64String($protectedBytes)
    }
    finally {
        if ($null -ne $tokenBytes) {
            [Array]::Clear($tokenBytes, 0, $tokenBytes.Length)
        }
        if ($null -ne $protectedBytes) {
            [Array]::Clear($protectedBytes, 0, $protectedBytes.Length)
        }
        if ($bstr -ne [IntPtr]::Zero) {
            [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr)
        }
        $plainToken = $null
        $secureToken = $null
    }
}
else {
    $Username = ''
}

$tempPath = "$configPath.tmp"
@(
    '[twitch]'
    'enabled=1'
    "channel=$Channel"
    "username=$Username"
    "token_dpapi=$tokenDpapi"
) | Set-Content -LiteralPath $tempPath -Encoding Unicode
Move-Item -LiteralPath $tempPath -Destination $configPath -Force

if ($Anonymous) {
    Write-Host "Anonymous Twitch chat for '#$Channel' was configured."
}
else {
    Write-Host "Authenticated Twitch chat for '#$Channel' was configured."
}
Write-Host "Configuration: $configPath"
