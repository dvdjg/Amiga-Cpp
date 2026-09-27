# Roadmap: corrección de tipos y eliminación de casts forzados

Plan de refactor del engine para alinear los tipos con el dato que representan y quitar los `cast`
que hoy los compensan. Referencia de reglas: `docs/engine/architecture/CODING_STYLE.md`
§"Seguridad de tipos sobre punteros crudos" (líneas 113-241), `docs/engine/architecture/PUBLIC_API.md`
§"sin punteros" y `docs/ai-dev-environment/AGENTS.md` §1.

## 1. Directrices aplicables (la fuente, no la inventamos)

- **Evitar punteros siempre que se pueda**; "puntero + count" está prohibido: buffers contiguos →
  `eng::Span<T>`; texto → `eng::Str`/`encoding::Str`. `CODING_STYLE.md:126,134,150`.
- **Puntero crudo a OBJETO: prohibido; solo memoria cruda** (`CODING_STYLE.md:141`). El `T*` legítimo
  es el que **es** el mecanismo (memoria cruda, `T*` autopropietario de un recurso con `free`), y
  está listado en `CODING_STYLE.md:161-166`.
- **Nada de `void*` + puntero a función en dominio**: para "algo variable" (escalar, estrategia) se
  usa un **tipo/`concept` de plantilla**, no `void*` ni puntero a función (`CODING_STYLE.md:140,152`).
- **El cast es un indicio de tipo mal elegido** (`CODING_STYLE.md:232`): ante un cast, **revisar el
  tipo de ORIGEN**; si el origen no **garantiza** lo que el destino afirma, cambiar el tipo o la
  **procedencia** del dato, nunca forzar el cast.
- **Todo cast debe demostrar que hace falta** (`CODING_STYLE.md:233`): quitar el cast y recompilar con
  `-Wall -Wextra` (narrowing solo en *list-init* `T{.}`). Si no hay aviso y el destino admite el
  valor, el cast es **ruido** y se elimina.
- **Buffers con dominio, escalares sin envolver** (`CODING_STYLE.md:179`); los internos usan tipos de
  dominio (`PlaneTag`, `CopperTag`, `Address<K>`, `q0/q12`), no `u8*`/`short` crudos sueltos.

## 2. Diagnóstico (medido, `2026-09`)

```
reinterpret_cast 241   static_cast 4445   const_cast 12   void* 156   mem* 20
```

Top por fichero (los que más concentran el problema):

| Patrón | Ficheros (nº) |
|---|---|
| `reinterpret_cast` | `field/playfield_base.hpp` (22), `debug/peripheral.hpp` (16), `amiga/amiga.cpp` (12), `amiga_blitter.cpp` (12), `amiga/input_poll.hpp` (10), `core/util/dynamic_hash_map.hpp` (9), **`amiga/object3d.hpp` (9)**, `amiga/blob.hpp` (7), `amiga_internal.hpp` (7) |
| `void*` | `amiga/backend.hpp` (19), `amiga_internal.hpp` (13), **`audio/music_player.hpp` (11)**, **`audio/sfx_mixer.hpp` (9)**, `core/util/heap_alloc.hpp` (6), **`ui/widgets.hpp` (6)** |
| `static_cast` | `amiga_blitter.cpp` (173), `field/scroll_engine.hpp` (155), `core/math/minifloat.hpp` (105), `field/xlimited_playfield.hpp` (86), `field/playfield_base.hpp` (76) |

**Clasificación (importa tanto como el número):**

- **Legítimo — NO tocar**: acceso a registros custom y frontera de hardware (`amiga.cpp`,
  `amiga_blitter.cpp`, `amiga_internal.hpp`, `blob.hpp`, `peripheral.hpp`, `input_poll.hpp`);
  almacenamiento interno de contenedores (`dynamic_hash_map`, `small_vector`); la frontera ABI del
  asm de audio (`__asm("a0")` + `void*`); la **vista de formato de fichero** (el `.obj` de `object3d`,
  que es un layout de bytes fijo). Aquí el cast es la frontera declarada (§161, §148).
- **A corregir — tipo mal elegido (fase por fase abajo)**: hooks `void* + fn` en headers de
  dominio; `reinterpret_cast` en acceso de dominio (no de formato); `static_cast` **ruido** (§233);
  offsets/índices en `s16/s32` crudos donde hay tipo de dominio.

## 3. Fases (ordenadas por relación valor/riesgo)

### Fase 0 — Red y medida (barato, primero)
- Añadir a `tools/check/` un gate de patrones prohibidos (`reinterpret_cast`/`const_cast`/`void*` +
  puntero a función) con **lista blanca por fichero** (backend, contenedores, ABI). Falla si aparece
  fuera de la lista → evita reincidir y hace visible el progreso.
- Script `tools/analyze/cast-audit.mjs`: cuenta por fichero y, para cada `static_cast`, marca los
  candidatos a **ruido** (cast que no silencia narrowing ni `void*`). Es el "medidor" de las fases.

### Fase 1 — `object3d.hpp` (aclaración: **ya usa `Fixed`**)
- **Hallazgo:** `object3d` **ya está tipado con `Fixed`**: `Point3D` son `q0` (LONGITUD), `Face.normal` y `Point3R` son `q12` (RATIO), `Angle3` son `Turns`, y la matriz es `math3d::Affine3<>` (Fixed). El propio header lo documenta ("Por qué los structs siguen en `s16`"): el LAYOUT del `objdat` no puede cambiar porque lo lee `flatshade_asm.s` y lo indexa el original por offset de byte; la capa de cálculo **sí** es tipada y genérica.
- Sus `reinterpret_cast<X*>(base + off)` son la **frontera declarada** del `objdat` empaquetado (byte→struct de un formato fijo), justificada en el código y **probada por HOST-014** (`node3d(i) == objdat + i - 2`). **No** son ruido: sin el cast no compila (§233 "void*→tipo" = legítimo). → se documentan y entran en la lista blanca del gate.
- Acción real de esta fase: **(a)** aplicar el test §233 a los casts *de ruido* — hecho aquí (`static_cast<s32>(i)` y `static_cast<s16>(i ± k)` eliminados; HOST-014 sigue verde); **(b)** opcional: migrar las **lecturas** a los cursores de `eng/core/util/binary.hpp` (`ByteReader`), dejando los accesores mutables (`point`/`vertex`) como única frontera.

### Fase 2 — Hooks `void*` + puntero a función → `concept`/plantilla
- `field/playfield_base.hpp` (`Fn = bool(*)(void* ctx, u8* plane_base, ...)`): el "contexto" y los
  punteros de plano de los hooks pasan a **parámetros de plantilla** (`class Target`, `concept
  PlaneWriter`) o a un `DrawTarget` tipado; el `void*`/`u8*` desaparece de la firma.
- `ui/widgets.hpp` (6 `void*`): modelo de callbacks tipado.
- Salida: prohibido `void*` + puntero a función en `engine/include` fuera del backend/ABI.

### Fase 3 — `static_cast` ruido (§233) en cabeceras de dominio
- Aplicar el test de §233 en `scroll_engine.hpp`, `xlimited_playfield.hpp`, `field_controller.hpp`,
  `behavior.hpp`, `goap.hpp`, `scheduler.hpp`: **quitar** los `static_cast` que solo repiten una
  conversión implícita (promociones `int`→`s16`, etc.). Dejar solo los que evitan *narrowing* en
  list-init `T{.}` o documentan una frontera.
- Los `static_cast` de `minifloat.hpp`/`fixed.hpp`/`fixed_math.hpp`/`minifloat_math.hpp` son
  **conversiones numéricas** del formato: revisar uno a uno, pero **no** es "tipo mal elegido" salvo
  que oculten pérdida de rango.

### Fase 4 — Audio: `void*` de API → vistas tipadas
- `audio/music_player.hpp` / `sfx_mixer.hpp`: la **API pública** pasa `Span<const u8>`/`Address<..>`
  (módulo, samples, buffer); el `void*` queda **solo** en la llamada al asm (`__asm("a0") a0...`).
- Salida: la frontera asm documentada y encapsulada; la API sin `void*`.

### Fase 5 — Cierre
- Gate (Fase 0) en verde con la lista blanca ya reducida; `cast-audit.mjs` sin candidatos de ruido.
- Documentar en `CODING_STYLE.md` los patrones nuevos (vistas de formato, hooks tipados) y bajar de
  la lista blanca lo que se haya limpiado.

## 4. Orden recomendado y valor
1. **Fase 0** (medir + blindar) — habilita todo lo demás y evita regresiones.
2. **Fase 1 (`object3d.hpp`)** — el ejemplo que pediste; alto valor didáctico (patrón de vista).
3. **Fase 2 (hooks `void*`)** — la directriz más citada; toca cabeceras muy reutilizadas.
4. **Fase 3 (ruido)** — mucha superficie, bajo riesgo, mejora legibilidad y señala tipos mal elegidos.
5. **Fase 4 (audio)** — encapsula la ABI.

Regla de parada por fichero: si un `reinterpret_cast` se demuestra **frontera real** (registro custom,
layout de fichero, ABI), se **documenta y se añade a la lista blanca** — no se fuerza una "abstracción"
que solo esconde el puntero.

## 6. Estado y método §233 (herramienta)

El medidor es `tools/analyze/cast-audit.mjs`:

- `--check` → gate de **no-reincidencia** contra `tools/check/casts-baseline.txt`, que ahora guarda
  **dos columnas** por fichero: `<reinterpret+const>  <static_cast>`. Ambas **solo pueden bajar**.
- `--list <fichero>` → imprime cada `static_cast<...>(...)` con su línea: el candidato a la pasada §233.
- `--update-baseline` → regenera (solo justificando cada subida).

**Método §233 (quitar-y-compilar).** Para cada `static_cast<T>(x)`: quitarlo, recompilar con las
mismas banderas; si **no** hay aviso y el valor entra, era **ruido** → fuera. Si hay error o *warning*
(narrowing en *list-init* `T{.}`, `void*→tipo`, truncado intencionado), es frontera → se queda y se
comenta. **Antes de quitarlo, mirar si el tipo debería ser el mismo**: muchos casts desaparecen
**unificando el tipo** (p. ej. `static_cast<u32>(span.size())` porque `Span::size()` es `usize`).

**Hecho (2026-09):**

- **F0 (red)**: `cast-audit.mjs` con gate de dos columnas + `--list`. Baseline inicial: 249 duros.
- **F1 parcial (`object3d.hpp`)**: aclarado que ya usa `Fixed`; aplicado el método §233 —
  fuera los `static_cast<s16>` de ruido en los accesores y en la cámara
  (`dot_fixed_row` ya devuelve `q0`); **unificados los tipos de tamaño a `usize`** (`MeshBlob::size`,
  `objdata_size`, `mesh_validate`) → cayeron los `static_cast<u32>(size())`.
- **Fase A del rediseño del mesh**: tipos fuertes (`ObjOffset`/`VertexRef`/`EdgeRef`/`FaceRef`),
  `MeshStatus` y accesores que devuelven `eng::Ref<T>`. Diseño en
  [`OBJECT3D_MESH_VIEW.md`](../../engine/architecture/OBJECT3D_MESH_VIEW.md).

**Pendiente:** F2 (`playfield_base` hooks `void*`→`concept`), F3 (§233 en `scroll_engine`,
`xlimited_playfield`, `amiga_blitter`, `minifloat`), F4 (audio), y las fases B/C del mesh
(wrappers libres fuera + `MeshAbi` + `Object3D` sin `u8*` público).
