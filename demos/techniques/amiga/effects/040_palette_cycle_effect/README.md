# 040_palette_cycle_effect

Esta demo valida dos efectos reutilizables del engine compuestos en el mismo plan:
`PaletteCycleEffect` (rota un tramo) y `PaletteTransitionEffect` (funde entre dos
paletas).

La imagen EHB permanece fija en memoria Chip. Lo que cambia cada frame son paletas
runtime de 32 colores. La demo genera un `FramePlan` con un parche de
`COLOR01..COLOR07` (ciclo) y otro de `COLOR16..COLOR31` (fundido entre la paleta base y
una atenuada), y la escena modifica solo las words de valor ya existentes en la
copperlist. La zona superior usa indices 1..7 para que el ciclo sea evidente; la zona
inferior mantiene una paleta Copper fija para comprobar que los efectos animados, una
zona raster y varios parches de paleta conviven bajo el `CopperScheduler`.

Objetivos verificados:

- compilar C++23 freestanding con el toolchain del plugin;
- reservar bitplanes y copperlist en Chip RAM;
- aplicar un ciclo de paleta y un fundido sin tocar pixels;
- componer dos efectos con parches de paleta distintos en un mismo `FramePlan`, sin
  reconstruir toda la copperlist;
- exponer `g_eng_run_status.detail` con una marca `0x04xxxxxx` cuando la demo ya
  ha avanzado varias fases;
- superar analisis automatico de captura y run-report.

Comandos:

```powershell
.\tools\build\build-demo.ps1 demos\techniques\amiga\effects\040_palette_cycle_effect -DebugBuild
.\tools\run\run-demo.ps1 demos\techniques\amiga\effects\040_palette_cycle_effect
.\tools\analyze\analyze-demo.ps1 demos\techniques\amiga\effects\040_palette_cycle_effect
```
