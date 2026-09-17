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
- Referencia de rendimiento para 68000: `docs/guides/optimization/OPTIMIZACION_GPP_68000.md`
  (documento vivo: [✓] verificado / [✗] corregido / [P] pendiente contra el toolchain,
  con bitácora de descubrimientos en su §8 y sonda reproducible en
  `docs/guides/optimization/_probe_gpp68000.cpp`).

## Reglas obligatorias de diseño

Estas reglas hay que preservarlas en todo el código. `AGENTS.md` las enruta aquí.

- **APIs paramétricas, nunca de tamaño fijo**: no generar funciones con geometría/tamaño embebido (p. ej. `emit_ehb_320x256_display`); el engine expone métodos paramétricos (registros/planos/ancho, etc.) y el llamador decide los valores. Los «magic numbers» de un caso concreto viven en la demo/config, no como API.
- **La lógica de juego es agnóstica del backend**: los registros/DMA específicos de Amiga viven en las capas backend/driver, nunca en la lógica de alto nivel (ver «Arquitectura»).
- **Comentarios didácticos**: el código nuevo de hardware Amiga debe incluir comentarios breves, en español y con estilo de tutorial, que expliquen qué registro o mecanismo del chipset interviene, qué invariantes mantiene el algoritmo y por qué una alternativa aparentemente más simple consumiría más CPU, Blitter o Chip RAM. Cuando una decisión sea difícil de inferir, enlazar al MD técnico correspondiente y usar un pequeño esquema ASCII si aclara la geometría de buffers, Copper, bitplanes o zonas visibles.

Las reglas de la **frontera de API pública** (sin hardware, sin punteros ni mecanismos inseguros, versátil para cualquier configuración) están en [PUBLIC_API.md](PUBLIC_API.md).
