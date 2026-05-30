# 050_blitter_bobs

Primera demo del Blitter integrada con `FramePlan`.

La escena usa el driver EHB ya existente para mostrar un fondo planar de 6
bitplanes. El juego no escribe registros del Blitter directamente: crea un
`BlitJob` cookie-cut en `FramePlan` y el backend Amiga lo materializa con el
Blitter hardware.

Restricciones de este MVP:

- BOB de 32x32 pixels;
- X alineada a 16 pixels, sin shifts;
- sin clipping;
- una unica mascara de 1 bit compartida por los 6 bitplanes;
- sin save/restore todavia, porque esta demo solo dibuja una vez.

El objetivo es fijar el contrato `FramePlan -> backend -> Blitter` antes de anadir
dirty rects, restauracion de fondo, animacion y presupuesto por frame.

Comandos:

```powershell
.\tools\build\build-demo.ps1 demos\050_blitter_bobs -DebugBuild
.\tools\run\run-demo.ps1 demos\050_blitter_bobs
.\tools\analyze\analyze-demo.ps1 demos\050_blitter_bobs
```
