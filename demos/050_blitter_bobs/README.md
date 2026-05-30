# 050_blitter_bobs

Primera demo del Blitter integrada con `FramePlan`.

La escena usa el driver EHB ya existente para mostrar un fondo planar de 6
bitplanes. El juego no escribe registros del Blitter directamente: crea trabajos
`BlitJob` en `FramePlan` y el backend Amiga los materializa con el Blitter
hardware.

La demo valida tres rutas:

- `MaskedBobCookieCut`: BOB normal que mas adelante necesitara save/restore si se
  mueve sobre fondo vivo.
- `CopyRect`/`RestoreRect`: guardar el fondo bajo el BOB, restaurar la posicion
  anterior y guardar la nueva antes de dibujar.
- `MaskedBlobNoSave`: blobs no solapados estilo Mega Typhoon, pensados para pintar
  directamente sobre un playfield cuando sabemos que no hace falta conservar el
  contenido previo.

Restricciones de este MVP:

- BOB de 32x32 pixels;
- X alineada a 16 pixels, sin shifts;
- sin clipping;
- una unica mascara de 1 bit compartida por los 6 bitplanes;
- el BOB se mueve en pasos de 16 pixels y se restaura con un buffer de fondo
  compacto en Chip RAM.

El objetivo es fijar el contrato `FramePlan -> backend -> Blitter`, demostrar
save/restore real y empezar a medir presupuesto por words/bitplanes antes de
anadir clipping y shifts.

El frame final tambien publica dirty rects fusionados en `g_amg_run_status.detail`.
El estado saludable actual es `0x05020311`: dos blobs no-save iniciales, tres jobs
de Blitter en el frame animado, un dirty rect final y una fusion de rectangulos.
La demo se congela tras ese frame para que la captura automatizada no caiga en
mitad de una secuencia restore/save/draw.

Comandos:

```powershell
.\tools\build\build-demo.ps1 demos\050_blitter_bobs -DebugBuild
.\tools\run\run-demo.ps1 demos\050_blitter_bobs
.\tools\analyze\analyze-demo.ps1 demos\050_blitter_bobs
```
