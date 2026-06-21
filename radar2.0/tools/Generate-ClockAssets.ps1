param(
    [string]$ProjectRoot = (Split-Path -Parent $PSScriptRoot)
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing

function Convert-ToRgb565Line {
    param(
        [System.Drawing.Bitmap]$Bitmap,
        [int]$Width,
        [int]$StartIndex,
        [int]$Count
    )

    $values = New-Object System.Collections.Generic.List[string]
    for ($i = 0; $i -lt $Count; $i++) {
        $index = $StartIndex + $i
        $x = $index % $Width
        $y = [int][Math]::Floor($index / $Width)
        $pixel = $Bitmap.GetPixel($x, $y)
        if ($pixel.A -lt 128) {
            $rgb565 = 0xF81F
        } else {
            $r = ($pixel.R -shr 3) -band 0x1F
            $g = ($pixel.G -shr 2) -band 0x3F
            $b = ($pixel.B -shr 3) -band 0x1F
            $rgb565 = (($r -shl 11) -bor ($g -shl 5) -bor $b)
        }
        $values.Add(("0x{0:X4}" -f $rgb565))
    }
    return "    " + ($values -join ", ")
}

function Write-Array {
    param(
        [System.IO.StreamWriter]$Writer,
        [string]$Name,
        [System.Drawing.Bitmap]$Bitmap
    )

    $pixelCount = $Bitmap.Width * $Bitmap.Height
    $Writer.WriteLine("const uint16_t $Name[$pixelCount] = {")
    $chunkSize = 12
    for ($start = 0; $start -lt $pixelCount; $start += $chunkSize) {
        $count = [Math]::Min($chunkSize, $pixelCount - $start)
        $suffix = if (($start + $count) -lt $pixelCount) { "," } else { "" }
        $Writer.WriteLine(((Convert-ToRgb565Line -Bitmap $Bitmap -Width $Bitmap.Width -StartIndex $start -Count $count) + $suffix))
    }
    $Writer.WriteLine("};")
    $Writer.WriteLine()
}

function Write-TransparentSpans {
    param(
        [System.IO.StreamWriter]$Writer,
        [System.Drawing.Bitmap]$Bitmap
    )

    $Writer.WriteLine("const uint16_t kClockFrameTransparentStart[kClockFrameHeight] = {")
    $startValues = New-Object System.Collections.Generic.List[string]
    $endValues = New-Object System.Collections.Generic.List[string]

    for ($y = 0; $y -lt $Bitmap.Height; $y++) {
        $firstStart = -1
        $lastEnd = -1

        for ($x = 0; $x -lt $Bitmap.Width; $x++) {
            $isTransparent = $Bitmap.GetPixel($x, $y).A -lt 128
            if ($isTransparent) {
                if ($firstStart -lt 0) {
                    $firstStart = $x
                }
                $lastEnd = $x + 1
            }
        }

        if ($firstStart -lt 0) {
            $firstStart = 0
            $lastEnd = 0
        }

        $startValues.Add($firstStart.ToString())
        $endValues.Add($lastEnd.ToString())
    }

    for ($start = 0; $start -lt $startValues.Count; $start += 16) {
        $count = [Math]::Min(16, $startValues.Count - $start)
        $suffix = if (($start + $count) -lt $startValues.Count) { "," } else { "" }
        $Writer.WriteLine("    " + (($startValues.GetRange($start, $count)) -join ", ") + $suffix)
    }
    $Writer.WriteLine("};")
    $Writer.WriteLine()

    $Writer.WriteLine("const uint16_t kClockFrameTransparentEnd[kClockFrameHeight] = {")
    for ($start = 0; $start -lt $endValues.Count; $start += 16) {
        $count = [Math]::Min(16, $endValues.Count - $start)
        $suffix = if (($start + $count) -lt $endValues.Count) { "," } else { "" }
        $Writer.WriteLine("    " + (($endValues.GetRange($start, $count)) -join ", ") + $suffix)
    }
    $Writer.WriteLine("};")
    $Writer.WriteLine()
}

function New-ResizedBitmap {
    param(
        [System.Drawing.Image]$Source,
        [int]$Width,
        [int]$Height
    )

    $bitmap = New-Object System.Drawing.Bitmap($Width, $Height)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
    $graphics.DrawImage($Source, 0, 0, $Width, $Height)
    $graphics.Dispose()
    return $bitmap
}

$framePath = Join-Path $ProjectRoot "marco.png"
$backgroundPath = Join-Path $ProjectRoot "fondo.png"
$outputPath = Join-Path $ProjectRoot "src\\ClockAssets.cpp"

$frameSource = [System.Drawing.Image]::FromFile($framePath)
$backgroundSource = [System.Drawing.Image]::FromFile($backgroundPath)

try {
    $frameBitmap = New-ResizedBitmap -Source $frameSource -Width 480 -Height 480
    $backgroundBitmap = New-ResizedBitmap -Source $backgroundSource -Width 1024 -Height 1024

    $writer = [System.IO.StreamWriter]::new($outputPath, $false, [System.Text.Encoding]::ASCII)
    try {
        $writer.WriteLine('#include "ClockAssets.h"')
        $writer.WriteLine()
        $writer.WriteLine("namespace ClockAssets")
        $writer.WriteLine("{")
        Write-Array -Writer $writer -Name "kClockFrame565" -Bitmap $frameBitmap
        Write-Array -Writer $writer -Name "kClockBackground565" -Bitmap $backgroundBitmap
        Write-TransparentSpans -Writer $writer -Bitmap $frameBitmap
        $writer.WriteLine("}")
    } finally {
        $writer.Dispose()
    }
} finally {
    $frameSource.Dispose()
    $backgroundSource.Dispose()
    if ($null -ne $frameBitmap) { $frameBitmap.Dispose() }
    if ($null -ne $backgroundBitmap) { $backgroundBitmap.Dispose() }
}
