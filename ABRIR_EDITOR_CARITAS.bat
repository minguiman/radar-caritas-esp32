@echo off
setlocal

rem Carpeta donde esta este BAT. Si mueves "last" a otro PC, todo cuelga de aqui.
set "ROOT=%~dp0"
set "EDITOR_DIR=%ROOT%faces_sd_anim"
set "PORT=8765"
set "URL=http://127.0.0.1:%PORT%/editor.html"

powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$root=$env:ROOT; $dir=${env:EDITOR_DIR}.TrimEnd('\'); $port=[int]$env:PORT; $url=$env:URL; if (-not (Test-Path -LiteralPath (Join-Path $dir 'editor.html'))) { Write-Host 'No encuentro editor.html en:' $dir; pause; exit 1 }; $faces=Join-Path $dir 'faces'; $files=@(); if (Test-Path -LiteralPath $faces) { $files = Get-ChildItem -LiteralPath $faces -Recurse -File -Filter '*.anim' | Sort-Object FullName | ForEach-Object { $rel=$_.FullName.Substring($dir.Length+1).Replace('\','/'); $parts=$rel.Split('/'); [PSCustomObject]@{ path=$rel; category=$parts[1]; name=[System.IO.Path]::GetFileNameWithoutExtension($_.Name); bytes=$_.Length } } }; [PSCustomObject]@{ root=$root; faces='faces_sd_anim/faces'; generated=(Get-Date).ToString('s'); files=$files } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $dir 'local_faces_manifest.json') -Encoding UTF8; $busy = Get-NetTCPConnection -LocalPort $port -ErrorAction SilentlyContinue; if (-not $busy) { $py=Get-Command py -ErrorAction SilentlyContinue; if ($py) { Start-Process -FilePath $py.Source -ArgumentList @('-3','-m','http.server',[string]$port,'--bind','127.0.0.1') -WorkingDirectory $dir -WindowStyle Hidden } else { $python=Get-Command python -ErrorAction SilentlyContinue; if (-not $python) { Write-Host 'No encuentro Python. Instala Python o revisa que py/python este en PATH.'; pause; exit 1 }; Start-Process -FilePath $python.Source -ArgumentList @('-m','http.server',[string]$port,'--bind','127.0.0.1') -WorkingDirectory $dir -WindowStyle Hidden }; Start-Sleep -Milliseconds 900 }; Start-Process $url"

endlocal
