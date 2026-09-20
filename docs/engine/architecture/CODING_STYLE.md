# Estilo C++ del engine

El objetivo no es escribir C con clases. El engine debe usar C++ como herramienta de
abstraccion, pero sin perder control sobre memoria, coste y layout.

## Reglas base

- Dialecto: `gnu++23`.
- Sin exceptions.
- Sin RTTI.
- Sin asignacion dinamica durante gameplay.
- Sin dependencias de STL pesada en runtime Amiga.
- Interfaces calientes mediante templates/concepts o funciones simples, no virtuales.
- Polimorfismo runtime solo donde el coste este fuera de bucles criticos.
- Recursos con ownership explicito: arena, pool o handle.
- Datos para DMA siempre marcados por memoria objetivo: Chip, Slow, Fast o Any.
- Cada unidad de codigo fuente debe estar comentada como un tutorial pequeno.
- Las cabeceras compartidas deben explicar intencion, coste, restricciones y uso
  esperado, pensando en generar documentacion mas adelante.
- Los comentarios deben aclarar decisiones close-to-the-metal: registros, DMA,
  memoria, blitter, copper, VBlank/HBlank o uso del ROM kernel.

### Documentación de código (obligatoria)

- **Toda función** lleva al menos un comentario descriptivo: **qué hace**, **quién la usa** y en
  **qué contexto**, y **qué otras funciones/clases usa** desde ahí (si es relevante). Se
  documentan **parámetros de entrada y de salida** (incluido el valor de retorno y su rango).
- **Estructuras y clases auxiliares, constantes, variables globales, macros y `enum`** también
  llevan su comentario (cometido y, si aplica, unidades/rango/layout/ABI).
- **Miembros de clase: uno a uno, todos documentados.** Cada **método** (público o privado) y cada
  **dato miembro** (público, protegido o privado) lleva **su propia descripción** de al menos una
  línea, aunque el nombre parezca obvio; igual para **constantes** (`constexpr`/`static constexpr`),
  **variables estáticas** (de clase o de función) y **campos de `struct`**. No se admite un miembro
  sin comentario (p. ej. `m_buffer_count`, handles, contadores, punteros de estado): describe qué
  guarda, su rango/unidades y, si aplica, su relación con el layout/hardware. Un grupo de miembros
  homogéneo puede documentarse con un comentario de bloque **siempre que** cada campo quede
  explicitado (no vale un comentario que solo cubra el primero).
- **Bug arreglado u optimización**: se documenta **en el código** (y en el commit) *por qué* se
  hizo así, para que una refactorización futura **no lo deshaga** (p. ej. "no copiar 512 B por
  frame: `retarget`"; "no zero-init de la timeline: bitset de tocadas").
- **Preservar los comentarios antiguos**: adaptarlos o corregirlos, no reescribirlos; **sólo se
  borran** si ya no aplican o son falsos.
- **Clase fundamental de la arquitectura** (mucha del engine): acompañarla de un **diagrama
  ASCII** de arquitectura que deje claro su **cometido** y su **relación** con las demás piezas
  (formato: cajas `┌─┐` para layout/relaciones; ver §2 de `AGENTS.md`).

## Arquitectura

La logica de juego debe depender de abstracciones del engine, no del Amiga. El Amiga
es un backend. La misma logica deberia poder compilar algun dia contra otro backend
como Mega Drive, Neo Geo o PC de herramientas.

Separaciones importantes:

- `Game`: reglas, entidades, scripts, intencion de render.
- `Engine`: scheduling, memoria, recursos, escena.
- `GraphicsDriver`: traduce intenciones a una estrategia grafica concreta.
- `PlatformBackend`: hardware, input, audio, reloj, display y debug.
- `UAF-R`: datos cocinados de runtime.

## Frontera de API publica

- La aplicacion (juego/demo) **no conoce hardware**: no incluye `<hardware/*.h>`, no nombra
  registros, planos, bitplanes, punteros, Copper ni modos de display, y **no elige** la composicion
  (single/DPF/HAM/chunky): la deduce el planner interno.
- El codigo de aplicacion usa solo `engine/include/eng/api/` (mas tipos de valor de `eng/core/`).
- El acceso a la vista de hardware (`hardware_view()` y similares) es **interno** (composicion) o
  de **debug**; nunca de la logica de juego.
- Si una funcion nueva obliga a la app a conocer el hardware, falta una abstraccion.
- Detalle y ejemplos: `PUBLIC_API.md`.

## Criterio de diseno

Una abstraccion es buena si:

- elimina decisiones repetidas del juego;
- mantiene visible el coste hardware;
- puede verificarse con tests o profiler;
- no oculta asignaciones ni copias caras;
- permite cambiar de driver grafico sin reescribir la logica de juego.

## `inline` no es una sugerencia de inline

En una libreria **header-only** como esta, `inline` en una funcion de cabecera es un
especificador de **enlace** (permite la definicion en varias unidades de traduccion sin
colision de simbolo, requisito ODR), **no** una orden ni una sugerencia de inline. Los
`template` y las funciones `constexpr` (con cuerpo en la cabecera) ya son implicitamente
`inline`; marcarlos `inline` es redundante pero es la convencion del repo (explicita).

Que una funcion sea `inline` **no obliga** al compilador a inlinearla: la decision es suya,
por coste/tamaño. Una funcion grande marcada `inline` (p. ej. `mesh_render_poly_filled`)
simplemente se emite una vez por programa y se llama como cualquier otra; no hay que
"quitarle el inline" para evitar inlinarla (quitarlo, de hecho, romperia el enlace si la
cabecera se incluye en mas de una unidad).

El control real de inlining son los atributos, y se usan con criterio:

- `[[gnu::always_inline]]`: **forzar** el inline. Reservado a helpers **diminutos y
  calientes** donde el `jsr` pesa mas que el cuerpo (`lerp`, `move_towards`, `bezier*`,
  `Scheduler::move`, los accesos de nodo de `expr.hpp` que deben fundirse en el bucle). Si
  se pone en una funcion grande, se hincha el binario y crece el tiempo de compilacion.
- `[[gnu::noinline]]`: **prohibir** el inline (p. ej. para aislar un punto de medida).

Regla: no marcar `always_inline` por costumbre; justificarlo con el perfil o el `.s`.

## Seguridad de tipos sobre punteros crudos

El runtime Amiga es freestanding (`-nostdlib`, sin STL hosted), asi que el engine
aporta sus propias piezas de seguridad de C++23 sin depender de `std::span`:

- `eng::Span<T>` (`engine/include/eng/core/span.hpp`): vista contigua con tamaño.
  Prohibe el fallo clasico de pasar puntero y contador por separado
  (`clear_bytes(u8*, u32)`): el tamaño viaja con la vista, `operator[]` es de coste
  cero como en `std::span`, y `at()` verifica el rango disparando `illegal`
  (0x4afc) en m68k ante una violacion.
- Regla: los buffers de arenas, bitplanes, caches de tiles y listas de comandos se
  manipulan mediante `Span`; solo las capas de hardware (Blitter, Copper, DMA)
  pueden recibir el puntero crudo, y solo el tiempo justo para programar registros.
- Evitar "puntero + count" en firmas de API; si una funcion necesita memoria
  propia, pedir `Span` por valor y devolver `Span` (mutable solo si escribe).
- **Nada de punteros crudos ni `char*` en la frontera**: el texto de solo lectura se pasa
  como `StringView`; las tablas de tamano fijo, como `eng::util::Array`; los datos
  empaquetados se leen/escriben con `ByteReader`/`ByteWriter` (`core/util/binary.hpp`),
  nunca con `reinterpret_cast` (falla por alineacion en 68000) ni con aritmetica de
  punteros. El sufijo `*` solo vive dentro del almacenamiento de estas vistas.
- **Polimorfismo estatico**: cuando un backend o politica varia (fuente de bloques, reloj,
  escalar, juego), se usa un tipo/`concept` en plantilla, no un puntero a funcion ni `void*`.
- **Referencias no propietarias**: para "usa este objeto, pero no es su dueno" (p. ej.
  `Surface` sobre `Playfield`, `copper::Plan` sobre `DoubleBuffer`) se usa `eng::Ref<T>`
  (anulable) o `eng::NonNull<T>` (contrato no nulo) de `core/ptr.hpp`, **no** `T*` en miembros
  ni en firmas; `eng::Opt<T>` cubre el opcional **en sitio** (sin `std::optional`/heap). Los
  buffers siguen con `Span`/`Bytes<Tag>`; solo la frontera de hardware (Copper/Blitter/DMA) usa
  el puntero crudo. El gate `tools/check/raw-pointer-members.mjs` avisa si aparece un
  `Tipo* m_campo` nuevo (baseline para buffers de almacenamiento).
- **Enteros de maquina**: para acumuladores e indices cuyo rango cabe en palabra, usar el
  entero elegido en compilacion (`eng::intw`; p. ej. `eng::board::board_int`), no `s32`
  por defecto. Reservar `s32`/`u64` para cuando el rango lo exige (puntuaciones de
  ordenacion, nodos) y el coste es irrelevante.
- **Buffers con dominio, escalares sin envolver**: los buffers/punteros internos usan tipos de
  dominio (`Bytes<Tag>`/`Words<Tag>`, `eng/core/typed.hpp`; p. ej. `PlaneBytes`, `Pattern`,
  `AudioSample`). **No** se envuelven escalares (ancho/alto/stride/planes): van como `u8`/`u16`/`u32`.
  Un error de dominio (p. ej. audio como origen gráfico, base ≠ front) debe **no compilar**.
  Inventario y reglas: `INTERNAL_TYPE_SYSTEM.md`.
- **Los productores devuelven tipos de dominio**: quien entrega un buffer (p. ej. `bitplanes()`,
  `MemoryBlock::buffer<Tag>()`) lo devuelve ya tipado, de modo que el consumidor conecta **sin
  casts**; forzar un cast explícito anula la detección del compilador y hay que evitarlo.
- **El campo nace etiquetado**: al reservar memoria, guardar directamente `eng::Block<Tag>`
  (dominio + `MemoryKind`) con `allocate_block<Tag>()`, en vez de un `MemoryBlock` crudo que luego
  se convierte a vista. Así el tipo se conoce desde el origen y un uso indebido no compila.

  ```cpp
  // Sí: reserva tipada, el dominio y el medio viajan con el objeto.
  eng::Block<eng::CopperTag> m_copper = memory.chip.allocate_block<eng::CopperTag>(2048u, 16);
  copper::Scheduler sched { m_copper };            // valida Chip RAM en el builder

  // No: reserva cruda + conversión posterior (pierde la comprobación).
  eng::MemoryBlock m_copper = memory.chip.allocate(2048u, 16);
  eng::CopperWords words = m_copper.view<eng::CopperTag>();
  ```

  Excepciones (documentadas en el sitio): buffers que consume asm/backend crudo, el núcleo de
  memoria (`Bitmap`), descriptores que alternan memoria propia y aliaseada y scratch genérico.
  Detalle e inventario: `INTERNAL_TYPE_SYSTEM.md` §1.
- **Descriptor propio o aliaseado**: cuando un dato puede ser memoria reservada (Chip) **o** una
  región incrustada de solo lectura (un `incbin`), usa un descriptor con la **vista de dominio +
  `MemoryKind`** en vez de un `MemoryBlock` crudo. Ejemplo canónico:
  `struct XlimitedTileBank { TileBankBytes view; MemoryKind kind; }` con `valid()`/`words()`
  (`engine/include/eng/field/xlimited_scene.hpp`); así el banco aliaseado no exige `const_cast` y
  el propietario recibe el dominio correcto.
- **El tipo dueño expone la conversión al dominio**: si un tipo posee el array/puntero (p. ej.
  `EhbPalette` con `color[32]`), ofrece la vista (`operator PaletteWords`, `words()`) para que el
  llamador pase el objeto; no se escribe `PaletteWords{ arr }` a mano.
- **Frontera `unsafe`**: `from_raw()`/`raw()` son explícitos y solo los usa la capa de
  backend/`BlitJob`; el resto del engine consume tipos de dominio.
- **`bool` para booleanos, bytes para layout**: usa `bool` en flags de estado semánticos
  (`valid`, `visible`, `enabled`); reserva `u8`/`s8` para **bytes empaquetados, IDs/índices
  pequeños, máscaras de bits y structs de ABI/DMA/`incbin`** (el `objdat` de lib3d, campos
  de registro). Nunca uses `bool` en un struct que cruza a hardware o a una herramienta
  externa. Detalle y plan: `docs/guides/roadmap/REFACTOR_SCALAR_GENERICO.md`.
- Referencia de rendimiento para 68000: `docs/guides/optimization/OPTIMIZACION_GPP_68000.md`
  (documento vivo: [✓] verificado / [✗] corregido / [P] pendiente contra el toolchain,
  con bitácora de descubrimientos en su §8 y sonda reproducible en
  `docs/guides/optimization/_probe_gpp68000.cpp`).

## Reglas obligatorias de diseño

Estas reglas hay que preservarlas en todo el código. `AGENTS.md` las enruta aquí.

- **APIs paramétricas, nunca de tamaño fijo**: no generar funciones con geometría/tamaño embebido (p. ej. `emit_ehb_320x256_display`); el engine expone métodos paramétricos (registros/planos/ancho, etc.) y el llamador decide los valores. Los «magic numbers» de un caso concreto viven en la demo/config, no como API.
- **La lógica de juego es agnóstica del backend**: los registros/DMA específicos de Amiga viven en las capas backend/driver, nunca en la lógica de alto nivel (ver «Arquitectura»).
- **Comentarios didácticos**: el código nuevo de hardware Amiga debe incluir comentarios breves, en español y con estilo de tutorial, que expliquen qué registro o mecanismo del chipset interviene, qué invariantes mantiene el algoritmo y por qué una alternativa aparentemente más simple consumiría más CPU, Blitter o Chip RAM. Cuando una decisión sea difícil de inferir, enlazar al MD técnico correspondiente y usar un pequeño esquema ASCII si aclara la geometría de buffers, Copper, bitplanes o zonas visibles.
- **Regla de oro de genericidad de cabeceras**: una cabecera debe ser **lo más genérica posible**. Sólo las de una **implementación concreta** específica de hardware (`eng/retro/`, `eng/platform/`, `eng/cpu/`) o del **propio escalar** (`fixed*.hpp`, `minifloat*.hpp`) pueden nombrar representaciones concretas (`Fixed<s16,…>`/`Fixed<s32,…>`, `q0/q8/q12/q24`, `MiniFloat16`). Un algoritmo **no fija el escalar**: lo recibe por plantilla (`S`) y pide lo que necesite por los **puntos de extensión** (`scalar_traits`, `scalar_div`, `scalar_const`, `numeric_traits`, `noise_traits`, `scalar_sin`/`scalar_cos`…, `mesh_traits`), que el escalar especializa en **su** cabecera. Ejemplo: `mesh3d` se plantilla sobre el escalar de coordenada y el culling 16 bits (`muls.w`) vive en `eng/retro/fixed_mesh.hpp`. El gate `tools/check/generic-headers.mjs` lo verifica en cada pasada (con baseline de la deuda histórica en `generic-headers-baseline.txt`).

Las reglas de la **frontera de API pública** (sin hardware, sin punteros ni mecanismos inseguros, versátil para cualquier configuración) están en [PUBLIC_API.md](PUBLIC_API.md).
