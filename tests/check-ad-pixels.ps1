# SPDX-License-Identifier: GPL-2.0-or-later
# Synthetic OBS composite PNG only. No desktop capture or OCR.
param(
    [Parameter(Mandatory = $true)][string]$Image,
    [int]$X = 0, [int]$Y = 0, [int]$Width = 0, [int]$Height = 0,
    [ValidateSet('primary','alternate')][string]$Variant = 'primary'
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$bitmap = [System.Drawing.Bitmap]::new((Resolve-Path -LiteralPath $Image).Path)
try {
    if ($bitmap.Width -ne 1280 -or $bitmap.Height -ne 720) { throw 'Unexpected OBS canvas dimensions' }
    if ($X -lt 0 -or $Y -lt 0 -or $Width -lt 0 -or $Height -lt 0 -or
        $X + $Width -gt 1280 -or $Y + $Height -gt 720) { throw 'Invalid expected rectangle' }
    $inside = 0; $painted = 0; $outside = 0; $outsideChanged = 0
    # Skip a two-pixel boundary to allow antialiasing; compare the whole canvas,
    # not just a source API flag or one screenshot byte count.
    for ($py = 2; $py -lt 720; $py += 6) {
        for ($px = 2; $px -lt 1280; $px += 6) {
            $pixel = $bitmap.GetPixel($px, $py)
            $distance = [Math]::Abs([int]$pixel.R - 47) +
                [Math]::Abs([int]$pixel.G - 170) + [Math]::Abs([int]$pixel.B - 90)
            if ($Width -gt 0 -and $px -ge $X + 10 -and $px -lt $X + $Width - 10 -and
                $py -ge $Y + 10 -and $py -lt $Y + $Height - 10) {
                $inside++; if ($distance -gt 24) { $painted++ }
            } elseif ($Width -eq 0 -or $px -lt $X - 2 -or $px -ge $X + $Width + 2 -or
                $py -lt $Y - 2 -or $py -ge $Y + $Height + 2) {
                $outside++; if ($distance -gt 6) { $outsideChanged++ }
            }
        }
    }
    # Fixed first-party dark-panel color in a text-free portion of the actual
    # OBS composite. This distinguishes two creatives at the SAME URL without
    # OCR, CEF DOM inspection or trusting the service's advertised campaign ID.
    $variantMatches = $true
    if ($Width -gt 0) {
        $sample = $bitmap.GetPixel($X + $Width - 8, $Y + [int][Math]::Floor($Height / 2))
        $expected = if ($Variant -eq 'alternate') { @(16, 59, 64) } else { @(20, 30, 50) }
        $distance = [Math]::Abs([int]$sample.R - $expected[0]) +
            [Math]::Abs([int]$sample.G - $expected[1]) +
            [Math]::Abs([int]$sample.B - $expected[2])
        $variantMatches = $distance -le 18
    }
    $matches = $outside -gt 0 -and $outsideChanged -eq 0 -and $variantMatches -and
        ($Width -eq 0 -or ($inside -gt 0 -and $painted / $inside -ge 0.95))
    @{ matches = $matches; insideSamples = $inside; paintedSamples = $painted;
       outsideSamples = $outside; outsideChanged = $outsideChanged;
       creativeColorMatches = $variantMatches } | ConvertTo-Json -Compress
} finally { $bitmap.Dispose() }
