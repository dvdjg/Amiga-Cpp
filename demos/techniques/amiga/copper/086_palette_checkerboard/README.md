# 086 — Paletas RGB444 y checkerboard por bandas Copper

La demo muestra, de arriba abajo, modos de 3 bitplanes (8 colores), 4 (16), 5 (32) y EHB de 6 (64 índices visibles). El Copper cambia `BPLCON0`, punteros de bitplane y paleta en los límites de cada franja mediante `ModeSwitchZone`; las cuatro imágenes indexadas se precocinan una vez en Chip RAM.

Cada franja contiene una tira de muestras de paleta y una matriz completa N×N. Cada celda de 2×2 alterna los colores de su fila y columna como `A B / B A`; la diagonal es color sólido y el espejo de la matriz repite las parejas con fase opuesta. Filas y columnas se ordenan por luminancia ponderada. El rótulo `DIAG: SIM LIGHT` identifica parejas de brillo próximo, que suelen dar un intermedio estable; `FAR: BRIGHT GAP` identifica los pares de contraste alternante fuerte. La clasificación se refiere a diferencia de brillo: tonos lejanos de color pero luminancia similar aún pueden dar una mezcla cromática poco natural.

La franja EHB usa sus 32 registros base y el sexto bitplane selecciona los otros 32 tonos half-bright. La paleta se genera con `eng::graphics::generate_palette`; COLOR01 se reserva para imprimir el nombre/versión y el contador hexadecimal de frame solo sobre el bitplane 0. EHB requiere BPU=6 con HAM y dual-playfield apagados (AHRM 3.ª ed., cap. 3, §Color Selection in EHB, líneas 2895–2897; WinUAE-DBG `drawing.cpp:2741`, `4172–4179`).

## Compilar y ejecutar

```powershell
& 'C:\Program Files\Git\bin\bash.exe' ./tools/build/build-demo.sh demos/techniques/amiga/copper/086_palette_checkerboard --debug --clean
& 'C:\Program Files\Git\bin\bash.exe' ./tools/run/run-demo.sh demos/techniques/amiga/copper/086_palette_checkerboard --sequence-frames 6 --sequence-interval-ms 1000
& 'C:\Program Files\Git\bin\bash.exe' ./tools/analyze/analyze-demo.sh demos/techniques/amiga/copper/086_palette_checkerboard
```

## Búsqueda previa

Se revisaron `ModeSwitchZone` y `eng::graphics::composition::bplcon0_for` antes de construir las transiciones, además de `eng::graphics::generate_palette` y `Palette32` para reutilizar la paleta existente. Referencias de implementación: demo `113_mode_switch` para conmutación geométrica y `030_ehb_palette_zones` para EHB por raster.

La evidencia final de FPS, secuencia de captura y análisis de visión está en [`VALIDATION.md`](VALIDATION.md); la evaluación canónica de visión está en [`src/VISION.md`](src/VISION.md).
