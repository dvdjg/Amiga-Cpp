# VALIDATION — 001_rpg_dungeon (capa RPG, M1+M2)

Informe de validación de la demo: medidas reproducibles, evaluación de visión con el prompt canónico y **alcance declarado** (qué queda sin verificar). Fecha de la campaña: 2026-10-17 (A500_debug, WinUAE-DBG del repo).

## HUD de host

`HostHud` muestra nombre + número + versión + frame + fps en el overlay. El HUD de juego de la banda inferior se reduce a dos líneas (inventario/ataque/hambre y objetivo); ya no duplica la identidad ni el contador de frames. El bucle usa `app.run_polling()` para evitar el guard `0xE212` bajo SO vivo.

## Medidas

| Medida | Valor | Método |
|---|---|---|
| fps (motor, 20 s, HUD de host) | **48,39 fps** (host 48,35) | `node tools/debug/measure-fps.mjs rpg_amiga_001_rpg_dungeon A500_debug --json` → `emulatedFps 48.39`, `cyclesPerFrame 146588`, `fieldsPerFrame 1.033` |
| fps (secuencia completa) | ~48 fps estables de F28 a F1083 | 10 capturas por intervalo (2,5 s) del `run-report.json`: deltas 112–122 frames por intervalo, sin caídas ni atascos |
| Presupuesto | 147,6k ciclos/frame vs 141,9k de un campo PAL | medida con contador de ciclos del periférico de depuración |
| Cadencia | uniforme (sin frame de 2 campos en régimen) | deltas de la secuencia; los frames de **recomposición del HUD** (~8 en el guion) cuestan ~5 campos aislados |
| Flicker (gate formal) | **OK** con `flicker-baseline.json` (`max_candidates: 2`) | `bash tools/test-regression.sh --flicker --require-flicker-ok --demo demos/features/rpg/amiga/001_rpg_dungeon` → Regression OK; secuencia de captura por **frame consecutivo**: 8/9 pares congelados y el único cambio es el **paso discreto de casilla** (16 px), que el detector marca como 2 candidatos de score bajo (justificado en el baseline) |

La sala (parches de sprites + publicar el HUD) mide **≈0 sobre la base** (el build sin escena da 142,1k ciclos/frame, 49,9 fps): el coste por frame es el presupuesto mínimo del bucle de juego.

## Visión (modelo local, prompt canónico)

Ejecución: `node tools/analyze/vision-final.mjs rpg_amiga_001_rpg_dungeon <dirSeq> --frames 5`. Respuestas crudas y prompt verbatim: `src/VISION.md` (generado por la herramienta).

Lectura de las respuestas frente a la **intención** (`src/INTENT.md`):

- El modelo describe la escena como sala con marco gris, fondo negro, personaje rojo con parte amarilla que **se mueve** entre frames, bloques (yunque, cofre, objetos) y banda inferior con texto de estado — coincide con lo pretendido.
- Detecta la desaparición del bloque naranja del borde derecho (la puerta al abrirse) y de los objetos del suelo al recogerse, y el cambio de estado (`ESCAPED!` en los últimos frames) — la dinámica de la historia es la buscada.
- Ruido de OCR conocido (no afecta a la conclusión): el modelo lee con errores algunos dígitos (`hambre 448` por 41, `ATK 54` por 5 en el primer frame, `OPT` por `OBJ`).

## Límites declarados (SIN VERIFICAR)

- **Pad humano**: los verbos con input real no se re-ejecutan en esta campaña (solo el guion determinista). El control manual comparte el mismo `try_step`/`interact` probado.
- **Créditos de partida**: esta demo no incluye save/load en el juego (el save por componente está verificado en HOST-457).

## Hallazgos de engine medidos en esta campaña (registrados como DT-025)

1. **Cada job del plan de Blitter paga sincronización con el Blitter**: medido ~15k ciclos/job (`s.fill`/`s.line`/`fill_box`). Una escena de ~20 rectángulos por frame por el plan baja a ~2-4 fps; la racha de streaming (`clear_now`/`copy_now`) con el mismo trabajo se mide **gratis** (1 job viable por frame sin coste observable). Guía práctica: pocos jobs por frame; para muchos objetos pequeños, streaming o fondo prerenderizado.
2. **`draw_text_words` por frame** (3 líneas de 40 caracteres, 4 planos) cuesta ~6 campos por frame en esta escena (≈750k ciclos): suficiente para desestabilizar la cadena de espera de VBlank si alterna frames rápidos y lentos. La tira Chip + `copy_now` (esta demo) compone una sola vez por cambio de estado y publica por blits: 48 fps sin alternancia.
3. **Herramientas**: durante la campaña `measure-fps` falló de forma intermitente leyendo `$b7e928` (timeout GDB) y el runner dejó `screenshot.png` obsoleto con `missing_screenshot`; ambas cosas ya están en la familia de DT-023/DT-024 y **no se tocaron** en este hilo (instancias huérfanas limpiadas por PID).

## Reproducir

```bash bash ./tools/build/build-demo.sh demos/features/rpg/amiga/001_rpg_dungeon --debug --clean bash ./tools/run/run-demo.sh demos/features/rpg/amiga/001_rpg_dungeon --sequence-frames 10 --sequence-interval-ms 2500 node tools/debug/measure-fps.mjs rpg_amiga_001_rpg_dungeon A500_debug --json node tools/analyze/vision-final.mjs rpg_amiga_001_rpg_dungeon out/run/rpg_amiga_001_rpg_dungeon/A500_debug/sequence --frames 5 --demo-dir demos/features/rpg/amiga/001_rpg_dungeon
```

