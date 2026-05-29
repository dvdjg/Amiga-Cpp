# Demo 020: Copper basic

Objetivo: instalar una copperlist real en Chip RAM y demostrar que el Copper cambia
`COLOR00` por bandas horizontales sin trabajo de CPU por frame.

## Que demuestra

- Construccion de copperlist con `amg::copper::ListBuilder`.
- Reserva de la copperlist en `ChipArena`.
- Instalacion close-to-the-metal mediante `COP1LC` y `COPJMP1`.
- Activacion de DMA master + Copper.
- Pantalla sin bitplanes: el fondo visible es `COLOR00`.
- Cambios raster basicos mediante `WAIT + MOVE COLOR00`.

## Build, run y analisis

```powershell
.\tools\build\build-demo.ps1 demos\020_copper_basic -DebugBuild
.\tools\run\run-demo.ps1 demos\020_copper_basic
.\tools\analyze\analyze-demo.ps1 demos\020_copper_basic
```

O como parte de la regresion completa:

```powershell
.\tools\test-regression.ps1
```

## Criterio de aceptacion

- La captura muestra el overlay de la demo.
- Se ven bandas rojas, verdes, azules, amarillas y cian a pantalla completa.
- Esta demo toma el control del display: la captura puede no mostrar el overlay de
  debug porque lo importante es validar el raster generado por Copper.
- El analizador especifico detecta muestras suficientes de las cinco bandas.
- La regresion compila, ejecuta, captura y analiza la demo.
