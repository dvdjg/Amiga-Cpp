# 122 - circles: informe de validación

Informe de validación de la demo (procedimiento F0-F5 de `docs/guides/methodology/PROCEDIMIENTO_DEMOS_Y_JUEGOS.md`). Pasada del port 2026-10-17.

## Estado

**VERIFICADA** en el alcance probado: build/run/analyze en verde, medida de fps en estado estable (49,87 fps / 1,003 campos/frame en `A500_debug`), secuencia + evaluación canónica de visión (`src/VISION.md`, sin artefactos: la figura es completa, simétrica y estática; el único cambio es el contador `F`) y tests host de las primitivas usadas (`HOST-232` circle/clip; `HOST-439` límites de BOB). Límites explícitos al final del informe.

## Medidas (`A500_debug`, `tools/debug/measure-fps.mjs`, 20 s)

| Versión | fps emulado | campos/frame | ciclos/frame |
|---|---|---|---|
| `Surface::circle` por píxel (`draw_span_op`) y figura completa en 2 frames (`A500_debug`) | 28,41 | 1,760 | 249.686 |
| `Playfield::draw_circle` (escritura de palabra por punto) y **un radio por frame** (`A500_debug`) | **49,87** | **1,003** | 142.245 |
| Igual, build `A500_release` (runner de máquina reutilizado; solo cambia el `.exe`) | 49,52 | 1,010 | 143.250 |

Qué explica el delta:

- El camino por píxel pagaba la cadena `circle`→lambda→`draw_span_op`→`draw_span`→`write_planes` por punto (~22k puntos para los 62 círculos) y en build de debug dominaba el frame (~150 campos en la primera versión).
- `Playfield::draw_circle` resuelve el contorno **dentro** del playfield (punto medio entero, 8 octantes, `write_planes`/`write_planes_op` por punto, sin callbacks): el coste por punto es una escritura de palabra.
- La figura se construye con **un radio por frame**; cuando termina, el frame estático no se redibuja y el coste por frame vuelve a cero. Así el arranque no contamina la ventana de medida (con el dibujo entero en 2 frames, la media de `measure-fps` seguía siendo 28 fps).

Estado estable: 996 frames medidos con `fieldsPerFrame=1.00159` (sin tirones ni frames de 2 campos tras el arranque). La construcción (62 radios) consume ~2 s de tiempo emulado y ocurre antes de `mark_ready`; el runner espera a READY, así que la captura y la secuencia son de la figura completa.

## Validación visual (secuencia + visión)

- Secuencia: `tools/run/run-demo.sh demos/techniques/amiga/effects/122_circles --sequence-frames 6 --sequence-interval-ms 300`; evaluación canónica con `tools/analyze/vision-final.mjs` (prompt verbatim en `src/VISION.md`, modelo `qwen3-vl:8b-instruct-q8_0`).
- Respuesta (resumen fiel de la cruda): patrón **circular** de múltiples anillos concéntricos blancos y negros con efecto de rayos; texto `CIRCLES v1.0.<build> F <n>` abajo a la izquierda; entre imágenes solo cambia el contador `F`; «no hay parpadeos, recortes, artefactos visibles ni elementos superpuestos. El patrón circular es completo y simétrico».
- Comprobación propia: captura final (figura completa, HUD legible) y lectura de registros/bounds en los tests host (abajo).

## Parpadeo

El detector (`node tools/vision-review/flicker-check.mjs --demo demos/techniques/amiga/effects/122_circles`) informa 2 candidatos en la franja inferior: son los **dígitos del contador `F` del HUD**, la única zona que cambia entre frames (`frame-diff` con bbox x≈396..448, y≈520..545 en la captura 2x, dentro de la línea del HUD). La figura (círculo y fondo) no cambia: el frame-diff fuera del HUD es 0. No se declara `flicker-baseline.json` porque la herramienta exige compensación de movimiento global para validar baselines y en una imagen **estática** esa compensación no aplica (`analysis-unavailable`); queda registrado como límite de herramienta, no como defecto de la demo.

## Tests host de las primitivas (no es solo la captura)

- **HOST-232** (`tests/host/field/232_draw_target`): `Screen::circle` sobre `DrawTarget`/`Surface` con 4 planos — extremos horizontales/verticales, diagonal del octante, el centro NO se pinta (contorno), radio 0 pinta el centro y un círculo fuera del clip no dibuja ni falla.
- **HOST-439** (`tests/host/graphics/439_bob_render_matrix`): `bob_draw`/`bob_erase_box` rechazan cajas que no caben en la geometría del destino (120+12>128; 58+8>64), reserva más corta que la geometría (`planes.size()` < `storage_bytes_used()`), y aceptan el límite exacto (x=112 con 1 palabra) y rechazan el caso con shift (x=116 → 2 palabras), con canarios de memoria intactos.
- **HOST-388** (`tests/host/graphics/388_frame_plan_state`): `begin_blit_job`/`begin_line_job` con el `FramePlan` lleno devuelven una ranura **dentro** del plan (descarte) y el `commit` falla, sin escribir en `m_blit_jobs[max]`.

## Coherencia de setup

- `g_eng_run_status` con `detail=0x12200` y `mark_ready` **después** de completar la figura (el runner y la captura ven la imagen final).
- Display 320×256×1 por `GameDisplay` + `App::start()` (composición validada: planos, Chip y presupuesto de bus); sin registros ni copper en la demo.
- Paleta `{0x000, 0xfff}` (negro/blanco del original).

## Alcance y límites

Cubierto: figura completa en estado estable, fps/fields en `A500_debug` y `A500_release`, clip del círculo por caja envolvente y puntos (`HOST-232`), límites de BOB y del `FramePlan` (`HOST-388`/`HOST-439`), y visión canónica. **SIN VERIFICAR**: mapeo del círculo sobre playfields con espejo/interleaved distintos del contiguo del test, el coste por frame de la **construcción** progresiva (ocurre antes de READY y no entra en la ventana de medida) y la cadencia de esos frames iniciales. El candidato de flicker del HUD es esperado (contador); la figura no cambia.
