# Radar Caritas ESP32 Workspace

Workspace de desarrollo para un radar sobre **ESP32-S3 Waveshare 2.1** con:

- firmware principal en `radar2.0`
- editor visual para animaciones `.anim` en `faces_sd_anim`
- scripts de apoyo para abrir el editor, compilar y subir firmware
- material fuente para animaciones y una biblioteca local de expresiones

## Que contiene este repositorio

Este repo agrupa dos flujos que trabajan juntos:

1. **Firmware embebido**
   - Proyecto PlatformIO/Arduino para la placa Waveshare ESP32-S3 Touch LCD 2.1.
   - Renderiza radar, UI, reloj, recursos graficos y animaciones faciales en pantalla `480x480`.

2. **Pipeline de animaciones**
   - Editor HTML local para importar, ordenar, recortar, clasificar y exportar animaciones `.anim`.
   - Biblioteca local de animaciones ya convertidas.
   - Previsualizaciones y herramientas para regenerar la salida final para la SD.

## Estructura principal

- `radar2.0/`
  Proyecto principal del firmware.
- `faces_sd_anim/`
  Editor, manifest local, previews y biblioteca `.anim` usada por el flujo de caritas.
- `exportacion caras  intento 1/`
  Material fuente y exportaciones intermedias de animaciones.
- `ABRIR_EDITOR_CARITAS.bat`
  Arranca servidor local y abre el editor visual.
- `COMPILAR_Y_SUBIR_RADAR.bat`
  Compila y sube el firmware a la placa.
- `INSTRUCCIONES_CARITAS_ANIM.txt`
  Guia operativa centrada en el flujo de caritas.
- `docs/REPOSITORY_LAYOUT.md`
  Mapa del workspace, decisiones de versionado y regeneracion.

## Inicio rapido

### Editor de animaciones

1. Ejecuta `ABRIR_EDITOR_CARITAS.bat`.
2. El script genera `faces_sd_anim/local_faces_manifest.json`.
3. Se levanta un servidor local en `http://127.0.0.1:8765/editor.html`.
4. Desde ahi puedes cargar la copia local, importar imagenes, reorganizar frames y exportar la carpeta `faces`.

### Firmware

1. Entra en `radar2.0/`.
2. Compila con PlatformIO.
3. Usa `COMPILAR_Y_SUBIR_RADAR.bat` si quieres el flujo automatizado de deteccion de puerto y subida.

## Requisitos

- Windows
- Python disponible como `py` o `python`
- PlatformIO para compilar el firmware
- Navegador moderno basado en Chromium para el selector de carpetas del editor

## Notas de versionado

El repositorio **no** incluye artefactos generados pesados, para mantenerlo usable en GitHub:

- `compilacion/`
- `radar2.0/.pio/`
- `Salida faces/`
- `Salida faces.zip`
- `.agents/`

Eso no elimina funcionalidad del proyecto: esos elementos se regeneran localmente a partir del codigo, scripts y recursos versionados.

## Flujo recomendado

1. Editar o importar animaciones en `faces_sd_anim/editor.html`.
2. Exportar la libreria `.anim`.
3. Probar la salida en la SD o integrarla con el firmware.
4. Compilar y subir firmware desde `radar2.0`.

## Documentacion relacionada

- [Mapa del repositorio](./docs/REPOSITORY_LAYOUT.md)
- [Guia de caritas](./INSTRUCCIONES_CARITAS_ANIM.txt)
- [README del firmware](./radar2.0/README.md)
