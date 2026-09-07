# Roadmap: sistema de pruebas unificado del engine

Documento maestro para **planificar y, más adelante, generar** pruebas que cubran el motor (`engine/`) de forma coherente: mismas trazas en memoria, mismos criterios de evidencia visual para visión automática, y escalado hacia **escenarios tipo videojuego** cuando la combinatoria explote.

## Parent links

- [Spec batería](amiga-test-battery-spec.md)
- [Matriz subsistema ↔ casos](engine-test-battery-matrix.md)
- [Subsistemas del engine](engine-subsystems.md)
- [Harness común](../tests/amiga-battery/common/README.md)
- [battery_evidence.h](../tests/amiga-battery/common/include/battery_evidence.h)

---

## 1. Principios (no negociables)

1. **Un ejecutable = un caso** (como hoy): se compila con `BATTERY_CASE=…`, produce `out/battery_<Caso>.exe` y `case.json` cuando entre al pipeline automatizado.
2. **Trazas en CHIP/Fast visibles para GDB/MCP**: el patrón actual es obligatorio para todo test del engine:
   - `g_battery_evidence_log` ([`BatteryEvidenceLog`](../tests/amiga-battery/common/include/battery_evidence.h)): `magic = 'BTEV'`, `assert_failures`, `last_stage`, anillo de eventos, `final_status`.
   - `g_battery_runtime_state` (`BRTS`): `stage_id` / `detail` actualizados con `battery_runtime_mark()`.
   - Eventos explícitos: `battery_evidence_event()`, fallos lógicos: `battery_evidence_assert(cond, code, arg)`.
   - Opcional pero recomendado: `battery_sys_trace_beam(custom, code)` en puntos de sincronía con el haz.
3. **Repercusión visual en pantalla** para **todos** los casos, incluido audio:
   - El modelo de visión solo ve **píxeles**; el sonido debe traducirse a **estado gráfico** (barras, contador de frame del envelope, banda de color que cambia con el pico, texto en overlay).
4. **Texto para visión** (`expected-visual` en `case.json` o equivalente): una frase estable que describa la captura **golden** esperada (como en B01/S01).
5. **API pública del engine**: el cuerpo del test debe llamar a `engine_*` / módulos de `engine_extensions.h` cuando el objetivo sea validar el motor; acceso directo a `custom` solo como último recurso y documentado en `docs/technique.md`.

---

## 2. Niveles de prueba (taxonomía)

| Nivel | Nombre | Objetivo | Duración típica | Combina APIs |
|-------|--------|----------|------------------|--------------|
| **U** | Micro / unitario-Amiga | Una función o invariante (p. ej. `engine_tilemap_cell_offset`, resultado de `engine_fixmath_mul`) con salida reflejada en **código de color** o 4 dígitos en pantalla | 1–3 s tras estabilizar frame | No |
| **F** | Funcional | Todo un subsistema (blitter copy, sprite, copper list, DOS slurp) con setup real CHIP | 3–30 s | Mínimo (solo helpers) |
| **T** | Técnica de juego | Escenario creíble: scroll + BOBs, o copper + audio + input, o playfield + sprite priority | Varias decenas de frames | Sí (controlada) |
| **I** | Integración producto | Flujo cercano a la app: menú → TakeSystem → efecto (fuera del scope estricto “solo engine”, pero regresión global) | Según app | Alta |

**Nota sobre “unitario” en cruzado m68k:** no hay gtest en host para la mayoría del código; el **unitario** aquí significa *binario mínimo que aísla una API* y falla con `battery_evidence_assert` + etapa bloqueada, no necesariamente sin hardware.

---

## 3. Contrato de huella visual + visión

Cada caso debe definir en su `README.md` / `case.json`:

| Campo | Uso |
|--------|-----|
| **Regiones de interés** | Rectángulos en coords lores (p. ej. “barra de estado abajo”, “viewport 160×128 centrado”) para recortes en pipelines automáticos. |
| **Paleta firma** | Colores reservados al test (p. ej. magenta = fallo, verde = PASS) codificados en copper al final del setup. |
| **Patrón “PASS frame”** | Tras la última aserción exitosa, dibujar un patrón único (2×2 píxeles, borde) para correlacionar con `last_stage` en memoria. |
| **Audio → vídeo** | Mientras suene un tono o sample: actualizar cada VBL un **indicador** (altura de barra, `engine_debug_text` con tick, o banda copper). Silencio = gris; tono = amarillo; clipping = rojo. |
| **expected-visual** | Descripción en lenguaje natural para el modelo multimodal (consistente entre ejecuciones). |

---

## 4. Roadmap por fases (qué implementar y en qué orden)

### Fase 0 — Infra unificada (motor de tests)

- [ ] Documentar en cada caso nuevo la **dirección de `g_battery_evidence_log`** (símbolo en `.map`) para lectura MCP sin adivinar.
- [ ] Plantilla `_template` ampliada: sección “Engine test checklist” (trazas + visual + `expected-visual`).
- [ ] Script o meta-target `make engine-tests` *(plan)* que liste solo casos cuyo `README` declare `engine_test_tier: U|F|T`.
- [ ] Criterio de **cierre de fase**: al menos un caso **U** y uno **F** nuevos cumplen el contrato §1 al 100 %.

### Fase 1 — Núcleo sistema / memoria / diag

| ID propuesto | Nivel | API / tema | Trazas mínimas | Huella visual |
|--------------|-------|------------|----------------|---------------|
| EU01 | U | `engine_get_frame_counter` + `engine_clock_*` | `last_stage` tras N VBL == esperado | Contador en overlay o dígitos `engine_font` |
| EU02 | U | `engine_rand` determinista (seed fija) | Assert secuencia de 8 valores conocidos | Colores por valor en cuadrícula 2×4 |
| EU03 | F | `engine_alloc` / `engine_free` + `ENGINE_MEM_TRACE` | `assert_failures==0`, evento de pico memoria | Barra proporcional a `engine_mem_trace_report` reflejada en color |
| EU04 | F | `ENGINE_DIAG` / `ENGINE_TRACE_*` | Evento al cerrar bloque | Línea de texto debug visible una frame |

### Fase 2 — Display, copper, blitter, sprites

| ID propuesto | Nivel | API / tema | Notas |
|--------------|-------|------------|--------|
| EF01–EF06 | F | Ya cubiertos en gran parte por **B01–B06**, **C01**, **S01** | **Acción**: migrar progresivamente a llamadas `engine_blit_*` / `engine_copper_*` sin duplicar lógica; alinear `expected_stage_id` con matriz. |
| EF07 | F | `engine_copper_double_*` + `engine_copper_list_finish` | Dos listas alternas, parpadeo de COLOR0 o DIW alterno documentado |
| EF08 | F | `engine_sprite_*` + `engine_sprite_disable` | S01 + variante que desactiva sprite y assert de “no columna” |
| EF09 | F | `engine_view_finescroll_*` + `engine_viewport_calc_mods` | Franjas que se mueven con scroll fino |

### Fase 3 — Input, joy, automatización, DOS

| ID propuesto | Nivel | API / tema | Huella visual |
|--------------|-------|------------|---------------|
| EI01 | F | `engine_input_edges_sync` + `engine_key_held` | Cuadrante iluminado según flanco simulado (`g_automation_input`) |
| EI02 | F | `engine_joy_*` | Cruces direccionales dibujadas según máscara; fire cambia borde |
| EI03 | F | `engine_dos_file_slurp` (ADF con fichero test) | Texto “OK”/“ERR” + código en pantalla según retorno |
| EI04 | U | `engine_tilemap_cell_offset` valores | Grid de celdas coloreadas por índice calculado |

### Fase 4 — Audio (siempre con vídeo)

| ID propuesto | Nivel | API / tema | Huella visual obligatoria |
|--------------|-------|------------|----------------------------|
| EA01 | F | `engine_audio_paula_mute` + tono manual breve (AUDx) vía wrapper test | Barra vertical proporcional a volumen leído o a “tick de muestra” |
| EA02 | T | *(plan)* Replayer externo enlazado + `engine_audio_set_backend` | Oscilograma simplificado en 1 bitplane o barras copper |

### Fase 5 — Técnicas de videojuego (combinatoria controlada)

Objetivo: **no** explotar el producto cartesiano; cada caso T tiene una **hipótesis** clara y un **máximo de 3–4 knobs**.

| ID propuesto | Escenario | APIs mezcladas | Criterio de éxito |
|--------------|-----------|----------------|-------------------|
| ET01 | **Shooter mínimo** | sprite enemigo + blitter clear fondo + scroll fino | Enemigo visible; fondo se restaura; `assert_failures==0` |
| ET02 | **Plataforma tile** | tilemap offset + blit copy tile a buffer | Plataforma reconocible en captura; stage final = “map_ok” |
| ET03 | **HUD + audio** | font + contador vidas + barra audio | Visión ve HUD estable y barra reacciona al tono |
| ET04 | **Input combo** | edges + joy + ratón (automation) | Secuencia de iconos encendidos en orden |
| ET05 | **Doble copper + BOB** | `engine_copper_double` + `engine_blit_bob` | Sin tearing documentado; captura en 2 frames consecutivos |

**Gestión combinatoria:** nuevas combinaciones solo entran si (a) falla un bug real, o (b) cubren una **plantilla** no representada (tabla de “plantillas T” a mantener ≤ 15 casos activos recomendados).

### Fase 6 — Regresión automática y visión

- [ ] Directorio `evidence/golden/` por caso *(plan)* con PNG de referencia opcional.
- [ ] Pipeline: build → ADF/carga → N frames → `winuae_screenshot` + lectura `g_battery_evidence_log` → fallo si `magic`/`assert_failures`/`last_stage` no coinciden.
- [ ] Segunda puerta: modelo de visión con `expected-visual` + recorte ROI.
- [ ] Publicar `engine-test-report.json` agregado *(plan)* con resumen por fase.

---

## 5. Asignación de IDs y convención de etapas

- Prefijo **`E`** + letra de área + número: `EU` utilidades, `EF` framebuffer/copper/blit, `EI` input/io, `EA` audio, `ET` técnica.
- **Stages** (`battery_runtime_mark`): usar rangos por área para depuración rápida, p. ej. `0xE0xx` utilidades, `0xE1xx` display, `0xE2xx` input, `0xE3xx` audio, `0xE5xx` técnica.
- **`case.json`**: mantener `evidence.runtime_expectations.expected_stage_id` alineado con la etapa estable **después** del setup válido (patrón B01 `0xB005`, S01 `0x5012`).

---

## 6. Qué reutilizar del código actual (referencia de “grado de feedback”)

- **`battery_case_run`**: ya resetea evidencia y marca etapas 1→2→3 ([`battery_runtime.c`](../tests/amiga-battery/common/src/battery_runtime.c)).
- **`battery_evidence_assert`**: incrementa `assert_failures` y registra evento `0x3000....` — el runner automático debe fallar si ≠ 0.
- **Capturas multi-frame**: patrón B01 `capture_presented_frames` + `battery_sys_trace_beam`.
- **Salida serial / pasos**: `battery_output_step` para correlación con logs host.

---

## 7. Límites y decisiones explícitas

- **Tests solo en host (Linux/Windows nativo)** para `fixmath`/`tilemap` puramente aritméticos: opcional y *no* sustituye el caso Amiga si la API toca tipos del SDK; si se añaden, deben vivir en carpeta separada (`tests/host/` *(plan)*) con otro Makefile.
- **Casos metal** (sin OS): solo si el test no requiere `engine_init` completo; documentar divergencia del contrato `BTEV` si no hay Exec.
- **Coste total**: la meta razonable es **< 40 ejecutables engine-centric** activos; el resto son variantes parametrizadas o datos externos.

---

## 8. Siguiente acción recomendada

1. Aprobar esta taxonomía **U / F / T** y el prefijo de IDs **`E*`**.
2. Añadir columna **Tier** a [engine-test-battery-matrix.md](engine-test-battery-matrix.md) al ir cerrando casos.
3. Implementar **EU01** (reloj + contador visual) como plantilla mínima copiable para el resto de micro-tests.

Con este documento se puede derivar tickets concretos (“implementar EF07 con ROI y golden string”) y enchufarlos al runner existente (`scripts/run-battery-*.mjs`, MCP) sin rediseñar el harness desde cero.

---

## Actualización 2026-04-06

- Se crea `EU01_clock_counter` en `tests/amiga-battery/` como primer micro-test U del roadmap.
- Se ejecuta validación viva por ruta ADF con `stage_id=0xE005` y `assert_failures=0` en `tests/amiga-battery/EU01_clock_counter/evidence/adf-live-validation-summary.json`.
- Estado actual: `PARCIAL` por cierre conservador mientras se estabiliza la evidencia visual multi-frame del contador incremental y se elimina la intermitencia de reconexión (`bitmap_decode`/`ECONNRESET`) observada en la tanda.
- Se abre el primer lote pequeño adicional del engine con `I01_input_edges`, `H01_custom_peek_poke` y `F01_font_digits`. Los tres casos ya existen en `tests/amiga-battery/` con `README.md`, `case.json`, `docs/technique.md`, `evidence/README.md` y `src/main.c`, y los tres compilan por ruta debug A500 (`out/battery_I01.exe`, `out/battery_H01.exe`, `out/battery_F01.exe`).
- `F01_font_digits` queda validado en vivo por ADF con `stage_id=0xE405`, `case_tag=F01`, `assert_failures=0` y evidencia visual consistente en `tests/amiga-battery/F01_font_digits/evidence/live-validation-summary.json`.
- `I01_input_edges` queda validado en vivo por ADF con `stage_id=0xE205`, `case_tag=I01`, `assert_failures=0` y patrón visual estable en `tests/amiga-battery/I01_input_edges/evidence/live-validation-summary.json`.
- `H01_custom_peek_poke` se corrige para evitar readback lógico sobre `COLORxx` (write-only/no fiable en OCS) y pasa a validar escritura visual de paleta más lectura de `DMACONR/VPOSR/VHPOSR`. La pasada viva queda cerrada con `stage_id=0xE305`, `case_tag=H01` y `assert_failures=0` en `tests/amiga-battery/H01_custom_peek_poke/evidence/live-validation-summary.json`.
- Durante el cierre del lote se corrige una carrera en `scripts/create-adf.bat` y `scripts/create-adf.sh`: el temporal fijo `bootcase.exe` podía mezclar binarios distintos al ejecutar varias validaciones en paralelo. Ahora el temporal se deriva del ADF destino.
- Se abre y cierra `CS01_cpu_sprite_1bpl` como primer piloto de la politica "low-level parametrico + retained": la primitive `engine_cpu_sprite_blit_1bpl` ya vive en `engine/` y el caso pasa en vivo por ADF con `stage_id=0xE505`, `case_tag=CS01` y `assert_failures=0` en `tests/amiga-battery/CS01_cpu_sprite_1bpl/evidence/live-validation-summary.json`.
- Se abre y cierra `CS02_cpu_sprite_4bpl_masked` como segundo paso low-level para sprites CPU: `engine_cpu_sprite_blit_4bpl_masked` valida dibujo masked 4bpl sobre fondo estable, manteniendo `mask` y `undraw` desacoplados. La pasada viva queda cerrada con `stage_id=0xE605`, `case_tag=CS02` y `assert_failures=0` en `tests/amiga-battery/CS02_cpu_sprite_4bpl_masked/evidence/live-validation-summary.json`.
- Se abre y cierra `CS03_scene_sprite_scroll` como primer paso retained de mundo: `engine_scene_cpu_sprite_*` y `engine_scene_present` validan anclaje world-space, adaptacion por scroll y `dirty_rect` minimo sobre un playfield 1bpl. La pasada viva queda cerrada con `stage_id=0xE705`, `case_tag=CS03` y `assert_failures=0` en `tests/amiga-battery/CS03_scene_sprite_scroll/evidence/live-validation-summary.json`.
- Se abre y cierra `CS04_scene_overlay_fixed` como primer paso retained de pantalla: `engine_scene_cpu_sprite_set_anchor(...SCREEN)` valida overlay fijo sobre un mundo con scroll, manteniendo `screen_x/screen_y` constantes y `dirty_rect` minimo. La pasada viva queda cerrada con `stage_id=0xE805`, `case_tag=CS04` y `assert_failures=0` en `tests/amiga-battery/CS04_scene_overlay_fixed/evidence/live-validation-summary.json`.
- Se abre y cierra `CS06_scene_overlay_fixed_32c` como variante rica del overlay fijo: hereda el contrato 5bpl de `T02`, valida `engine_cpu_sprite_blit_interleaved_masked` sobre retained `SCREEN`, deja `stage_id=0xEA05`, `case_tag=CS06`, `assert_failures=0` y una secuencia animada de 60 frames en `tests/amiga-battery/CS06_scene_overlay_fixed_32c/evidence/sequence/sequence-lossless.apng`.
