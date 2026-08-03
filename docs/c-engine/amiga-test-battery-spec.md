# Especificación: batería de pruebas gráficas y de hardware (Amiga)

Documento maestro para implementar **tests reproducibles** (código + capturas + notas), alineado con el MCP WinUAE, el engine y las fichas de [techniques/README.md](techniques/README.md). Las entradas marcadas **(plan)** aún no tienen carpeta en el repo: sirven como **backlog priorizado** para la IA y el desarrollador.

---

## 1. Objetivos

1. Cada prueba debe poder **compilarse**, **arrancar en emulador** (ADF o carga según el caso) y generar **evidencia** (al menos una captura PNG).
2. **Cobertura del engine**: la matriz [engine-test-battery-matrix.md](engine-test-battery-matrix.md) enlaza cada subsistema reusable con un caso previsto o existente; al añadir API en `engine/`, actualizar la matriz y el caso de prueba asociado. El plan detallado de niveles de test, trazas y visión está en [engine-unified-test-roadmap.md](engine-unified-test-roadmap.md).
3. La IA puede automatizar: build, despliegue, lectura/escritura de memoria y registros, depuración paso a paso, reconocimiento de gráficos (cuando exista la herramienta) — ver §2 y [mcp-live-coding-workflow.md](mcp-live-coding-workflow.md).
4. Documentar **límites por máquina** (OCS/ECS vs AGA): usar [amiga-chipset-matrix.md](amiga-chipset-matrix.md).
5. Cualquier caso que toque hardware/kickstart/kernel debe incluir respaldo técnico en `docs/technique.md` con referencias objetivas (AHRM, RKM/NDK, autodocs, etc.).

## 1.1 Compatibilidad base del kernel y del loader

Para la parte de kernel, DOS y carga de binarios, esta batería debe interpretar sus decisiones con esta prioridad:

| Entorno | Prioridad | Regla |
|--------|-----------|-------|
| **Amiga 500 + Kickstart 1.3** | **Base principal** | La ruta de carga y verificación debe ser correcta aquí antes de darse por buena. |
| **Amiga 600** | Compatible en gran parte | Puede compartir muchas rutas del A500, pero no sustituye a la referencia base. |
| **Amiga 1200 / CD32** | Extensión posterior | Sirve para AGA, más memoria y rutas ampliadas, pero no debe dictar el loader base. |

Regla práctica:

- si un caso usa una API o una estrategia de loader pensada para ROMs/DOS más nuevos, debe marcarlo explícitamente en su `README.md`;
- si no se indica nada, debe asumirse que la expectativa es **A500 + Kickstart 1.3**.

---

## 2. Capacidades de la IA sobre el Amiga emulado (objetivo y estado)

La intención es que la IA pueda **operar el emulador como un banco de pruebas y depurador completo**. Lo que ya existe en **mcp-winuae-emu** se marca como **hecho**; lo que requiere ampliar el MCP, el monitor GDB de WinUAE o el fork WinUAE se marca como **desarrollo**.

### 2.1 Medios y binarios

| Capacidad | Descripción | Estado |
|-----------|-------------|--------|
| **ADF en caliente** | Montar o sustituir imagen de disco **sin reiniciar** la sesión cuando el emulador lo permita (cambio de DF0:, etc.). | **Parcial:** `winuae_insert_disk` / eyección; en sesión conectada puede usarse monitor según build. La matriz reproducible `mcp-winuae-emu/scripts/a-mcp-01-matrix.mjs` ya documenta `connect` vs `connect_existing` × DF0-DF3 y genera evidencia real; el hot-swap en `connect_existing` funciona en DF0-DF3 sobre una sesión externa visible. **Desarrollo:** la reutilización entre turnos tras `disconnect(false)` sigue siendo dependiente del build, así que todavía conviene documentar esos límites (reboot vs hot-swap) y ampliar comandos en WinUAE-DBG si hace falta. |
| **Carga del binario del cross-compiler** | Volcar `out/a.exe` (u otro) en memoria Amiga y opcionalmente fijar PC/SP (`winuae_load`, `winuae_run_program`, `winuae_memory_write`, `winuae_exec_chunk`). | **Parcial:** `winuae_load` ya soporta un subconjunto práctico de AmigaHunk con `RELOC32`, pero falta validación viva con WinUAE para dar por cerrada la ruta completa del toolchain. |

### 2.2 Lectura: instantánea de la máquina

| Capacidad | Descripción | Estado |
|-----------|-------------|--------|
| **Memoria** | Leer cualquier rango (`winuae_memory_read`, `winuae_memory_dump`). | **Hecho** |
| **Registros custom** | Instantánea de $DFF000– (playfield, copper, Paula, blitter, sprites, DMA…) (`winuae_custom_registers`). | **Hecho** |
| **Copper** | Desensamblar lista en una dirección (`winuae_copper_disassemble`). | **Hecho** |
| **CPU** | Registros m68k D0–D7, A0–A7, SR, PC (`winuae_registers_get`). | **Hecho** |
| **Instantánea única “todo el estado”** | Un solo resultado (o JSON) con: CPU + bloque custom + metadatos (frame, PC) + opcionalmente rangos RAM configurables (chip/fast). | **Hecho:** `winuae_machine_snapshot` existe en el MCP, compone CPU + custom + ventanas opcionales chip/fast RAM y quedó validado en vivo en este workspace. Cada ventana se limita a 16 KiB por seguridad, se lee en chunks para evitar resets del stub y puede devolver errores por región sin romper el snapshot completo. |

### 2.3 Escritura y control de ejecución

| Capacidad | Descripción | Estado |
|-----------|-------------|--------|
| **Escribir memoria** | Parches, datos, trozos de código (`winuae_memory_write`). | **Hecho** |
| **Escribir registros CPU** | Forzar PC, SP, datos (`winuae_registers_set`). | **Hecho** |
| **Depuración** | Breakpoints, watchpoints, `continue`, `pause`, **paso a paso por instrucción** (`winuae_step`), esperar parada (`winuae_wait_stop`), desensamblado (`winuae_disassemble_full`). | **Hecho** |
| **Entrada programática dentro de la app** | Escribir el buffer `g_automation_input` del engine para simular ratón, teclado y joystick por coordenadas/flags directamente en el programa, sin depender solo del input host de WinUAE. | **Parcial:** `mcp-winuae-emu` expone helpers sobre `g_automation_input` y `g_automation_enter_demo`; falta validación viva amplia y adopción en más casos de battery. |
| **Avanzar hasta puntos determinados** | Breakpoints software + continuar; opcionalmente condiciones sobre registros/custom/memoria. | **Parcial:** existe `winuae_breakpoint_conditional_wait` como helper asistido por software en el MCP. **Desarrollo:** el stub GDB de WinUAE no expone todavía condiciones nativas tipo expresión. |

### 2.4 Gráficos: patrones, mapas de bits y extracción coloreada

| Capacidad | Descripción | Estado |
|-----------|-------------|--------|
| **Captura de pantalla** | PNG del framebuffer emulado (`winuae_screenshot`). | **Hecho:** `winuae_screenshot` usa por defecto la ruta interna del emulador (`capture_mode=monitor/internal`) y solo cae a `host_window` si se pide explícitamente o en `auto` como fallback. La evidencia de batería debe preferir siempre captura interna (`internal_buffer`). |
| **Reconocimiento de bitmaps en RAM** | Dada una dirección base, **modo** (p. ej. lores 4/5/6 planos, interleaved, ancho, alto) y paleta desde `COLOR00–31` o parámetros, **decodificar** a imagen (p. ej. RGB PNG o buffer raw) para comparar con golden references o inspección visual. Opcional: buscar **patrones** (cabeceras IFF ILBM, secuencias típicas de bob, etc.). | **Parcial:** existe `winuae_bitmap_decode` en el MCP con salida PNG o RGBA, soporte 4/5/6+ planos, layouts interleaved/no interleaved y paleta desde args o custom regs. También existe `winuae_memory_pattern_search` para firmas exactas y patrones con stride. Falta validación viva con WinUAE para darla por cerrada. |
| **Uso en la batería de tests** | Cada prueba gráfica puede declarar en su `README.md` la **plantilla de extracción** (dirección desde `.map` o desde `BPLxPT`, profundidad, interleave). | **Parcial:** la herramienta existe; falta enlazar ejemplos y evidencias reales en `evidence/`. |

---

## 3. Convención de resolución y ventana

| Parámetro | Valor recomendado | Notas |
|-----------|-------------------|--------|
| Modo base | **Lores** | 320 px de ancho activo típico. |
| Alto útil | **200 líneas** (NTSC) o **256 líneas** (PAL) | Para comparar capturas entre pruebas, **fijar una sola variante por batería** (p. ej. PAL 320×256 con `DIWSTRT`/`DIWSTOP` estándar) y documentarla en el `README.md` de cada caso. |
| Evidencia | PNG de pantalla completa del emulador o recorte documentado | Nombre sugerido: `evidence/NN-slug-frameF.png`. |

Si una prueba exige **overscan** o **interlace**, lo declara en su `README.md` para no mezclar criterios con el resto.

---

## 4. Estructura de carpetas (objetivo)

Cuando se implemente cada caso en el repo:

```text
tests/amiga-battery/
  README.md                 # Índice, estados y documentos padre
  _template/
    README.md
    case.json
    src/main.c
    docs/technique.md
    evidence/
      README.md
  common/
    include/
    src/
  T01_lores_16c/
    README.md
    case.json
    src/main.c
    docs/technique.md
    evidence/
      README.md
```

Cada caso real debe aportar un programa C propio en `src/main.c`, su documentación técnica en `docs/technique.md` y evidencia en `evidence/`. La infraestructura común previa vive en `_template/` y `common/`. Hasta que existan subcarpetas por ID, los efectos pueden vivir en `app/effects/<nombre>/` y este documento actúa como **catálogo de IDs** y requisitos.

`docs/technique.md` de cada caso debe incluir al menos:

1. Registros y secuencia de inicialización.
2. Diferencia entre técnica horizontal/vertical cuando aplique (por ejemplo scroll).
3. Sección **References** con enlaces a fuentes objetivas y al índice AHRM.

La batería no es un producto paralelo al engine. Su función es:

1. demostrar que una capacidad concreta del hardware Amiga es viable;
2. documentar cómo se inicializa, verifica y depura;
3. servir de base para extraer esa capacidad a `engine/` cuando ya sea reusable.

Por tanto, el estado objetivo es que los casos usen cada vez más biblioteca común (`engine/` + `tests/amiga-battery/common/`) y cada vez menos código hardware duplicado.

---

## 5. Evidencias y trazas

| Artefacto | Cuándo | Cómo |
|-----------|--------|------|
| **Captura PNG** | Siempre | MCP `winuae_screenshot` con `filepath` fijo bajo `evidence/`; si el monitor devuelve error, usar `capture_mode=host_window` o `auto` con WinUAE visible |
| **Análisis visual LM Studio** | Obligatorio siempre que exista una captura PNG o bitmap decodificado | `scripts/lmstudio-vision.mjs` con modelo local `qwen2.5-vl-7b-instruct` (LM Studio). Guardar `.json` + `.md` junto a la imagen y referenciar la conclusión en el resumen del caso. El runner común debe fallar si hay `live-screen.png` o `decoded-playfield.png` sin sus artefactos `vision-*`. El veredicto visual debe incluir texto `VERDICT`, `EVIDENCE` y `BLOCKERS`. |
| **Política de restauración de sesión** | Obligatoria al cerrar cualquier test | El resumen de evidencia debe registrar `disconnectPolicy.stopEmulator`. Si el test abrió una sesión nueva y no había WinUAE previo, debe cerrar emulador para dejar estado equivalente; si había sesión previa, debe preservarla para encadenar tandas. |
| **Registros custom** | Duda gráfica / DMA | `winuae_custom_registers` + volcado en `evidence/regs-NN.txt` |
| **Instantánea completa** | Depuración profunda / regresión | `winuae_machine_snapshot` cuando haya emulador disponible; guardar JSON/texto junto con otras evidencias. |
| **Postmortem de crash/excepción** | Cuando el caso cae, entra en requester o devuelve una parada sospechosa | `winuae_postmortem_capture`: guardar JSON/Markdown con stop reason, CPU, stack, desensamblado alrededor de PC y snapshot auxiliar. Mantener además los artefactos planos si el runner ya los genera. |
| **Estado lógico mínimo del caso** | Cuando exista harness común o ruta `direct` diagnóstica | Capturar `g_battery_runtime_state` en `runtime-state.json` / `runtime-state.md` para saber si el binario alcanzó al menos su primera marca lógica antes del crash. Debe validarse firma (`BATTERY_RUNTIME_MAGIC`) para evitar lecturas de dirección errónea. |
| **Log de asserts/eventos en RAM** | Recomendado para todos los casos nuevos | Capturar `g_battery_evidence_log` en `evidence-log.json` / `evidence-log.md` con `run_id`, `last_stage`, `final_status`, `assert_failures` y anillo de eventos. Debe validarse firma (`BATTERY_EVIDENCE_MAGIC`) y permitir demostrar que el test alcanzó su checkpoint final aunque la imagen falle o no sea concluyente. |
| **Traza ligera de SO/custom** | Recomendado en casos DOS o de depuración de vídeo | Emitir eventos `battery_sys_trace_*` (por ejemplo `OpenLibrary`, `CloseLibrary`, `LoadView`, y muestras de `VPOSR/VHPOSR`) dentro del `evidence-log` para confirmar actividad del sistema y presentación de frames. |
| **Bitmap decodificado** | Comparar playfield / bob con golden image | Cuando exista herramienta §2.4 |
| **Perfil de frame** | CPU vs blitter vs copper | `winuae_profile` (WinUAE-DBG); analizar con `scripts/parse-amigaprofile.sh` — ver [debug-with-ai.md](debug-with-ai.md) |
| **Copper list** | Pruebas copper | `winuae_copper_disassemble` con dirección de `COP1LC` |

Infra común disponible:

- plantilla base de caso en `tests/amiga-battery/_template/`
- plantilla de propuesta metal directa en `tests/amiga-battery/_template-metal/`
- harness compartido en `tests/amiga-battery/common/`
- stub común de arranque directo en `tests/amiga-battery/common/metal/`
- runner común `scripts/run-battery-case.mjs` para build + ADF + circuito base de evidencia
- wrapper `scripts/capture-devfs-battery-evidence.mjs` para la ruta DOS alternativa `dh0/dh1 + debugging_trigger`
- análisis visual local con LM Studio en `scripts/lmstudio-vision.mjs` y configuración base en `.cursor/lmstudio.json`
- captura viva robusta en `scripts/capture-battery-evidence.mjs`, que debe intentar preservar screenshot + resumen + análisis visual aunque la ejecución del caso falle a mitad de camino
- postmortem MCP reutilizable (`winuae_postmortem_capture`) para autopsia de crashes, requesters y stops anómalos
- propuesta mínima de artefacto `metal_direct_stub` para futuros casos que necesiten carga fija y stack propio sin DOS
- marca de runtime `g_battery_runtime_state` en el harness común para que los casos puedan registrar una etapa lógica mínima antes de pasos críticos
- para casos `dos_hunk_exe`, la ruta estándar de validación debe ser `ADF/OS-loader`; la ruta `direct` se reserva para diagnóstico, postmortem temprano y preparación de futuros casos `metal`
- para todos los casos gráficos nuevos, la escena debe mezclar patrones procedurales y geometría reconocible para que el modelo de visión describa con precisión layout, colores y posiciones
- para capturas y secuencias de evidencia, el caso debe usar un punto representativo del dataset (no el primer tramo por defecto si es pobre): elegir el viewport/estado donde la técnica se vea de forma clara y completa, y documentar ese criterio en `case.json` y `docs/technique.md`
- cuando ni ADF clásico ni carga directa sean suficientemente robustos, el siguiente escalón recomendado es un `dev harness disk` o launcher residente propio, documentado en `doc/amiga-dev-harness-loader.md`
- como escalón intermedio antes de un `HDF` real, ya existe la ruta `devfs` basada en `.vscode/mcp-amiga-battery-devfs.uae` y `out/devfs/a.exe`, útil para iterar binarios DOS sin regenerar floppy en cada pasada
- ese dev harness debería preferir `LoadSeg` / `RunCommand` sobre `PROGDIR:payload.exe` o ruta equivalente antes de intentar puentes más agresivos; la carga directa debe seguir siendo una vía diagnóstica o metal, no la ruta principal de un `dos_hunk_exe`

---

## 6. Flujo Workbench ↔ programa “metal” (integración)

Escenario deseado (documentar en la prueba de integración cuando exista):

1. Arranque con Workbench/DOS disponible.
2. Lanzar ejecutable que hace `TakeSystem` (o equivalente) y entra en modo pantalla completa.
3. Salida ordenada (`FreeSystem`, vuelta a WB/CLI) **o** reset documentado si el caso es solo demoscene.

Este flujo es **más complejo** que un único efecto desde menú copper; conviene un ID dedicado (ver §8.6).

---

## 7. Entrada: teclado, ratón, joystick

Herramientas MCP (`winuae_input_key`, `winuae_input_mouse`, `winuae_input_joy`): en cada `README.md` de prueba que requiera interacción, indicar **códigos** o **secuencias** usados para la captura final.

Cuando el caso viva dentro de `Cursor-Amiga-C`, es preferible usar la capa programática del engine (`g_automation_input`) a través de los helpers MCP, porque permite fijar coordenadas Amiga 320×256 y estados de mouse/joy/key de forma determinista dentro del programa aunque el input host de WinUAE sea más frágil.

---

## 8. Catálogo de pruebas (técnicas)

Cada fila: **ID**, **nombre**, **idea**, **hardware principal**, **máquina** (A500 = OCS/ECS típico; AGA = A1200/CD32), **estado**.

### 8.1 Modos de vídeo, bitplanes y color

| ID | Nombre | Qué validar | Hardware / registros | Máquina | Estado |
|----|--------|-------------|----------------------|---------|--------|
| **T00** | Lores 2 planos, 4 colores (carta) | Conmutación base de modo + carta de ajuste reconocible por visión | `BPLCON0`, punteros `BPLxPT`, `DIW`/`DDF` | A500 | (plan) |
| **T01** | Lores 4 planos, 16 colores | Playfield estable, paleta `COLOR00–15` | `BPLCON0`, punteros `BPLxPT`, `DIW`/`DDF` | A500 | (plan) |
| **T02** | Lores 5 planos, 32 colores | Fetch y modulo correctos | `BPLCON0`, `BPL1MOD`/`BPL2MOD` | A500 | (plan) |
| **T03** | **EHB** (6 planos, extra halfbrite) | Colores 32–63 como mitad de brillo de 0–31; patrón visible | `BPLCON0`, `BPLCON2` (EHB), 6 punteros | ECS/AGA típ. | (plan) |
| **T04** | **HAM6** | 4096 colores con restricciones de cambio por píxel; artefactos controlados | `BPLCON0` modo HAM | A500 | (plan) |
| **T05** | Hires 640×… (opcional) | 4 planos hires o modo documentado | `BPLCON0` HIRES | A500 | (plan) |
| **T06** | Dual playfield **3+3** estático | Fondo y frente con colores distintos; prioridad | `BPLCON0` DBLPF, `BPLCON2` | A500 | (plan) |
| **T07** | Dual playfield **3+3** scroll **H/V independiente** | `BPLCON1` por capo o copper por frame; sin corrupción | Copper + `BPLCON1`, módulos | A500 | (plan) |
| **T08** | Modulo tricks (skew / ventana) | Patrón que demuestra `BPL1MOD`/`BPL2MOD` ≠ ancho nominal | Módulos, `DDF` | A500 | (plan) — enlazar [modulo-tricks.md](techniques/modulo-tricks.md) |

### 8.2 Copper

| ID | Nombre | Qué validar | Hardware | Máquina | Estado |
|----|--------|-------------|----------|---------|--------|
| **C01** | **Degradado vertical** (gradiente) | `COLOR00` (u otros) distinto por línea; transición suave | Lista copper `WAIT`+`MOVE` | A500 | (plan) |
| **C02** | Degradado multibanda | Varios registros `COLORxx` por zona vertical | Copper | A500 | (plan) |
| **C03** | **Copper bars** animadas | Barras horizontales en movimiento (raster) | Copper + contador frame | A500 | (plan) |
| **C04** | Copper + **espera a blitter** (BFD) | `WAIT` con BFD según necesidad de fin de blit | `BLTCON`, copper waits | A500 | (plan) |
| **C05** | Copper “chunky” por bloques | Bloques de color simulando baja resolución horizontal | Copper denso | A500 | (plan) — [copper-chunky.md](techniques/copper-chunky.md) |

### 8.3 Blitter (líneas, áreas, máscaras)

| ID | Nombre | Qué validar | Hardware | Máquina | Estado |
|----|--------|-------------|----------|---------|--------|
| **B01** | Relleno **rectángulo** (clear / solid) | Área uniforme; minterm fill | `BLTCON0/1`, `BLTSIZE`, canal D | A500 | (plan) |
| **B02** | **Copia** rectángulo (A→D) | Imagen fuente visible en destino | Canales A/B/D según modo | A500 | (plan) |
| **B03** | **Línea** hardware (vector) | Líneas en múltiples orientaciones y colores (patrón rayado) | Modo línea blitter, `BLTCON1` | A500 | (plan) |
| **B04** | **Polígono relleno** (p. ej. triángulo) | Relleno por scanlines con blitter o trapezoides | Blitter fill + CPU sort edges | A500 | (plan) |
| **B05** | **Minterm / máscara** (cookie-cut BOB) | Sprites “software” con máscara y fondo | A, B, C, D, minterms | A500 | (plan) |
| **B06** | Copy con **desplazamiento** fino | Uso de shifts del blitter para scroll parcial | `BLTCON0` shifts | A500 | (plan) |
| **B07** | Blitter **asistido por CPU** (bandas) | CPU y blitter en paralelo en regiones distintas | `DMACON`, `BBUSY` | A1200 preferente | (plan) — [cpu-blit-assist.md](techniques/cpu-blit-assist.md) |

### 8.4 Sprites y “objetos”

| ID | Nombre | Qué validar | Hardware | Máquina | Estado |
|----|--------|-------------|----------|---------|--------|
| **S01** | **Sprites hardware** simples | Uno o más sprites DMA, posición `SPRxPOS/CTL` | Sprite pointers | A500 | (plan) |
| **S02** | Sprites **attach** (16 colores) | Par attach + paleta asociada | Sprites 0+1, etc. | A500 | (plan) |
| **S03** | Prioridad sprite / playfield | Delante o detrás según `BPLCON2` | Prioridades | A500 | (plan) |
| **S04** | **BOBs** (blitter objects) | Objetos movidos con restauración de fondo o dual playfield | Blitter + [dual-playfield-fastbobs.md](techniques/dual-playfield-fastbobs.md) | A500 | (plan) |
| **S05** | “Soft sprites” **CPU** sobre buffer | Dibujo directo en bitplanes (EOR, máscaras CPU) sin DMA sprite | CPU + chip RAM | A500 | (plan) |

### 8.5 Audio (Paula)

| ID | Nombre | Qué validar | Hardware | Máquina | Estado |
|----|--------|-------------|----------|---------|--------|
| **A01** | Tono / onda en un canal | Periodo, volumen, longitud DMA | `AUDxLCH/LCL`, `AUDxLEN`, `AUDxPER`, `AUDxVOL` | A500 | (plan) |
| **A02** | Cuatro canales simultáneos | Mezcla perceptible sin glitches | Paula DMA | A500 | (plan) |
| **A03** | Mezcla software ligera para juego | Buffer mezclado + reproducción (si aplica) | CPU + Paula | A500+ | (plan) — [audio-mixing.md](techniques/audio-mixing.md) |

### 8.6 Sistema, colisiones, misc.

| ID | Nombre | Qué validar | Hardware | Máquina | Estado |
|----|--------|-------------|----------|---------|--------|
| **M01** | **Colisiones** sprite/datos | Lectura `CLXDAT` / configuración `CLXCON` | Collision | A500 | (plan) |
| **M02** | **VBL / interrupciones** | Contador estable por frame; ack correcto | `INTENA`, `INTREQ`, `VERTB` | A500 | (plan) |
| **M03** | Integración **Workbench → metal → WB** | Transición documentada | Exec/Intuition + `TakeSystem`/`FreeSystem` | A500 | (plan) |

### 8.7 Subconjunto **AGA** (A1200 / CD32)

Pruebas adicionales cuando `TARGET_MACHINE=a1200|cd32` y `.uae` AGA:

| ID | Nombre | Qué validar | Estado |
|----|--------|-------------|--------|
| **AG01** | Más planos / modos fetch AGA | `FMODE`, `BPLCON3`/`BPLCON4` según modo | (plan) |
| **AG02** | Paleta extendida 24-bit | Selección de bancos de color | (plan) |
| **AG03** | Blitter ancho AGA | BLTSIZV/H y restricciones | (plan) |

---

## 9. Priorización sugerida (implementación de pruebas)

Orden razonable para ir cerrando dependencias:

1. **T01 → T02 → C01 → B01 → B02** (base display + copper + blitter).
2. **B03** (líneas) y **B04** (polígonos).
3. **T06 → T07** (dual playfield).
4. **T03** (EHB) y **T04** (HAM).
5. **S01 → S04** (sprites HW y BOBs).
6. **S05**, **B05**, **B07** (híbridos).
7. **M03** (integración OS).
8. Subconjunto **AG*** cuando el perfil AGA esté estable.

En paralelo, conviene avanzar la **hoja de ruta MCP** (§10) para que la extracción de bitmaps y la instantánea única reduzcan trabajo manual en cada ID.

---

## 10. Hoja de ruta de desarrollo (MCP / WinUAE / scripts)

**Seguimiento vivo (estados PENDIENTE / PARCIAL / HECHO, fases A–E, tabla de todos los IDs T/C/B/S/A/M/AG):** [amiga-implementation-roadmap.md](amiga-implementation-roadmap.md). Este apartado resume las entregas; el roadmap de implementación es la referencia para saber **qué falta** en cada momento.

| Prioridad | Entrega | Descripción |
|-----------|---------|-------------|
| P0 | **ADF en caliente** | **Parcial:** existe `scripts/a-mcp-01-matrix.mjs` en `mcp-winuae-emu` para probar `connect` vs `connect_existing` en DF0-DF3 y generar evidencia JSON/Markdown. La validación viva confirma `dfN insert/eject` en DF0-DF3 sobre una sesión externa visible; la parte aún parcial es la reutilización entre turnos tras `disconnect(false)` en sesiones lanzadas por el propio MCP. |
| P0 | **`winuae_machine_snapshot`** | **Hecho:** respuesta estructurada validada en vivo con CPU + `custom_registers` + ventanas opcionales chip/fast RAM, limitadas a 16 KiB cada una y con manejo de error por región cuando una ventana falla. |
| P1 | **`winuae_bitmap_decode`** | **Parcial:** implementado con parámetros de dirección, ancho, alto, profundidad, interleaved, `row_bytes`, paleta desde custom regs o argumentos, y salida PNG o RGBA; falta validación viva con emulador. |
| P1 | **Búsqueda de patrones** | **Parcial:** implementado como `winuae_memory_pattern_search` con búsqueda por firma hex exacta, rango configurable y scoring opcional por stride/repetición; falta validación viva con emulador. |
| P2 | **Reloc hunks** | **Parcial:** `winuae_load` ya aplica relocs de un subconjunto práctico de AmigaHunk (`HEADER/CODE/DATA/BSS/RELOC32/END`); falta validación viva con emulador y con binario real del toolchain. |
| P2 | **Breakpoints condicionales** | **Parcial:** expuestos como helper asistido por software (`winuae_breakpoint_conditional_wait`) sobre breakpoints normales. Pendiente validación viva y, si el fork lo soporta en el futuro, soporte nativo del stub. |

Implementación preferente en el repo **mcp-winuae-emu**; si hace falta soporte en **WinUAE-DBG** (nuevos `qRcmd`), documentar el contrato en `HANDOVER.md` o equivalente del fork.

---

## 11. Referencias

- **Roadmap de implementación y estado global:** [amiga-implementation-roadmap.md](amiga-implementation-roadmap.md)
- Flujo MCP y compilación: [mcp-live-coding-workflow.md](mcp-live-coding-workflow.md)
- Fichas por técnica: [techniques/README.md](techniques/README.md)
- Matriz chipset: [amiga-chipset-matrix.md](amiga-chipset-matrix.md)
- AHRM índice: [amiga-hardware-manual-index.md](amiga-hardware-manual-index.md)
- Integración engine/demoscene: [demoscene-effects-integration.md](demoscene-effects-integration.md)
