param(
    [string]$Port = "COM4",
    [string]$Source = "",
    [int]$BaudRate = 2000000,
    [switch]$WipeSourceLayout,
    [switch]$NativeUsb
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($Source)) {
    $workspaceRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    $Source = Join-Path $workspaceRoot "faces_sd_anim\faces"
}

function Open-Port {
    param(
        [string]$Name,
        [int]$Speed,
        [bool]$EnableResetLines = $true
    )

    $port = [System.IO.Ports.SerialPort]::new($Name, $Speed, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
    $port.NewLine = "`n"
    $port.ReadTimeout = 5000
    $port.WriteTimeout = 15000
    $port.Handshake = [System.IO.Ports.Handshake]::None
    $port.DtrEnable = $EnableResetLines
    $port.RtsEnable = $EnableResetLines
    $port.Open()
    return $port
}

function Read-LineOrNull {
    param(
        [System.IO.Ports.SerialPort]$Port,
        [int]$TimeoutMs = 1200
    )

    $previousTimeout = $Port.ReadTimeout
    try {
        $Port.ReadTimeout = $TimeoutMs
        return $Port.ReadLine().Trim()
    } catch {
        return $null
    } finally {
        $Port.ReadTimeout = $previousTimeout
    }
}

function Send-Line {
    param(
        [System.IO.Ports.SerialPort]$Port,
        [string]$Line
    )
    try {
        $Port.Write($Line + "`n")
    } catch {
        throw "Error enviando '$Line': $($_.Exception.Message)"
    }
}

function Read-LineStrict {
    param(
        [System.IO.Ports.SerialPort]$Port,
        [int]$TimeoutMs = 5000
    )

    $line = Read-LineOrNull -Port $Port -TimeoutMs $TimeoutMs
    if ($null -eq $line) {
        throw "Timeout esperando respuesta del ESP32"
    }
    return $line
}

function Read-ProtocolResponse {
    param(
        [System.IO.Ports.SerialPort]$Port,
        [int]$TimeoutMs = 5000,
        [switch]$AllowHandshakeBanner
    )

    $deadline = (Get-Date).AddMilliseconds($TimeoutMs)
    while ((Get-Date) -lt $deadline) {
        $line = Read-LineOrNull -Port $Port -TimeoutMs 250
        if ($null -eq $line) {
            continue
        }
        if ([string]::IsNullOrWhiteSpace($line)) {
            continue
        }
        if (($line -like "OK SDUP1*") -and -not $AllowHandshakeBanner) {
            continue
        }
        if ($line.StartsWith("OK") -or $line.StartsWith("ERR")) {
            return $line
        }
    }

    throw "Timeout esperando respuesta del ESP32"
}

function Invoke-CommandStrict {
    param(
        [System.IO.Ports.SerialPort]$Port,
        [string]$Line
    )
    Send-Line -Port $Port -Line $Line
    $response = Read-ProtocolResponse -Port $Port
    if (-not $response.StartsWith("OK")) {
        throw "Respuesta inesperada para '$Line': $response"
    }
    return $response
}

function Clear-PortBuffers {
    param([System.IO.Ports.SerialPort]$Port)

    try {
        $Port.DiscardInBuffer()
    } catch {
    }

    $deadline = (Get-Date).AddMilliseconds(250)
    while ((Get-Date) -lt $deadline) {
        $line = Read-LineOrNull -Port $Port -TimeoutMs 80
        if ($null -eq $line) {
            break
        }
    }
}

function Test-UploadHandshake {
    param(
        [System.IO.Ports.SerialPort]$Port,
        [int]$Rounds = 24,
        [int]$PerReadTimeoutMs = 400
    )

    for ($round = 0; $round -lt $Rounds; $round++) {
        try {
            Send-Line -Port $Port -Line "HELLO"
        } catch {
            return $false
        }

        $readsThisRound = 0
        while ($readsThisRound -lt 8) {
            $readsThisRound++
            $line = Read-LineOrNull -Port $Port -TimeoutMs $PerReadTimeoutMs
            if ($null -eq $line) {
                Start-Sleep -Milliseconds 100
                continue
            }
            if ([string]::IsNullOrWhiteSpace($line)) {
                Start-Sleep -Milliseconds 100
                continue
            }
            if ($line -like "OK SDUP1*") {
                return $true
            }
            if ($line -eq "OK SDUP REBOOT") {
                Start-Sleep -Milliseconds 900
            }
            Start-Sleep -Milliseconds 100
        }

        Start-Sleep -Milliseconds 150
    }

    return $false
}

function Connect-UploadMode {
    param(
        [string]$Name,
        [int]$Speed
    )

    $deadline = (Get-Date).AddSeconds(20)
    while ((Get-Date) -lt $deadline) {
        $port = $null
        try {
            $port = if ($NativeUsb) {
                Open-Port -Name $Name -Speed $Speed -EnableResetLines:$false
            } else {
                Open-Port -Name $Name -Speed $Speed
            }
            Start-Sleep -Milliseconds 350
            Clear-PortBuffers -Port $port
            if (Test-UploadHandshake -Port $port -Rounds 32) {
                return $port
            }
            $port.Close()
        } catch {
            if ($port -and $port.IsOpen) {
                $port.Close()
            }
        }

        Start-Sleep -Milliseconds 700
    }

    throw "No se pudo entrar en modo SDUP por $Name"
}

function Prepare-ActivePort {
    param(
        [System.IO.Ports.SerialPort]$SerialPort,
        [string]$PortName,
        [int]$Speed
    )

    if ($NativeUsb) {
        if ($SerialPort -and $SerialPort.IsOpen) {
            $SerialPort.Close()
        }
        Start-Sleep -Milliseconds 200
        $SerialPort = Open-Port -Name $PortName -Speed $Speed -EnableResetLines:$false
        Start-Sleep -Milliseconds 200
        Clear-PortBuffers -Port $SerialPort
    }

    return $SerialPort
}

function Get-RelativePath {
    param(
        [string]$BasePath,
        [string]$FullPath
    )
    $baseUri = [System.Uri]((Resolve-Path $BasePath).Path + [System.IO.Path]::DirectorySeparatorChar)
    $fileUri = [System.Uri](Resolve-Path $FullPath).Path
    $relative = $baseUri.MakeRelativeUri($fileUri).ToString()
    return [System.Uri]::UnescapeDataString($relative).Replace('/', '\')
}

function Send-File {
    param(
        [System.IO.Ports.SerialPort]$Port,
        [string]$SourceRoot,
        [System.IO.FileInfo]$File
    )

    $relative = (Get-RelativePath -BasePath $SourceRoot -FullPath $File.FullName).Replace('\', '/')
    $size = [uint64]$File.Length
    $prep = Invoke-CommandStrict -Port $Port -Line ("PUT {0} {1}" -f $relative, $size)
    if ($prep -ne "OK READY") {
        throw "Respuesta inesperada antes de enviar ${relative}: $prep"
    }

    $stream = $File.OpenRead()
    try {
        if ($NativeUsb) {
            $buffer = New-Object byte[] 4096
        } else {
            $buffer = New-Object byte[] 8192
        }
        $chunkIndex = 0
        while (($read = $stream.Read($buffer, 0, $buffer.Length)) -gt 0) {
            $Port.BaseStream.Write($buffer, 0, $read)
            $chunkIndex++
            if ($NativeUsb -and (($chunkIndex % 4) -eq 0)) {
                Start-Sleep -Milliseconds 1
            }
        }
        $Port.BaseStream.Flush()
    } finally {
        $stream.Dispose()
    }

    $done = Read-ProtocolResponse -Port $Port -TimeoutMs 15000
    if ($done -notlike "OK STORED*") {
        throw "Error subiendo ${relative}: $done"
    }
}

if (-not (Test-Path $Source)) {
    throw "No existe la carpeta origen: $Source"
}

$serialPort = Connect-UploadMode -Name $Port -Speed $BaudRate
try {
    $serialPort = Prepare-ActivePort -SerialPort $serialPort -PortName $Port -Speed $BaudRate

    $info = Invoke-CommandStrict -Port $serialPort -Line "INFO"
    Write-Host "Conectado: $info"

    $root = Resolve-Path $Source
    $rootEntries = Get-ChildItem -LiteralPath $root

    if ($WipeSourceLayout) {
        foreach ($entry in $rootEntries) {
            if ($entry.PSIsContainer) {
                Invoke-CommandStrict -Port $port -Line ("RMDIR {0}" -f $entry.Name) | Out-Null
            } else {
                Invoke-CommandStrict -Port $serialPort -Line ("RM {0}" -f $entry.Name) | Out-Null
            }
        }
    }

    $directories = Get-ChildItem -LiteralPath $root -Recurse -Directory | Sort-Object FullName
    foreach ($dir in $directories) {
        $relative = (Get-RelativePath -BasePath $root -FullPath $dir.FullName).Replace('\', '/')
        Invoke-CommandStrict -Port $serialPort -Line ("MKDIR {0}" -f $relative) | Out-Null
    }

    $files = Get-ChildItem -LiteralPath $root -Recurse -File | Sort-Object FullName
    $index = 0
    $total = $files.Count
    foreach ($file in $files) {
        $index++
        Write-Host ("[{0}/{1}] {2}" -f $index, $total, $file.FullName)
        $attempt = 0
        while ($true) {
            try {
                $attempt++
                Send-File -Port $serialPort -SourceRoot $root -File $file
                break
            } catch {
                if ($attempt -ge 3) {
                    throw
                }

                Write-Host ("Reintentando {0} ({1}/3)" -f $file.FullName, ($attempt + 1))
                if ($serialPort -and $serialPort.IsOpen) {
                    Start-Sleep -Milliseconds 250
                    continue
                }

                Start-Sleep -Milliseconds 300
                $serialPort = Connect-UploadMode -Name $Port -Speed $BaudRate
                $serialPort = Prepare-ActivePort -SerialPort $serialPort -PortName $Port -Speed $BaudRate
                Invoke-CommandStrict -Port $serialPort -Line "INFO" | Out-Null
            }
        }
    }

    try {
        Invoke-CommandStrict -Port $serialPort -Line "RESET" | Out-Null
    } catch {
    }
    Write-Host "Carga completada."
} finally {
    if ($serialPort -and $serialPort.IsOpen) {
        $serialPort.Close()
    }
}
