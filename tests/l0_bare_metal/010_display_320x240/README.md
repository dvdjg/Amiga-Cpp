# L0-010 — display_320x240 (5 bitplanes, paleta 32, líneas planares)

Tutorial bare metal del Amiga 500 que conmuta a un modo gráfico **320x240 lowres con 5
bitplanes** (paleta de 32 colores plenos), dibuja un patrón de líneas **por CPU en formato
planar**, y demuestra el flujo completo de depuración asistida:

1. el host **lee el framebuffer** por el canal lateral y comprueba las líneas;
2. el host **escribe figuras** por `poke` (escritura de memoria del proceso Amiga);
3. el host **captura una pantalla** como prueba;
4. la demo **restaura el sistema y vuelve a Workbench**.

## Qué ejercita

- **Capa 0 (bare metal):** registros custom `$dffxxx` (`BPLCON0`, `DIWSTRT/STOP`,
  `DDFSTRT/STOP`, `BPL1MOD`, punteros `BPLxPT`, `COLORxx`, `DMACON`, `COP1LC/COPJMP1`),
  dibujo planar de líneas (Bresenham), construcción de una copperlist word a word y
  restauración del sistema.
- **Capa 1 (backend):** `MinimalBackend::configure_memory` (Chip RAM vía `AllocMem`) y
  `MinimalBackend::install_copper_list`.

## Modo gráfico

| Parámetro | Valor | Nota |
|-----------|-------|------|
| Resolución | 320x240 | ventana de display dentro del frame PAL |
| Bitplanes | 5 | 32 colores plenos (sin EHB) |
| Formato pixel | planar | 40 bytes por fila por plano |
| `BPLCON0` | `0x5200` | BPU=5, COLOR=1 |
| `DIWSTRT` / `DIWSTOP` | `0x2c81` / `0x1cc1` | ventana vertical 44..284 |
| `DDFSTRT` / `DDFSTOP` | `0x38` / `0xd0` | fetch 320 px lowres |

## Patrón dibujado (contrato)

El borde del rectángulo en rojo (color 1), una horizontal verde (color 2) en y=32, una
vertical azul (color 3) en x=64 y una diagonal amarilla (color 4) de (0,0) a (200,200).
Los puntos de verificación se publican en `g_test_contract`:

| Punto | Color esperado |
|-------|----------------|
| (0,0), (319,0), (0,239), (319,239) | 1 (borde) |
| (160,32) | 2 (horizontal) |
| (64,120) | 3 (vertical) |
| (100,100) | 4 (diagonal) |
| (160,200) | 0 (fondo limpio) |

## Cómo ejecutar

```powershell
# 1) Compilar
powershell -ExecutionPolicy Bypass -File .\tools\build\build-demo.ps1 tests\l0_bare_metal\010_display_320x240 -DebugBuild -Clean

# 2) Ejecutar + verificar por canal lateral (lee framebuffer, poke figuras, captura)
node .\tests\l0_bare_metal\010_display_320x240\verify-framebuffer.mjs --demo tests\l0_bare_metal\010_display_320x240

# 3) Análisis estándar de la demo (build->run->analyze)
powershell -ExecutionPolicy Bypass -File .\tools\run\run-demo.ps1 tests\l0_bare_metal\010_display_320x240
powershell -ExecutionPolicy Bypass -File .\tools\analyze\analyze-demo.ps1 tests\l0_bare_metal\010_display_320x240
```

### Evidencia generada en `out/run/010_display_320x240/`

- `framebuffer.png` — captura tomada por el host **mientras la demo vive** (líneas + figuras
  inyectadas por `poke`).
- `workbench.png` — captura tomada **después** de que la demo restaure el sistema.
- `screenshot.png` — captura final del runner (Workbench restaurado).
- `verify-report.json` — resumen del verificador.

## Notas técnicas

- `DIWSTOP = 0x1cc1`: ventana vertical de 44 a 284 (240 líneas). Es el valor equivalente a
  `0x2cc1` (256 líneas PAL) menos 16; se verificó por captura. Si en otra máquina/versión
  de WinUAE se ven 256 líneas, revisa este valor.
- El framebuffer y la copperlist viven en Chip RAM (asignados por el backend). La dirección
  física se publica en `g_test_contract`, así que el host la lee sin resolver símbolos.
- La demo se mantiene `Ready` hasta el frame 240 (~4,8 s) y luego restaura `COP1LC` y el
  DMA de display antes de retornar.
