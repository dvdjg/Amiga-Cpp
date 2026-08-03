> **Procedencia:** este índice y las fichas proceden del repo hermano `Cursor-Amiga-C`
> (engine en C). La referencia al "Technique lab" (`app/effects/technique_lab/`) y a la
> batería de pruebas pertenecen a aquel proyecto; aquí se conservan como conocimiento
> reutilizable para el engine C++ de este repo. Los enlaces se han reajustado a esta carpeta.

# Fichas de técnicas (Amiga, juegos y demos)

Resúmenes operativos para la IA y el desarrollador: **qué problema resuelve**, **coste** (CPU, DMA, chip RAM), **límites**, **registros AHRM** y enlace al tutorial externo. **No** se reproducen listados largos de terceros (copyright).

**Laboratorio compilable en el repo:** menú fallback (sin Intuition) → opción **2. Technique lab** — overlay con `BPL1MOD`/`BPL2MOD` y frame counter; código en `app/effects/technique_lab/technique_lab.c`.

| Ficha | Tema |
|-------|------|
| [modulo-tricks.md](modulo-tricks.md) | Módulos de bitplane (`BPL1MOD`, `BPL2MOD`) para efectos y límites de fetch |
| [dual-layer.md](dual-layer.md) | Dos capas gráficas (dual playfield vs otras composiciones) |
| [copper-chunky.md](copper-chunky.md) | “Chunky” vía copper / cambios por línea |
| [cpu-blit-assist.md](cpu-blit-assist.md) | CPU + blitter en paralelo (A1200+) |
| [audio-mixing.md](audio-mixing.md) | Mezcla de audio para juegos |
| [sprite-layer.md](sprite-layer.md) | Capa tipo sprite / prioridades |
| [dual-playfield-fastbobs.md](dual-playfield-fastbobs.md) | BOBs rápidos con dual playfield |

**Manual local:** [amiga-hardware-manual-index.md](../reference/ahrm/amiga-hardware-manual-index.md) y el `.cat.md` del AHRM. **Matriz de máquinas:** [amiga-chipset-matrix.md](../hardware/amiga-chipset-matrix.md).

**Catálogo de pruebas reproducibles** (IDs T/C/B/S/A/M/AG), **capacidades IA vs MCP** (lectura/escritura, depuración, bitmaps, ADF caliente) y **roadmap de herramientas:** [amiga-test-battery-spec.md](../c-engine/amiga-test-battery-spec.md) (§2, §10). **Estado de implementación (qué falta):** [amiga-implementation-roadmap.md](../c-engine/amiga-implementation-roadmap.md).
