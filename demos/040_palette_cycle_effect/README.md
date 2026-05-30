# 040_palette_cycle_effect

Esta demo valida el primer efecto reutilizable del engine: `PaletteCycleEffect`.

La imagen EHB permanece fija en memoria Chip. Lo que cambia cada frame es una
paleta runtime de 32 colores que `StaticEhbScene` recompila en su copperlist. La
zona superior usa indices 1..7 para que el ciclo sea evidente; la zona inferior
mantiene una paleta Copper fija para comprobar que un efecto animado y una zona
raster pueden convivir bajo el `CopperScheduler`.

Objetivos verificados:

- compilar C++23 freestanding con el toolchain del plugin;
- reservar bitplanes y copperlist en Chip RAM;
- aplicar un ciclo de paleta sin tocar pixels;
- reconstruir la copperlist desde un driver, no desde la logica de juego;
- exponer `g_amg_run_status.detail` con una marca `0x04xxxxxx` cuando la demo ya
  ha avanzado varias fases;
- superar analisis automatico de captura y run-report.

Comandos:

```powershell
.\tools\build\build-demo.ps1 demos\040_palette_cycle_effect -DebugBuild
.\tools\run\run-demo.ps1 demos\040_palette_cycle_effect
.\tools\analyze\analyze-demo.ps1 demos\040_palette_cycle_effect
```
