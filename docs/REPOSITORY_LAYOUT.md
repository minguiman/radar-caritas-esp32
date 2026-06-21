# Repository Layout

## Objetivo

Este workspace mezcla firmware, herramientas locales de edicion y activos de animacion. La idea de esta documentacion es dejar claro que parte es fuente, que parte es salida generada y que parte conviene regenerar en vez de versionar.

## Carpetas versionadas

### `radar2.0/`

Contiene el proyecto principal del firmware:

- `src/`: codigo fuente C++ del radar, render, modelo y recursos
- `tools/`: scripts auxiliares de generacion y mantenimiento
- `boards/`: definicion local de la placa
- `assets/`: recursos necesarios en tiempo de build
- `platformio.ini`: configuracion del build

### `faces_sd_anim/`

Contiene el flujo de animaciones:

- `editor.html`: editor visual local
- `_preview.html`: galeria de previsualizacion
- `_preview/`: imagenes de preview generadas y utiles para inspeccion
- `faces/`: libreria local `.anim` lista para cargar o editar
- `local_faces_manifest.json`: indice local regenerable para el editor

### `exportacion caras  intento 1/`

Contiene material fuente y exportaciones intermedias relacionadas con la creacion de animaciones. Se conserva porque aporta contexto visual y permite rehacer parte del pipeline creativo.

## Archivos raiz relevantes

- `ABRIR_EDITOR_CARITAS.bat`: abre el editor en local y genera el manifest
- `COMPILAR_Y_SUBIR_RADAR.bat`: build + upload del firmware
- `INSTRUCCIONES_CARITAS_ANIM.txt`: manual operativo del flujo de animaciones

## Carpetas y archivos excluidos del repo

### `compilacion/`

Salida generada de PlatformIO y enlazado final. No debe versionarse:

- ocupa bastante espacio
- cambia en cada build
- se regenera con `platformio run`

### `radar2.0/.pio/`

Dependencias y caches locales de PlatformIO. Tampoco deben versionarse:

- incluyen librerias descargadas
- contienen metadatos temporales de compilacion
- inflan el repo innecesariamente

### `Salida faces/`

Exportacion final pesada de animaciones ya procesadas. Es una salida reproducible del pipeline, no una fuente primaria.

### `Salida faces.zip`

Archivo empaquetado grande, no adecuado para historial git normal y ademas innecesario si la salida puede regenerarse.

### `.agents/`

Metadatos locales del entorno de trabajo de Codex. No forman parte del proyecto.

## Regeneracion

### Rebuild del firmware

Desde `radar2.0/`:

```powershell
py -m platformio run -e release
```

### Apertura del editor

Desde la raiz del workspace:

```powershell
.\ABRIR_EDITOR_CARITAS.bat
```

### Regenerar animaciones `.anim`

Segun la guia existente:

```powershell
cd .\radar2.0
py .\tools\face_anim_tool.py from-manifest
```

## Criterio de publicacion

Este repositorio queda orientado a:

- compartir el codigo fuente real
- conservar herramientas y activos utiles
- documentar el flujo completo
- evitar binarios generados que rompen el historial o hacen el clone pesado
