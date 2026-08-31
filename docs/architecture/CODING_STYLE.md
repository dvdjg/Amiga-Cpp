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
- Referencia de rendimiento para 68000: `docs/architecture/OPTIMIZACION_GPP_68000.md`
  (documento vivo: [✓] verificado / [✗] corregido / [P] pendiente contra el toolchain,
  con bitácora de descubrimientos en su §8 y sonda reproducible en
  `docs/performance/_probe_gpp68000.cpp`).
