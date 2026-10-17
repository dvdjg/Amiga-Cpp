# VALIDATION — 400_rpg_slice

Fecha de la campaña: 2026-10-17. Perfil: `A500_debug` (OCS, PAL, 512K+512K). Emulador: WinUAE-DBG del workspace.

## Medidas de rendimiento

Comando: `node tools/debug/measure-fps.mjs 400_rpg_slice A500_debug --json --duration-ms 40000` (ventana tras READY; ciclos del contador de CPU vía `$B7E928` y frames del run-status vía canal lateral).

| Estado del juego | fps emulados | ciclos/frame | campos/frame |
|---|---|---|---|
| Antes de la campaña (copia de sala por frame + cocción por pixel + parches) | 13,78 | 514 862 | 3,63 |
| Tabla ROM de tiles + publicación por cambio + borrado de estelas | 42,87–45,21 | ~155 000–165 500 | 1,09–1,17 |
| Pre-cocina de las 3 salas + banda de HUD + bucle de sondeo (estado final) | **44,7–45,7** | **155 332–158 625** | **1,095–1,118** |
| Instrumentación `ENG_PROF` + cache de rutas del guion (estado actual) | **45,97** | **154 324** | **1,088** |
| + lote de casillas del render (`BlobBatch`: 2 rachas en vez de ~20 esperas por paso) | **46,32** | **153 153** | **1,079** |

- **Determinismo**: con el bucle de sondeo (`run_polling`) tres arranques consecutivos dan la **misma** medida (44,72 fps / 158 6xx ciclos) — sin la varianza (y los cuelgues) del bucle por IRQ.
- **Cocción de sala**: ~12,5 M → ~1,0 M ciclos por cocción (tabla ROM); con la pre-cocina de las tres salas en `init`, las puertas ya no cocinan (solo conmutan imagen).
- **Frames sin transición de sala**: 1,00 campo/frame; el ~1,10 medios incluye publicaciones completas al entrar en sala y la espera activa. Objetivo de 50 fps exactos: **no alcanzado** (margen pendiente).
- **Cadencia medida por checkpoints del periférico** (slot 0 por frame; deltas exactos dentro del emulador) y reparto por secciones (`ENG_PROF` + `profile.mjs`): el bucle sondea la línea 311 una vez por campo; el periodo medio es 149,5–157,4 k ciclos con **5–9 % de frames que doblan** (el trabajo rebasa el campo y la espera cae al siguiente). Con el fix de replans (cache de rutas del guion + tapón cada 4 frames) los skips de patrulla bajan **5,1 %→3,5 %**, y con el lote de casillas del render a **2,71 %** — idéntico al **suelo ambiental 2,70 %** medido con la ablación total de estelas/estampado: el juego ya no aporta dobles frames en patrulla; el residual es del entorno (AmigaDOS vivo + re-arme de VERTB; ficha 212). Los frames de evento de la crónica (publicación síncrona ~745 k de tiempo de bus) son el resto del margen, **SIN VERIFICAR/abierto** con el instrumento montado (detalle y plan en la ficha 400 §7).

## Verificación visual y dinámica (regla de oro)

- Secuencia de 14 capturas cada 2,5 s (canal lateral) con **14 hashes distintos**: la crónica avanza (aldea → bosque → aldea → torreón → mazmorra) y el frame final muestra `OBJ:VICTORY!` con el cofre abierto (`G60`, `POT4`).
- `node tools/analyze/vision-final.mjs 400_rpg_slice out/run/400_rpg_slice/A500_debug/sequence --demo-dir games/400_rpg_slice --frames 6` (prompt canónico, `qwen3-vl:8b`): describe la evolución del mapa y del HUD (contador, oro, inventario y objetivo cambiando) y **no reporta artefactos**. Respuesta cruda completa en [`src/VISION.md`](src/VISION.md).
- **Navegación del guion**: con el buscador del engine (`eng::util::astar` sobre la rejilla de la sala; `plan_route`/`nav_open`) el jugador rodea NPCs, árboles y lobos en movimiento. Validado en host (simulador del paseo+combate: crónica completa en 1.305 frames, igual que el walker anterior) y en vivo (secuencia de 14 frames con la crónica completa).

## Flicker

- Captura por **pasos de frame** (1 frame entre capturas, breakpoint del ready probe; `run.args` la fija en 60 frames para el gate): 50/59 pares congelados, 9 pares con cambio **discreto** (salto de casilla del paseo cada 8 frames) y **sin oscilación A-B-A**.
- Defecto encontrado y corregido en la campaña: el contador del HUD alternaba en pantalla **dos instantáneas** (41/49) porque la republicación de la banda repartía la versión nueva a un solo buffer (conteo a ciegas de 2 renders; un evento podía cortarlo). Se sustituyó por **versión por buffer** (`m_hud_ver`/`m_hud_shown[]` y `App::screen_buffer_index()`): cada buffer repone la banda la primera vez que vuelve a ser trasero.
- `games/400_rpg_slice/flicker-baseline.json`: `max_candidates 7` (5 saltos de casilla + 2 del pintado inicial del HUD), declarado tras la corrección; el gate `tools/test-regression.sh --flicker --require-flicker-ok --demo games/400_rpg_slice` queda en verde. La herramienta acepta escenas estáticas (`no_global_translation`) como analizables cuando hay baseline.

## Conclusiones (alcance)

- **Correcto en el alcance probado**: render de las tres salas con HUD y decorado; actores y borrado de estelas sin huecos visibles; crónica completa hasta la victoria; navegación por A* alrededor de obstáculos; 44,7-45,7 fps con ~1,10 campos/frame y cadencia determinista entre arranques.
- **Cuelgue intermitente de VBlank**: **resuelto en el juego** usando el bucle de **sondeo** (`App::run_polling`), que no depende de la IRQ VERTB (la ROM la apaga bajo AmigaDOS; ver [`212-vblank-irq-apagada-por-el-so.md`](../../docs/debugging/investigaciones/212-vblank-irq-apagada-por-el-so.md)). La deuda del engine (bucle por IRQ bajo SO vivo) sigue abierta para las demos que lo usen.
- **SIN VERIFICAR**: los 50 fps exactos.
- El "48,8 fps" de la primera sesión no era una medida válida del juego real: se tomó con la copia de sala **ablacionada** (`-DK_SLICE_NO_ROOM=1`) y el guion atascado (dibujaba solo parches).

