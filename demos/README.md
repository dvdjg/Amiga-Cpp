# Demos

Demos del engine, en dos raíces **hermanas**:

- **`techniques/`** — técnicas de **hardware** (no portables): Copper, Blitter, Sprites (Amiga);
  Shifter/YM/MFP (Atari ST/STE); VDP/Z80 (Megadrive). Path: `techniques/<familia>/<categoría>/NNN_<tema>/`.
- **`features/`** — **features portables** con paridad entre plataformas (UI, cartas, tablero,
  simulación, emulación, audio…). Path: `features/<feature>/<plataforma>/NNN_<tema>/`; la lógica
  portable vive en el engine y cada plataforma es un **adaptador fino**.

La **variante de build** (`A500`/`A1200`/`ST`/`STE`) es un eje de `TARGET_MACHINE`, no un
directorio; el `CONFIG_ID` del binario y el nombre de los assets llevan el perfil para no machacarse.

- Estructura canónica: [`../docs/STRUCTURE.md`](../docs/STRUCTURE.md) §4.
- Plan y decisiones: [`../docs/guides/roadmap/PLAN_ORGANIZACION_DEMOS.md`](../docs/guides/roadmap/PLAN_ORGANIZACION_DEMOS.md).
- Numeración: [`../docs/ai-dev-environment/NUMBERING.md`](../docs/ai-dev-environment/NUMBERING.md).

## Estado de migración

| Ámbito | Estado |
|---|---|
| `techniques/amiga/copper/` | migrado (020, 030, 055, 085) |
| `amiga/` (histórico) | pendiente de reclasificar a `techniques/amiga/…` |
| `features/…` | pendiente de extraer (lógica al engine) |

## Build / run / analyze

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/copper/020_copper_basic --debug --clean
bash ./tools/run/run-demo.sh demos/techniques/amiga/copper/020_copper_basic
bash ./tools/analyze/analyze-demo.sh demos/techniques/amiga/copper/020_copper_basic
```
