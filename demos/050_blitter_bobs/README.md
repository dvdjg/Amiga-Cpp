# 050_blitter_bobs

Primera demo del Blitter integrada con `FramePlan`.

La escena usa el driver EHB ya existente para mostrar un fondo planar de 6
bitplanes. El juego no escribe registros del Blitter directamente: crea trabajos
`BlitJob` en `FramePlan` y el backend Amiga los materializa con el Blitter
hardware.

La demo valida dos rutas:

- `MaskedBobCookieCut`: BOB normal que mas adelante necesitara save/restore si se
  mueve sobre fondo vivo.
- `MaskedBlobNoSave`: blobs no solapados estilo Mega Typhoon, pensados para pintar
  directamente sobre un playfield cuando sabemos que no hace falta conservar el
  contenido previo.

Restricciones de este MVP:

- BOB de 32x32 pixels;
- X alineada a 16 pixels, sin shifts;
- sin clipping;
- una unica mascara de 1 bit compartida por los 6 bitplanes;
- save/restore existe como tipo de job copy/restore en `FramePlan`, pero la demo
  aun no anima ni restaura fondo entre frames.

El objetivo es fijar el contrato `FramePlan -> backend -> Blitter` y empezar a
medir presupuesto por words/bitplanes antes de anadir dirty rects, restauracion de
fondo y animacion.

Comandos:

```powershell
.\tools\build\build-demo.ps1 demos\050_blitter_bobs -DebugBuild
.\tools\run\run-demo.ps1 demos\050_blitter_bobs
.\tools\analyze\analyze-demo.ps1 demos\050_blitter_bobs
```
