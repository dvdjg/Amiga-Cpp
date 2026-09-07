# Roadmap de portación de demoscene-repo-orig al engine C++23

Estrategia para incorporar el **conocimiento de las librerías de
`demoscene-repo-orig`** al engine de Amiga-Cpp, no importando las demos como
tales sino **portando sus librerías (`lib*`) a C++23 dentro de `engine/`**,
validadas efecto a efecto mediante las demos del propio repositorio como banco
de pruebas.

Origen:

```text
C:\Users\dvdjg\Documents\programa\AI\Amiga\demoscene-repo-orig
```

Referencia intermedia (conversión a dialecto GCC, **no fiable**):

```text
C:\Users\dvdjg\Documents\programa\AI\Amiga\demoscene-repo
```

> **Qué es cada cosa.** `demoscene-repo-orig` es un proyecto Amiga en un
> dialecto C no portátil: usa GNU statement-expressions (`({ ... })`),
> `typeof`, libc propia (`libc/`), tipos propios (`types.h`), fixed-point
> (`fx.h`, `SIN/COS`), y librerías `lib2d/lib3d/libblit/libgfx/libgui/libmisc`
> más reproductores de audio en asm (`libp61/libpt/libahx/libctr`). La
> conversión `demoscene-repo` (hecha por otra IA) *puede no compilar*; sirve
> solo de pista de cómo abordar la adaptación, nunca como fuente de verdad.

## 1. Principios

1. **Se portan librerías, no efectos.** Los ~67 efectos de `effects/` son el
   banco de pruebas que demuestra que una librería portada funciona, no el
   objetivo. Una librería solo sube a `engine/` cuando al menos un efecto que
   la usa corre y valida con el pipeline de Amiga-Cpp (`build -> run ->
   analyze`).
2. **Reutilizar antes que portar.** El engine ya tiene `eng::core`
   (`sinetable.hpp`, `fast_div.hpp`, `span.hpp`, `types.hpp`, `ct_array.hpp`),
   `eng::graphics::copper`, `frame_plan.hpp`, `sprite_manager.hpp` y drivers de
   tile/scroll. Antes de portar una función, buscar si el engine ya resuelve el
   mismo problema y, si es así, **mapear** (no duplicar).
3. **Portación fiel al comportamiento, limpia en estilo.** Se traduce la lógica
   al C++23 del engine: sin excepciones, sin RTTI, sin asignación dinámica en
   gameplay, APIs paramétricas y agnósticas del backend (reglas de
   `AGENTS.md`/`CODING_STYLE.md`). Los comentarios didácticos en español, con
   esquema ASCII cuando aclare geometrías.
4. **Regla de no-salto.** No pasar de "código C en origen" a "API final del
   engine" directamente. Por librería: inventariar → portar mínimo → validar
   con efecto → documentar costes/ownership → promover.
5. **El asm se conserva, no se traduce.** Las rutas en `.asm/.S` (c2p, p61,
   pt, ahx, cinter, inflate, checksum, sintab…) están probadas y son específicas
   de 68000; se mantienen como `support/` de ensamblador y se enlazan desde el
   wrapper C++ correspondiente.

## 2. Destino en el repo

Las librerías portadas viven en `engine/include/eng/` como namespaces, según
`docs/STRUCTURE.md` §3:

| Librería origen | Namespace C++ destino | Contenido |
|---|---|---|
| `libmisc` | `eng::core` (ampliar) o `eng::misc` | fx (fixed-point), sintab, sort, crc32, console, checksum |
| `libc` (string/stdlib/stdio) | `eng::core::str` / helpers | `strlen/strcpy/memset/memcpy/strcmp/qsort/random/kvprintf` |
| `libgfx` | `eng::graphics` (bitmaps, copper, sprites, paletas, c2p) | `NewBitmap/CopList/CopSetup*/Sprite*/Pixmap*` |
| `libblit` | `eng::graphics::blit` (nuevo) | `BlitterCopy/BlitterFillArea/BlitterLine/Bitmap*` |
| `lib2d` | `eng::math2d` (nuevo) | `ClipLine2D/ClipPolygon2D/Transform2D/Rotate2D/...` |
| `lib3d` | `eng::math3d` (nuevo) | `Object3D/Transform3D/FaceVisibility/SortFaces/...` |
| `libgui` | `eng::ui` (futuro) | widgets, layout, navegación |
| `libp61/libpt/libahx/libctr` | `eng::audio` (futuro) | wrappers C++ sobre los reproductores asm |

Regla de no duplicar: si el engine ya tiene la funcionalidad (p. ej.
`sinetable` para `SIN/COS`), se usa la del engine y se **elimina** la versión
portada duplicada; no se mantienen dos implementaciones del mismo concepto.

## 3. Oleadas por dependencia

Orden pensado para reducir riesgo: primero infraestructura sin hardware, luego
gráficos+blitter (necesitan la base), después matemáticas 2D/3D (independientes
pero útiles para cubrir más efectos), y al final audio/UI (dependen de asm y de
más infraestructura).

### Oleada 0 — Infraestructura y flujo de portación

Objetivo: dejar asentado el patrón de portar+validar una librería sin tocar
hardware.

- **`libmisc`** (`sintab`, `fx`, `sort`, `crc32`, `console`, `checksum`) y **las
  funciones de `libc`** (`string`, `stdlib` qsort/random, `stdio` kvprintf).
- Mapear contra `eng::core`: `SIN/COS` → `core::sinetable`, div/mod → `core::fast_div`.
- Validadores: efectos que usan solo tablas/fx (p. ej. `04-plasma`, `52-sea-anemone`, `14-metaballs` por su generación de valores).

**Piloto hecho (2026-09):** portados y validados con test host:

- `engine/include/eng/core/isqrt.hpp` — port fiel de `libmisc/fx.c` (`isqrt`
  tabla + nlz, sin división/floats); validado por equivalencia con el C
  original compilado (test HOST-000).
- `engine/include/eng/core/sort.hpp` — `eng::quick_sort` (quick + inserción,
  genérico sobre `Span<T>` con comparador) y `eng::sort_items` (equivalent de
  `SortItemArray`); validado por el mismo test HOST-000.
- Infraestructura de **test unitario host**: `tests/host/` con
  `tools/run-host-tests.sh` (usa el `g++` del entorno del toolchain, sin WSL,
  sin MSVC). Ver `tests/host/README.md`.

Pendiente de `libmisc` en esta oleada: `crc32`, `console` (depende de
`libgfx`, pasa a Oleada 1), `sync`, `file`; y de `libc`: `string`/`stdlib`
(qsort/random)/`stdio` (kvprintf/snprintf).

### Oleada 1 — libgfx (display base)

Objetivo: portar la capa de bitmaps, copper lists y sprites.

- `NewBitmap/BitmapSetPointers/BitmapMakeDisplayable`, `CopList*`, `CopSetup*`
  (bitplanes, display window, mode, sprites), `Sprite*`, `Palette/Color*`,
  `c2p_1x1_4` (asm).
- Mapear contra `eng::graphics::copper`, `frame_plan.hpp`, `bitmap.hpp`.
- Validadores: `01-empty`, `02-circles`, `03-color-cycling`, `04-plasma`,
  `50-roller`, `53-showpchg`.

### Oleada 2 — libblit (blitter)

Objetivo: portar las operaciones de Blitter reutilizables.

- `BlitterCopy/CopyArea/CopyFast/CopyMasked`, `BlitterFillArea`, `BlitterLine`,
  `BlitterSetArea`, `BitmapAddSaturated/DecSaturated/IncSaturated/Or/SetArea/...`,
  `WordMask`.
- Mapear contra `frame_plan.hpp` (BlitJob) y el backend blitter del engine.
- Validadores: `11-game-of-life`, `14-metaballs`, `58-tiles8`, `59-tiles16`,
  `67-weave`.

### Oleada 3 — lib2d y lib3d

Objetivo: portar la matemática (independiente de hardware, testeable en host).

- `lib2d`: `ClipLine2D`, `ClipPolygon2D`, `Transform2D`, `Rotate2D`, `Scale2D`,
  `Translate2D`, `PointsInsideBox`.
- `lib3d`: `NewObject3D`, `Transform3D`, `Compose3D`, `UpdateFaceVisibility`,
  `SortFaces`, `LoadRotate3D`, etc.
- Validadores: `06-wireframe`, `30-flatshade`, `56-texobj`, `65-uvmap`.
- **Host tests**: al ser matemática pura, permiten test unitario en el host
  (`tests/`), no solo demo en emulador.

### Oleada 4 — libgui

Objetivo: UI reutilizable (widgets, eventos, teclado+ratón).

- Portar `gui.c/font.c` como `eng::ui` inspirado en su API pero integrado al
  engine (layout, temas, navegación).
- Validador: `37-gui`, `38-kbtest`.

### Oleada 5 — audio (libp61, libpt, libahx, libctr)

Objetivo: wrappers C++ sobre reproductores asm.

- Conservar `p61.asm/pt.asm/ahx.asm/cinter.asm` en `support/`; exponer
  `eng::audio` con init/play/stop/vblank.
- Validadores: `46-playp61`, `47-playpt`, `44-playahx`, `45-playcinter`.

### Oleada 6 — efectos complejos y restantes

Objetivo: cerrar la cobertura con los efectos que combinan varias librerías.

- `05-fire-rgb`, `13-highway`, `23-bumpmap-rgb`, `26-cathedral`,
  `60-tilezoomer`, `63-twister-rgb`, `66-uvmap-rgb`, etc. Se portan cuando la
  infraestructura base exista; suelen quedar como demos compuestas que validan
  la combinación de librerías ya portadas.

## 4. Flujo por librería (pasos repetibles)

Para cada librería, en cada oleada, seguir este pipeline:

1. **Inventario**
   - Leer el `.h` público (en `include/`) y los `.c` de `lib/<lib>/`.
   - Listar funciones, tipos y dependencias (hacia otras libs: ej. libblit usa types/gfx).
   - Enumerar los efectos de `effects/` que la consumen (catálogo en `docs/06-catalogo-efectos.md`).
2. **Crosswalk con `demoscene-repo` (no fiable)**
   - Comparar cómo la conversión GCC abordó cada pieza. Extraer pistas, no
     copiar. Verificar contra el original cuando haya duda.
3. **Mapeo con el engine**
   - Para cada función: ¿ya existe equivalente en `eng::`? → mapear y no portar.
     ¿Es específica y vale la pena? → marcar para portar. ¿No aporta? → descartar
     y anotar por qué (regla: solo la mejor información).
   - Decidir namespace destino según tabla de §2.
4. **Portación C++23**
   - Traducir con el estilo del engine (codigar a `AGENTS.md`/`CODING_STYLE.md`).
   - Los `.asm` se referencian desde `support/`, no se traducen.
   - Añadir comentarios didácticos en español (qué invariante mantiene, por qué
     una alternativa simple sería más cara).
5. **Validación con efectos**
   - Elegir al menos un efecto de la librería y adaptarlo como demo
     `demos/amiga/NNN_...` sobre la librería portada.
   - Verificar `build -> run -> analyze`, evidencia real y regresión
     (`tools/test-regression.sh`).
6. **Promoción y documentación**
   - Cuando la API esté estable y validada por ≥1 efecto, dejarla en `engine/`.
   - Añadir ficha técnica si aplica (`docs/reference/amiga/techniques/`) y
     actualizar el coverage-index y este roadmap.

## 5. Índice de cobertura (seguimiento por librería)

Estado por librería (actualizarlo en cada cambio de estado):

| Librería | Inventario | Portado | Validado por efecto | En engine | Efectos validadores | Notas |
|---|---|---|---|---|---|---|
| `libmisc` (fx/sort) | ✅ | ✅ | ✅ (host) | ✅ | (sin efecto aún) | `isqrt` y `sort` en `eng/core` (isqrt.hpp, sort.hpp); validados por test HOST-000. `sintab`→`core::sinetable`. Pendientes: crc32, console (→Oleada 1), sync, file. |
| `libc` (string/stdlib/stdio) | ✅ | 🔄 | ❌ | 🔄 | 04, 14 | sustituir por `eng::core::*`; no portar lo que el engine ya da. Inventariado; pendiente de portar/mapear. |
| `libgfx` (bitmaps/copper/sprites/c2p) | ❌ | ❌ | ❌ | ❌ | 01, 02, 03, 04, 50, 53 | contra `graphics::copper`, `bitmap.hpp`, `frame_plan` |
| `libblit` (blitter) | ❌ | ❌ | ❌ | ❌ | 11, 14, 58, 59, 67 | contra `frame_plan` (BlitJob) |
| `lib2d` | ❌ | ❌ | ❌ | ❌ | 06, 30, 56 | host tests |
| `lib3d` | ❌ | ❌ | ❌ | ❌ | 06, 30, 56, 65 | host tests |
| `libgui` | ❌ | ❌ | ❌ | ❌ | 37, 38 | `eng::ui` |
| `libp61/libpt/libahx/libctr` | ❌ | ❌ | ❌ | ❌ | 46, 47, 44, 45 | asm en `support/` |

Leyenda: ❌ pendiente · 🔄 en curso · ✅ hecho.

## 6. Piloto de la Oleada 0 (hecho)

**`libmisc` (fx + sort) mapeada a `eng::core`** — primera pieza portada y
validada con test host.

Del flujo del piloto:

1. ✅ Inventariado: `libmisc` (`sintab`, `fx`, `sort`, `crc32`, `console`,
   `checksum`, `sync`, `file`) y `libc` (`string`, `stdlib`, `stdio`).
2. ✅ Mapeo: `SIN/COS` → `core::sinetable` (ya existe), `fast_div` → `core::fast_div`;
   `isqrt` y sort se portaron a `eng/core/isqrt.hpp` y `eng/core/sort.hpp`.
3. ✅ Test host: `tests/host/000_eng_core_math` + `tools/run-host-tests.sh`
   (usa el `g++` del entorno del toolchain; sin WSL, sin MSVC).
4. 🔄 Validación con demo mínima en WinUAE: pendiente (el test host ya cubre la
   corrección; falta un efecto/demo que lo use como validación visual).
5. ✅ Índice de cobertura actualizado (ver §5, fila `libmisc`).

Siguiente paso recomendado: portar/mapear el resto de `libmisc`/`libc`
(`crc32`, `string`, `stdlib`, `stdio`) con su test host, o saltar a `libgfx`
(Oleada 1) cuando se necesite un efecto validado en WinUAE.

## 7. Referencias

- Repo origen: `C:\Users\dvdjg\Documents\programa\AI\Amiga\demoscene-repo-orig`
  (código en `lib/`, headers en `include/`, efectos en `effects/`, docs en `docs/`).
- Conversión GCC (no fiable): `C:\Users\dvdjg\Documents\programa\AI\Amiga\demoscene-repo`.
- Índice técnico previo del repo de demoscene (para este engine y el C):
  [`../DEMOSCENE_REPO_INDEX.md`](DEMOSCENE_REPO_INDEX.md).
- Política de réplica de efectos: [`DEMOSCENE_EFFECT_REPLICATION_POLICY.md`](DEMOSCENE_EFFECT_REPLICATION_POLICY.md).
- Índice efecto a efecto previo (cobertura contra el engine C):
  [`demoscene-repo-coverage-index.md`](demoscene-repo-coverage-index.md).
- Roadmap previo de importación (oleadas por efecto, ahora superado por este
  enfoque por librerías): [`demoscene-repo-import-roadmap.md`](demoscene-repo-import-roadmap.md).
- Catálogo de técnicas del origen: `docs/06-catalogo-efectos.md` y `docs/07-referencia-apis.md` en el repo origen.