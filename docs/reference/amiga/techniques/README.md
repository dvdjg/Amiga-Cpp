> **Procedencia:** este índice y las fichas proceden del repo hermano `Cursor-Amiga-C`
> (engine en C). La referencia al "Technique lab" (`app/effects/technique_lab/`) y a la
> batería de pruebas pertenecen a aquel proyecto; aquí se conservan como conocimiento
> reutilizable para el engine C++ de este repo. Los enlaces se han reajustado a esta carpeta.

# Fichas de técnicas (Amiga, juegos y demos)

Resúmenes operativos para la IA y el desarrollador: **qué problema resuelve**, **coste** (CPU, DMA, chip RAM), **límites**, **registros AHRM** y enlace al tutorial externo. **No** se reproducen listados largos de terceros (copyright).

> **Cómo se implementan estas técnicas en el engine**: el modelo objetivo que separa algoritmo,
> superficie, composición y mapping Amiga es `docs/engine/architecture/PLAYFIELD_SCROLL_ARCHITECTURE.md`;
> el plan por fases, `docs/guides/roadmap/REFACTOR_PLAYFIELD_SCROLL.md`.

**Laboratorio compilable en el repo:** menú fallback (sin Intuition) → opción **2. Technique lab** — overlay con `BPL1MOD`/`BPL2MOD` y frame counter; código en `app/effects/technique_lab/technique_lab.c`.

| Ficha | Tema |
|-------|------|
| [modulo-tricks.md](modulo-tricks.md) | Módulos de bitplane (`BPL1MOD`, `BPL2MOD`) para efectos y límites de fetch |
| [dual-layer.md](dual-layer.md) | Dos capas gráficas (dual playfield vs otras composiciones) |
| [robocod-layered-scroll.md](robocod-layered-scroll.md) | Fondo más lento tras un primer plano (parallax por capas, DPF 2 campos) + raster colors |
| [copper-chunky.md](copper-chunky.md) | “Chunky” vía copper / cambios por línea |
| [copper-road-rasters.md](copper-road-rasters.md) | **Carretera/suelo por raster**: buffer ancho planar + tablas `y_source`/`x_offset` + parcheo de `BPLxPT` **por línea** (perspectiva, curvas, rasantes); qué reutilizar del engine y qué falta |
| [hardware-zoom.md](hardware-zoom.md) | **Zoom hardware**: vertical por `BPLxMOD` (repetir/saltar líneas) + horizontal por `BPLCON1` a mitad de línea (truco `$102`, magic table, límite de 4 planos) + pre-escalados y Blitter; contrasta con el rotozoom software del engine |
| [copper-timing-and-budget.md](copper-timing-and-budget.md) | Presupuesto de bus/ciclos PAL, coste de instrucciones 68000, límites de Copper por línea y la técnica SMC (parchear la copperlist); incluye lo medido en la demo 125 |
| [blitter-cpu-interleaving.md](blitter-cpu-interleaving.md) | Solape Blitter/CPU: arrancar el blit y hacer cómputo solo-registros durante la espera; estado del seam (`wait_blitter`, servicio de fondo, espera diferida) |
| [cpu-blit-assist.md](cpu-blit-assist.md) | CPU + blitter en paralelo (A1200+) |
| [audio-mixing.md](audio-mixing.md) | Mezcla de audio para juegos |
| [sprite-layer.md](sprite-layer.md) | Sprites hardware (OCS/ECS/AGA): formato, colores por par, **attached (15 colores)**, prioridad `BPLCON2`, multiplexado vertical/horizontal, **sprite-as-playfield**, colisión `CLXCON/CLXDAT`, antipatrones y estado en el engine |
| [sprite-horizontal-multiplex.md](sprite-horizontal-multiplex.md) | Multiplexado **horizontal** de sprites: rearmado de canal por línea (fondos tipo Risky Woods / Free Form, coste DMA por línea) |
| [sprite-techniques-catalog.md](sprite-techniques-catalog.md) | **Catálogo de las técnicas históricas con sprites hardware** (OCS): básicos, attached, mux vertical/horizontal, Risky Woods, Free Form, combinación con BOBs, colisiones, bending, palette splitting y avanzadas; efectos especiales (animación de DATA, wave), organización de memoria del Free Form, costes y encaje en el engine |
| [sprite-tricks-games.md](sprite-tricks-games.md) | **Sprites hardware en juegos reales** (análisis de *Sprite Tricks*): Risky Woods (fondo 64 px repetido + scroll por punteros), Jim Power (inyección de DATA), Agony (DPF + lluvia + paleta duplicada), Shadow of the Beast (11 capas de parallax + prioridad por bandas) y el Free Form Sprite Layer |
| [sprite-multiplexer-bob-fallback.md](sprite-multiplexer-bob-fallback.md) | **Multiplexor de sprites con fallback a BOB**: elegir dinámicamente sprite hardware o Blitter por objeto (clasificación en setup, sort por Y, reparto O(1) con `lastY[8]`/bitfield, grupos con trayectoria, lista DMA + copperlist); coste casi nulo en el bucle principal y encaje en `SpriteAllocator`/`compose_sprites` |
| [dual-playfield-fastbobs.md](dual-playfield-fastbobs.md) | BOBs rápidos con dual playfield |
| [trackloading.md](trackloading.md) | Carga en segundo plano desde disquete (`trackdisk.device` vs. trackloader de hardware) y equivalente desde HD |
| [blitter-line-subpixel-fill.md](blitter-line-subpixel-fill.md) | Líneas por Blitter (octantes, acumulador/incrementos), **receta de polígono relleno** (contorno `ONEDOT`+EOR + un `area fill` XOR; truco `BLTDPTR`=base, `BLTSIZE` altura 0) y rasterizado **sub-píxel** de polígonos |
| [interleaved-bob-single-blit.md](interleaved-bob-single-blit.md) | BOB **interleaved enmascarado en un solo blit** (`$CA` con máscara expandida una copia por plano): layout, registros (`height=filas*planos`, `DMOD`=fila de plano) y uso desde el engine (demo 213) |
| [blitter-fill-constant.md](blitter-fill-constant.md) | **Relleno de rect por Blitter con valor constante**: cookie-cut `$CA` (`D=(A&B)|(~A&C)`) con A = máscara de borde (canal A deshabilitado, `BLTADAT` recortado por `AFWM`/`ALWM`), B = color y C = D = destino, para **preservar el borde parcial** sin CPU. `BLTADAT` es constante **solo con el canal deshabilitado** (`USEA=0`); y `AFWM`/`ALWM` con `D=A` **no preservan** la D. Regla y trampas del `RectFillSink` y del `FillRect` del `FramePlan` |

**Manual local:** [amiga-hardware-manual-index.md](../../ahrm/amiga-hardware-manual-index.md) y el `.cat.md` del AHRM. **Matriz de máquinas:** [amiga-chipset-matrix.md](../hardware/amiga-chipset-matrix.md).

**Catálogo de pruebas reproducibles** (IDs T/C/B/S/A/M/AG), **capacidades IA vs MCP** (lectura/escritura, depuración, bitmaps, ADF caliente) y **roadmap de herramientas:** [amiga-test-battery-spec.md](../../engine/c-engine/amiga-test-battery-spec.md) (§2, §10). **Estado de implementación (qué falta):** [amiga-implementation-roadmap.md](../../engine/c-engine/amiga-implementation-roadmap.md).
