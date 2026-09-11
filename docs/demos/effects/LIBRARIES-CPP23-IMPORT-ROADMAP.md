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
- `engine/include/eng/core/crc32.hpp` — CRC-32 IEEE de `libmisc/crc32.c`;
  validado por equivalencia (incluye el valor canónico `0xCBF43926`).
- `engine/include/eng/core/random.hpp` — xoroshiro64++ de `libc/stdlib/random.c`.
  El `rol` del original (por rangos + `swap16`) equivale a `rotl32` estándar
  (verificado); se expone la forma limpia.
- Infraestructura de **test unitario host**: `tests/host/` con
  `tools/run-host-tests.sh` (usa el `g++` del entorno del toolchain, sin WSL,
  sin MSVC). Ver `tests/host/README.md`.

Pendiente de `libmisc` en esta oleada: `console` (depende de `libgfx`, pasa a
Oleada 1), `sync`, `file`. De `libc`: `qsort` ya está superado por
`eng::core::quick_sort` (no duplicar). `string`/`stdio` (kvprintf/snprintf) NO
se portan en esta oleada: el engine usa su `Span`/builtins y `debug.text()`
acepta `const char*` fijo (sin formatos variables); si en el futuro una API
necesita volcado/formatos, portar entonces `kvprintf`/`snprintf` (con test
host). Criterio: solo se porta lo que realmente hace falta.

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

## 4-bis. Lecciones aprendidas de la conversión asm/C (bitácora de errores a evitar)

Registradas durante la portación de `libgfx` (c2p) para que un hilo nuevo no repita
los fallos. Cada lección es un error real ya cometido y corregido.

### A. Asm de demoscene → GAS (no es vasm)

El origen (`demoscene-repo-orig`) escribe asm para vasm/asm-one con convenciones que
**GNU as (m68k-amiga-elf-as)** no comparte. Al portar un `.asm`:

1. **Comentarios**: vasm usa `;`; GAS lo interpreta como separador de statements.
   Usar `/* ... */` de bloque. OJO: un `/* ... */` ANIDADO (comentario que menciona
   otro `/* */`) cierra el comentario antes y rompe todo el fichero — no escribir
   las marcas de comentario dentro del texto del comentario.
2. **Directivas**: `xdef` → `.globl`; `section '.text',code` → `.section .text.nombre,"ax",@progbits`.
3. **Literales hex**: `#$0f0f0f0f` (Motorola `$`) NO es reconocido como inmediato en
   GAS; usar `#0x0f0f0f0f`. El `$` en GAS Motorola puede tratarse como parte de un
   identificador (produce "undefined reference to `$0f0f0f0f`" en el link).
4. **`lea (An,Dn.L),Am`**: exige 68020+ en GNU as. Sustituir por
   `move.l An,Am` + `add.l Dn,Am` (semánticamente idéntico en 68000).
5. **`movem.l (sp)+,d2-d7/a2-a6`** y los mnésmicos de la familia Motorola son válidos
   con `--register-prefix-optional` (que ya usa `build-demo.sh`).

### B. ABI por pila en el asm del repo

El asm de `support/` usa la ABI C del objetivo m68k: argumentos por pila, de izquierda
a derecha, cada uno como `unsigned long` (word/long según tipo). Tras un
`movem.l d2-d7/a2-a6,-(sp)` (48 bytes), los args viven en `sp@(52)`, `sp@(56)`, ...
Ver `support/gcc8_a_support.s` (`__mulsi3`/`__udivsi3`) como plantilla exacta.

Peligro: el asm de demoscene ACCEDE a sus argumentos por **registros**, no por pila
(convención interna propia, no ABI-C). El port debe AÑADIR el prefijo que cargue los
args de la pila a los registros que la rutina espera (d0/a0/a1/...), o envolver la
rutina con un `jsr` desde un wrapper que los coloque.

### C. Algoritmos de bits (c2p/desintercalado): no reinventar, validar por equivalencia

El c2p de Kalms usa un desintercalado por máscaras (tres fases + $33333333) que NO se
puede "simplificar" a mano sin romper la semántica: el orden de escritura
(plano0/2/1/3 intercalados) es parte del contrato. Lección: ante un algoritmo así,

1. primera implementación correcta por construcción (naive, host-testable);
2. si se quiere optimizar, portar el asm EXACTO (cargando args por pila) o replicar
   las operaciones bit a bit en C++, y **validar equivalencia contra (1)** (test host
   o demo que compare byte a byte).

No validar "que enlaza" como prueba de corrección: el asm puede enlazar y aún buclear
o producir basura. La evidencia es la imagen/captura o un test de equivalencia.

### C-bis. En 68000, no acceder byte a byte en el hot path (inspeccionar `-S`)

Regla de `AGENTS.md` ("comprobar el ensamblador generado") confirmada con el c2p: si el
C++ construye un `u32` byte a byte (p. ej. `load_be` con 4 shifts para "portabilidad"),
g++ NO lo fusiona en un `move.l (a0)+` y emite ~15 instrucciones por longword en vez de
1. Lo mismo al escribir: `move.b` sueltos en vez de `move.w`.

- Para código m68k, usar `u32`/`u16` NATIVOS en las cargas/escrituras, y aislar el
  byte-swap de endianness con `#if defined(__m68k__)` (ruta nativa) vs host (swap).
- DESPUÉS de portar, regenerar `-S -O1 -m68000` y confirmar que las cargas son
  `move.l (an)` y las escrituras `move.w d0,(an)+`, no `move.b`.
- Un port "fiel" que g++ compila a código byte-a-byte pierde contra el asm a mano del
  repo; la regla de rendimiento exige revisar el asm generado y anotarlo.

### D. Emulador vs build: el READY timeout no siempre es el código

Cuando una demo nueva no llega a READY: (1) revisar el `startup-sequence` de dh0
(puede quedar pisado por una sesión F5, arrancando `:current.exe` en vez de `a.exe`);
(2) sondear `state`/`regs` por canal lateral para ver si el 68000 está vivo (PC cambiando)
y dónde se queda; (3) confirmar `configure_memory` (un mark_failed también deja READY
sin alcanzar). Un bucle infinito en rutina portada se distingue por el PC estancado.

### E. El build debe ensamblar TODO `support/*.s`

`tools/build/build-demo.sh` ahora itera sobre todos los `*.s` de `support/` (antes solo
`gcc8_a_support.s`). Para añadir otra rutina asm (p61, pt, ahx...) no hay que tocar el
script: basta colocar el `.s`. Cada `.s` produce un objeto `support_<nombre>.o`.

### F. `wait_line_pal` para líneas raster > 255 (overflow PAL)

El comparador de WAIT del Copper tiene 8 bits verticales con semántica `>=` y **SIN
bit V8** (verificado en `WinUAE-DBG/custom.cpp coppercomp`; ver
`AMIGA_8WAY_SCROLLING.md` §12). Una línea >= 256 no se puede esperar con precisión:
cualquier WAIT (incluido el doble-WAIT `0xffdf/0xfffe` de `CopWaitSafe`, portado como
`ListBuilder::wait_line_pal`) dispara en la primera coincidencia del byte bajo.

- El `wait_line_pal` (port de `CopWaitSafe`) NO resuelve el split vertical del
  corkscrew: se intentó aplicar a `XlimitedDisplayComposer` y produjo recortes
  incorrectos. Se revirtió. La limitación es del comparador, no del overflow.
- Antes de "arreglar" un split/banda con una espera a línea >= 256, releer
  `AMIGA_8WAY_SCROLLING.md` §12: ya documenta que es inherente al chipset y que el
  `XYLimited` original degrada igual a 255.
- `wait_line_pal` sigue siendo útil para otras esperas (p. ej. sincronizar a final
  de frame) pero NO para posicionar un split con precisión a una línea >= 256.

## 5. Índice de cobertura (seguimiento por librería)

Estado por librería (actualizarlo en cada cambio de estado):

| Librería | Inventario | Portado | Validado por efecto | En engine | Efectos validadores | Notas |
|---|---|---|---|---|---|---|
| `libmisc` (fx/sort/crc32) | ✅ | ✅ | ✅ (host) · demo 060 creada (build OK, pendiente corrida WinUAE) | ✅ | 060 | `isqrt` (isqrt.hpp), `sort` (sort.hpp) y `crc32` (crc32.hpp) en `eng/core`; validados por test HOST-000 y compilados en la demo 060. `sintab`→`core::sinetable`; `random`→`core::random.hpp` (xoroshiro64++, equivalente al `random.c` de libc). Pendientes: `console` (→Oleada 1), `sync`, `file`. |
| `libc` (string/stdlib/stdio) | ✅ | 🔄 (random) | ✅ (random) | 🔄 | 04, 14 | `random` portado. `qsort`→`eng::core::quick_sort` (no duplicar). `string`/`stdio` (kvprintf/snprintf): no se portan ahora (sin necesidad real; `debug.text()` es `const char*` fijo). Recomendado: portar cuando una API lo exija. |
| `libgfx` (bitmaps/copper/sprites/c2p) | 🔄 | 🔄 (c2p) | 🔄 (c2p demo 061) | 🔄 | 03, 04, 50, 53 | `c2p_1x1_4` portado: version C++ naive en `eng/graphics/c2p.hpp` (validada por demo 061) + asm de Kalms en `support/c2p_1x1_4.s` (pendiente equivalencia). `CopWaitSafe`→`wait_line_pal`. Bitmap portable pendiente. Ver `OLEADA1_LIBGFX_INVENTARIO.md`. |
| `libblit` (blitter) | ✅ | ✅ (mapeado) | ✅ | 11, 14, 58, 59, 67 | Las ops son hardware y van al backend: `FramePlan` (`frame_plan.hpp`) ya es la **cola de blits con presupuesto** (`BlitJob` copy/restore/tile/masked + `BlitBudget`/`DirtyRect`), y los minterms (`cookie_cut=0xca`, `copy_c=0xaa`) están en `amiga_minimal.cpp`. Las tablas puras `WordMask` no se portan (el backend usa HWM/LWM completos). |
| `lib2d` | ✅ | ✅ | ✅ (host HOST-010) | ✅ | 06, 30, 56 | `engine/include/eng/core/math2d.hpp`: `Mat2x2` (fixed-point 4.12, formato del origen) con `load_identity`/`translate`/`scale`/`rotate`/`transform`, tabla de seno 4096 (reusa `eng::SineTable`, sin libm), `point_flags`, `clip_line` (Liang-Barsky) y `clip_polygon` (Sutherland-Hodgman, con buffer de trabajo). Validado por `tests/host/010_math2d`. |
| `lib3d` | ✅ | ✅ (matrices + caras + malla) | ✅ (host HOST-011/013 + demos 077/078) | ✅ | 06, 30, 56, 65 | `engine/include/eng/core/math3d.hpp`: `Mat3x3` (fixed-point 4.12) con `load_identity`/`translate`/`scale`/`load_rotate` (Rx·Ry·Rz)/`load_reverse_rotate` (Rz·Ry·Rx)/`compose`/`transform`, más `Face` + `face_visible` (back-face culling por signo), `face_z_sum`/`face_z_min` (claves del painter's algorithm). Reutiliza la tabla de seno de `math2d`. Validado por `tests/host/011_math3d`. Modelo de malla en `engine/include/eng/core/mesh3d.hpp`: `MeshView` (vértices + caras), `mesh_transform` (lote) y `mesh_painter_order` (culling + orden lejos→cerca por shell sort in-place, sin asignación), validado por `tests/host/013_math3d_mesh`. Doble cara (`double_sided` en `mesh_painter_order`) y formato binario de malla `obj2c` consumido por la capa UAF-R (`MeshAssetView`; demo 078). |
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