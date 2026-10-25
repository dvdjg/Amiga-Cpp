# Tests on-target (`tests/amiga/`)

Tests de integración que corren **en el Amiga emulado** (WinUAE) y se validan por el canal lateral (`g_eng_run_status`), porque ejercitan hardware o ASM que el host no reproduce.

## Estructura

- `l0_bare_metal/`: sin SO ni servicios del engine (display básico, escalares).
- `l1_backend/`: con `AmigaBackend`/`App` (memoria, display, audio).

Cada test es una app del flujo normal (`tools/build/build-demo.sh`) con su `verify.sh`, que compila, arranca en el emulador, espera READY y **comprueba el `detail`** del run-status (bits de checks + evidencia).

## Índice

| Test | Qué valida | Evidencia (`detail`) |
|---|---|---|
| `l1_backend/001_app_display_teardown` (L1-001) | `App` compone display, materializa mundo y **detiene DMA** antes de liberar la escena; el pool Chip queda devuelto | 5 bits: DMA apagado, Chip recuperado, App arrancado, fondo materializado y añadido |
| `l1_backend/002_audio_leases` (L1-002) | Con el **mixer ASM real**: 4 canales software, rechazo por prioridad menor, reocupación tras `stop_sfx` y `AUD0EN`/DMA apagado tras `shutdown` | 6 bits + `DMACONR` leído con SFX activo en la mitad alta |
| `l1_backend/003_plane_fill_bounds` (L1-003) | Relleno por plano del Blitter con caras que tocan el borde (x=320, x=-1, y=232): recorte al rectángulo seguro del contorno (margen de 1 px por el desfase del trazador) y comparación CPU | `detail=0x1ff`: nueve bits de límites, interiores, cobertura ±2 px, fila 0 y juntas rectas/diagonales entre planos |
| `l1_backend/004_actors_cost` (L1-004) | Coste por vértice del pipeline 3D de CPU (`mesh_transform`, test de frustum, proyección) en una sonda estática, para A/B antes de optimizar | `detail` con ciclos/vértice (/4) de las tres fases |
| `l1_backend/005_render3d_corners` (L1-005) | Cuatro esquinas 3D frente a CPU; modo `--edge-stress` reproduce una pose animada congelada y compara el shared-edge del recorte fijo en ambos sentidos | Default: 6 bits de esquinas/cobertura ±2/HUD/caras; estrés: máscara de color, CPU/Blitter ±2, excepciones de borde y contador exacto de diferencias por plano |
| `l1_backend/006_director_state` (L1-006) | `SpriteDirector`: alta de 8 entidades, persistencia de `count()` tras `init` y canarios alrededor del director | `detail=0xbeef0801`: 8 entidades, checks OK y magic `0xBEEF` |

## Ejecución

```bash
bash tests/amiga/l1_backend/001_app_display_teardown/verify.sh
bash tests/amiga/l1_backend/002_audio_leases/verify.sh
bash tests/amiga/l1_backend/003_plane_fill_bounds/verify.sh
bash tests/amiga/l1_backend/004_actors_cost/verify.sh
bash tests/amiga/l1_backend/005_render3d_corners/verify.sh
bash tests/amiga/l1_backend/005_render3d_corners/verify.sh --edge-stress
bash tests/amiga/l1_backend/006_director_state/verify.sh
```

`--skip-build` reutiliza el binario ya compilado. Los puertos del emulador son los de `docs/debugging/system/debug-winuae-v2-guide.md` (`WINUAE_GDB_PORT`/`WINUAE_SIDE_CHANNEL_PORT`).

Referencias: `docs/testing/README.md` (taxonomía y IDs), `docs/build/BUILD_AND_RUN.md` (runner), `ROADMAP_API_COHERENCE.md` §F8 (origen de los canarios de audio) y `ROADMAP_DEUDA_TECNICA.md` DT-029 (arranque de la 011).
