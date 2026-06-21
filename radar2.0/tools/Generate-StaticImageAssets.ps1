param(
    [string]$ProjectRoot = (Split-Path -Parent $PSScriptRoot),
    [string]$ImageName = "image.bmp"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing

# Convierte image.bmp (raíz del proyecto, 480x480) a un asset RGB565 compilado.
# Genera src/StaticImageAssets.h y src/StaticImageAssets.cpp con el mismo estilo
# de arrays que ClockAssets (0xRRRR, 12 por línea). Usa LockBits para leer el mapa
# de bits en bloque (mucho más rápido que GetPixel pixel a pixel).

$imagePath = Join-Path $ProjectRoot $ImageName
$headerPath = Join-Path $ProjectRoot "src\StaticImageAssets.h"
$sourcePath = Join-Path $ProjectRoot "src\StaticImageAssets.cpp"

if (-not (Test-Path $imagePath)) {
    throw "No se encuentra la imagen: $imagePath"
}

$bitmap = New-Object System.Drawing.Bitmap($imagePath)
try {
    if ($bitmap.Width -ne 480 -or $bitmap.Height -ne 480) {
        throw "La imagen debe medir 480x480 (actual: $($bitmap.Width)x$($bitmap.Height))."
    }

    $width = $bitmap.Width
    $height = $bitmap.Height
    $pixelCount = $width * $height

    $rect = New-Object System.Drawing.Rectangle(0, 0, $width, $height)
    $data = $bitmap.LockBits($rect,
        [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
        [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    try {
        $stride = $data.Stride
        $buffer = New-Object byte[] ($stride * $height)
        [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $buffer, 0, $buffer.Length)
    } finally {
        $bitmap.UnlockBits($data)
    }

    # --- Cabecera ---
    $h = [System.Text.StringBuilder]::new()
    [void]$h.AppendLine("#pragma once")
    [void]$h.AppendLine()
    [void]$h.AppendLine("#include <cstdint>")
    [void]$h.AppendLine()
    [void]$h.AppendLine("namespace StaticImageAssets")
    [void]$h.AppendLine("{")
    [void]$h.AppendLine("inline constexpr int kImageWidth = $width;")
    [void]$h.AppendLine("inline constexpr int kImageHeight = $height;")
    [void]$h.AppendLine()
    [void]$h.AppendLine("extern const uint16_t kStaticImage565[kImageWidth * kImageHeight];")
    [void]$h.AppendLine("}")
    [System.IO.File]::WriteAllText($headerPath, $h.ToString(), [System.Text.Encoding]::ASCII)

    # --- Fuente (array RGB565) ---
    $writer = [System.IO.StreamWriter]::new($sourcePath, $false, [System.Text.Encoding]::ASCII)
    try {
        $writer.WriteLine('#include "StaticImageAssets.h"')
        $writer.WriteLine()
        $writer.WriteLine("namespace StaticImageAssets")
        $writer.WriteLine("{")
        $writer.WriteLine("const uint16_t kStaticImage565[$pixelCount] = {")

        $chunk = 12
        $line = [System.Text.StringBuilder]::new()
        for ($y = 0; $y -lt $height; $y++) {
            $rowBase = $y * $stride
            for ($x = 0; $x -lt $width; $x++) {
                $idx = $rowBase + ($x * 3)
                $b = $buffer[$idx]
                $g = $buffer[$idx + 1]
                $r = $buffer[$idx + 2]
                $rgb565 = ((($r -shr 3) -band 0x1F) -shl 11) -bor `
                          ((($g -shr 2) -band 0x3F) -shl 5) -bor `
                          (($b -shr 3) -band 0x1F)

                $pixelIndex = ($y * $width) + $x
                if (($pixelIndex % $chunk) -eq 0) {
                    [void]$line.Append("    ")
                }
                [void]$line.Append(("0x{0:X4}" -f $rgb565))

                $isLastPixel = ($pixelIndex -eq ($pixelCount - 1))
                if ((($pixelIndex % $chunk) -eq ($chunk - 1)) -or $isLastPixel) {
                    if (-not $isLastPixel) {
                        [void]$line.Append(",")
                    }
                    $writer.WriteLine($line.ToString())
                    [void]$line.Clear()
                } else {
                    [void]$line.Append(", ")
                }
            }
        }

        $writer.WriteLine("};")
        $writer.WriteLine("}")
    } finally {
        $writer.Dispose()
    }

    Write-Output "Generado: $headerPath"
    Write-Output "Generado: $sourcePath ($pixelCount pixeles)"
} finally {
    $bitmap.Dispose()
}
