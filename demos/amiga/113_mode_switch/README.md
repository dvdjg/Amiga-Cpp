# 113 — `ModeSwitchZone`: conmutación de geometría de vídeo (MI09)

Microtest de hardware del invariante **MI09**: cambiar la geometría de vídeo
(`BPLCON0`/`DDF`/`BPLxMOD`) en un `WAIT` permite tramos con distinto número de planos,
reprogramando en orden `BPLCON0` → `DDF` → módulos → `BPLxPT` y con el `DDF` alineado.
Es la pieza de Fase 1b del refactor de playfields (`REFACTOR_PLAYFIELD_SCROLL.md`).

## Qué muestra

```text
  ventana 320x256 (raster DIWSTRT 0x2c81)
  ┌──────────────────────────────┐  linea 0
  │ CAMPO  5 planos (32 colores)  │  reticula de celdas de 40 px,
  │                               │  indice = banda(2 bits) | celda(3 bits)
  ├──────────────────────────────┤  linea 160  (raster 0xcc)  <- WAIT + zone
  │ HUD    2 planos (4 colores)   │  barras que reciclan 0..3
  └──────────────────────────────┘  linea 256
```

- **Campo**: `BPLCON0 = 0x5200` (BPU=5), `emit_planes_display` con 5 planos
  contiguos (mod=0, mismo layout que `StaticEhbScene`).
- **HUD**: `ModeSwitchZone` con `BPLCON0 = 0x2200` (BPU=2), el mismo `DDF`/módulos y
  los punteros a un bloque de 2 planos. Detrás van 3 planos "veneno" a `0xFF`: si la
  conmutación no se aplicara y el HUD siguiera leyendo 5 planos, los bits altos
  (veneno) pintarían índices > 3 con colores ajenos a la paleta del HUD.

## Verificación (determinista, sin visión)

```bash
bash ./tools/build/build-demo.sh demos/amiga/113_mode_switch --debug --clean
bash ./tools/run/run-demo.sh demos/amiga/113_mode_switch
node tools/analyze/verify-113-mode-switch.mjs out/run/113_mode_switch/A500_debug/screenshot.png
```

El verificador comprueba que la franja del HUD muestra **exactamente** los 4 colores de
su paleta de 2 planos y que el campo supera los 8 colores.

**Control negativo** (demuestra que el verificador detecta la ausencia de conmutación):

```bash
EXTRA_DEFINES="-DK_NO_MODE_SWITCH=1" bash ./tools/build/build-demo.sh demos/amiga/113_mode_switch --debug
bash ./tools/run/run-demo.sh demos/amiga/113_mode_switch --config A500_k_no_mode_switch1_debug
node tools/analyze/verify-113-mode-switch.mjs out/run/113_mode_switch/A500_k_no_mode_switch1_debug/screenshot.png
# -> FAIL: HUD con 24 colores (lee los 5 planos del campo)
```

## Relación

- Orden canónico fijado por HOST-042 (`tests/host/042_mode_switch`).
- Variante conservadora sin cambio de geometría: `CopperIntentKind::BitplaneSplit`.
- Modelo: `docs/engine/architecture/PLAYFIELD_SCROLL_ARCHITECTURE.md` §4.1.
