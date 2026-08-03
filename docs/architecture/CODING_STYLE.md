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
