# Guía: iniciar un proyecto con este engine

Referencia práctica para **arrancar un juego o demo** reutilizando el motor de Cursor-Amiga-C: qué copiar, qué patrones tomar del repo actual y cómo compilar y verificar.

## Parent links

- [Índice de documentación](engine-docs-index.md)
- [Arquitectura](engine-architecture.md)
- [Build CLI](build.md)
- [Batería de tests](../tests/amiga-battery/README.md)
- [Matriz engine ↔ batería](engine-test-battery-matrix.md)
- [Roadmap tests unificados del engine](engine-unified-test-roadmap.md)

---

## 1. Dos formas de trabajar (elige una)

### A) Proyecto **dentro** de este repositorio (recomendado al principio)

- Añades un efecto o juego como **`app/effects/<tu_juego>/`** con el contrato `create` / `loop` / `destroy` (igual que [demo_scroll_bobs/effect.h](../app/effects/demo_scroll_bobs/effect.h)).
- Registras la entrada en el menú ([`app/menu.c`](../app/menu.c)) o en el flujo Intuition ([`app/main.c`](../app/main.c)).
- **Ventajas**: mismo `Makefile`, `scripts/verify-build.sh`, batería, MCP y configs WinUAE ya probados; promoción natural de código a `engine/` cuando se repita.

### B) Repositorio **nuevo** (producto aparte)

- Copias como mínimo **`engine/`** (include + src) y **`support/`** (runtime GCC/ABI y ensamblador que espera el linker).
- Adaptas un **Makefile** tomando como modelo el de la raíz: `VPATH`, `m68k-amiga-elf-gcc`, `elf2hunk`, lista de `.c` de app + `wildcard engine/src/*.c`.
- Mantén la **misma convención** de salida `out/<nombre>.exe` y, si usas ADF, el flujo de [build.md](build.md) / `make adf`.
- **Licencia**: el proyecto hereda **GPL-3.0** del ecosistema vscode-amiga-debug; un juego comercial suele requerir plan legal aparte o sustituir el runtime según asesoramiento.

---

## 2. Contrato de aplicación que ya funciona aquí

1. **`engine_init()`** — abre librerías base (`graphics`, `dos` si existe).
2. **UI opcional** (Intuition) o **menú copper** — ver [`app/main.c`](../app/main.c).
3. **`TakeSystem()`** — antes del bucle que toca hardware directo (bitplanes, copper propios).
4. Por frame: **`engine_wait_vbl()`**, input (`engine_mouse_update`, `engine_key_*`, opcional `engine_input_edges_sync` con [engine_suite.h](../engine/include/engine_suite.h)), lógica y gráficos.
5. **`FreeSystem()`** + **`effect_destroy()`** (o equivalente) al salir.
6. **`engine_shutdown()`** al terminar el programa.

Patrón de efecto documentado en el propio header del demo:

```13:20:app/effects/demo_scroll_bobs/effect.h
/* Devuelve 0 si OK, distinto de 0 si error. */
int effect_create(void);

/* Ejecuta un frame. El caller repite hasta que ambos botones (volver al menú). */
void effect_loop(void);

/* Libera todo lo asignado en create. */
void effect_destroy(void);
```

---

## 3. Cabeceras del motor (por capas)

| Necesidad | Include |
|-----------|---------|
| API principal (sistema, display, blitter, input, memoria) | `engine.h` |
| Gameplay / math / trazas opcionales | `engine_suite.h` |
| Copper doble buffer, sprites, joy, DOS, audio stub, etc. | `engine_extensions.h` |

No incluyas **`engine_internal.h`** desde tu juego: reservado al código bajo `engine/src/`.

Detalle de subsistemas: [engine-subsystems.md](engine-subsystems.md).

---

## 4. Ideas del repo que merece la pena replicar

| Idea | Dónde está | Para qué sirve |
|------|------------|----------------|
| **Separación engine / app / batería** | [README](../README.md), [engine-architecture.md](engine-architecture.md) | Escalar sin mezclar menú con HAL. |
| **Efecto = create + loop + destroy** | `app/effects/*` | Carga/descarga limpia y menú estable. |
| **Estados push/pop** | [`app/state.c`](../app/state.c) | Pausa, menú encima del juego, transiciones. |
| **Automatización MCP** | `g_automation_input`, [`engine_automation_input.h`](../engine/include/engine_automation_input.h) | Pruebas sin depender solo del ratón host. |
| **`engine_has_dos()`** | [`engine.h`](../engine/include/engine.h) | Misma build para ADF sin Workbench y para CLI. |
| **Batería aislada** | `tests/amiga-battery/`, `BATTERY_CASE=` en [Makefile](../Makefile) | Validar una técnica antes de ensuciar el juego. |
| **Matriz de cobertura** | [engine-test-battery-matrix.md](engine-test-battery-matrix.md) | Saber qué API tiene ya un test con evidencia. |
| **Constantes copper sin acoplar** | `ENGINE_CUSTOM_*` en [engine_types.h](../engine/include/engine_types.h) | Listas reubicables y legibles. |
| **Depuración WinUAE + MCP** | [mcp-live-coding-workflow.md](mcp-live-coding-workflow.md), [debug-with-ai.md](debug-with-ai.md) | Regresiones visuales y memoria. |

---

## 5. Build mínimo

```bash
bash scripts/verify-build.sh              # app por defecto → out/a.exe
bash scripts/verify-build.sh --clean LTO=0 # depuración estable
bash scripts/build.sh --debug --program=out/mijuego --battery-case=Mi_caso   # binario suelto + caso batería
```

- Toolchain: variable **`AMIGA_BIN_PATH`** o detección automática (extensión amiga-debug); detalle en [build.md](build.md).
- **ADF**: `make adf` o scripts documentados en [winuae-y-adf.md](winuae-y-adf.md) si aplica.

---

## 6. Nuevo efecto o módulo en este repo (checklist)

1. Crear `app/effects/<slug>/effect.h` y `effect.c` con `create` / `loop` / `destroy`.
2. Incluir `engine.h` (y `engine_suite.h` / `engine_extensions.h` si hace falta).
3. Reservar memoria CHIP para copper/bitplanes con **`engine_alloc(..., MEMF_CHIP)`**.
4. Registrar opción en **`menu.c`** (o flujo en **`main.c`**).
5. `bash scripts/verify-build.sh` y prueba en WinUAE (Kick 1.3 para A500 de referencia).
6. Si la técnica es genérica, moverla a `engine/` y añadir fila en [engine-test-battery-matrix.md](engine-test-battery-matrix.md).

---

## 7. Repo nuevo (checklist resumido)

1. Copiar `engine/`, `support/`, plantilla de **Makefile** y `scripts/build.sh` (o equivalente).
2. Tu `main.c`: `engine_init` → `TakeSystem` → bucle → `FreeSystem` → `engine_shutdown`.
3. Ajustar **`program=`** en make para el nombre del ejecutable.
4. Configurar ROM **Kickstart 1.3** y ADF en WinUAE (ver `.vscode/mcp-amiga-debug.uae` como referencia).
5. Documentar en tu README la ruta del toolchain y el comando de build reproducible.

---

## 8. Qué deja fuera esta guía (siguiente documentación)

- Tutorial paso a paso “del primer píxel al BOB” línea a línea (sigue siendo candidato a doc largo; aquí solo el **esqueleto** y enlaces).
- **Doxygen** u hoja única de cada símbolo: conviene generarlo o mantener `engine.h` como contrato principal.
- **Pipeline de assets** (paletas, ILBM): usar `gfx/` y [techniques/README.md](techniques/README.md) cuando exista herramienta estable.

---

## 9. Lectura recomendada después de esta guía

1. [engine-architecture.md](engine-architecture.md) — visión completa del API.
2. [amiga-test-battery-spec.md](amiga-test-battery-spec.md) — cómo demostrar técnicas con evidencia.
3. [m68k-stack-and-calling-notes.md](m68k-stack-and-calling-notes.md) — cuando toques ensamblador o interrupciones.
