# VALIDATION — 400_rpg_slice

Fecha de la campaña: 2026-10-17. Perfil: `A500_debug` (OCS, PAL, 512K+512K). Emulador: WinUAE-DBG del workspace.

## Medidas de rendimiento

Comando: `node tools/debug/measure-fps.mjs 400_rpg_slice A500_debug --json --duration-ms 40000` (ventana tras READY; ciclos del contador de CPU vía `$B7E928` y frames del run-status vía canal lateral).

| Estado del juego | fps emulados | ciclos/frame | campos/frame |
|---|---|---|---|
| Antes de la campaña (copia de sala por frame + cocción por pixel + parches) | 13,78 | 514 862 | 3,63 |
| Tabla ROM de tiles + publicación por cambio + borrado de estelas | 42,87–45,21 | ~155 000–165 500 | 1,09–1,17 |
| Pre-cocina de las 3 salas + banda de HUD + bucle de sondeo (estado final) | **44,7–45,7** | **155 332–158 625** | **1,095–1,118** |

- **Determinismo**: con el bucle de sondeo (`run_polling`) tres arranques consecutivos dan la **misma** medida (44,72 fps / 158 6xx ciclos) — sin la varianza (y los cuelgues) del bucle por IRQ.
- **Cocción de sala**: ~12,5 M → ~1,0 M ciclos por cocción (tabla ROM); con la pre-cocina de las tres salas en `init`, las puertas ya no cocinan (solo conmutan imagen).
- **Frames sin transición de sala**: 1,00 campo/frame; el ~1,10 medios incluye publicaciones completas al entrar en sala y la espera activa. Objetivo de 50 fps exactos: **no alcanzado** (margen pendiente).

## Verificación visual y dinámica (regla de oro)

- Secuencia de 14 capturas cada 2,5 s (canal lateral) con **14 hashes distintos**: la crónica avanza (aldea → bosque → aldea → torreón → mazmorra) y el frame final muestra `OBJ:VICTORY!` con el cofre abierto (`G60`, `POT4`).
- `node tools/analyze/vision-final.mjs 400_rpg_slice out/run/400_rpg_slice/A500_debug/sequence --demo-dir games/400_rpg_slice --frames 6` (prompt canónico, `qwen3-vl:8b`): describe la evolución del mapa y del HUD (contador, oro, inventario y objetivo cambiando) y **no reporta artefactos**. Respuesta cruda completa en [`src/VISION.md`](src/VISION.md).
- **Navegación del guion**: con el buscador del engine (`eng::util::astar` sobre la rejilla de la sala; `plan_route`/`nav_open`) el jugador rodea NPCs, árboles y lobos en movimiento. Validado en host (simulador del paseo+combate: crónica completa en 1.305 frames, igual que el walker anterior) y en vivo (secuencia de 14 frames con la crónica completa).

## Conclusiones (alcance)

- **Correcto en el alcance probado**: render de las tres salas con HUD y decorado; actores y borrado de estelas sin huecos visibles; crónica completa hasta la victoria; navegación por A* alrededor de obstáculos; 44,7–45,7 fps con ~1,10 campos/frame y cadencia determinista entre arranques.
- **Cuelgue intermitente de VBlank**: **resuelto en el juego** usando el bucle de **sondeo** (`App::run_polling`), que no depende de la IRQ VERTB (la ROM la apaga bajo AmigaDOS; ver [`212-vblank-irq-apagada-por-el-so.md`](../../docs/debugging/investigaciones/212-vblank-irq-apagada-por-el-so.md)). La deuda del engine (bucle por IRQ bajo SO vivo) sigue abierta para las demos que lo usen.
- **SIN VERIFICAR**: baseline de flicker (`tools/test-regression.sh --flicker`) no declarado; los 50 fps exactos.
- El "48,8 fps" de la primera sesión no era una medida válida del juego real: se tomó con la copia de sala **ablacionada** (`-DK_SLICE_NO_ROOM=1`) y el guion atascado (dibujaba solo parches).

