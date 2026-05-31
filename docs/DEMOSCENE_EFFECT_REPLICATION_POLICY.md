# Politica para replicar efectos de demoscene

Repositorio de referencia:

```text
C:\Users\David\Documents\Programa\Amiga\demoscene-repo\effects
```

La meta no es portar los efectos linea a linea. La meta es reconstruirlos con
nuestro engine, usando codigo limpio, capas reutilizables y pruebas automatizadas.
Cada replica debe convertirse en una pieza que pueda servir despues para juegos,
herramientas o efectos combinables.

## Principios

1. La demo expresa intenciones, no registros.

   Codigo aceptable en una demo:

   - `tilemap.scroll_to(x, y)`;
   - `scene.add_palette_cycle(...)`;
   - `frame_plan.add_tile_block_copy(...)`;
   - `copper_intents.add_fine_scroll(...)`;
   - `object3d.rotate(...)`.

   Codigo que debe quedar en capas bajas, no en la demo:

   - escribir `BLTCON0`, `BLTSIZE`, `BPLCON1`, `BPLxPT`, `COLORxx`, `COPJMP1`;
   - calcular offsets magicos de registros custom;
   - decidir manualmente si una copperlist cabe en HBlank;
   - mezclar logica del efecto con setup DMA.

2. El efecto original se usa como referencia tecnica.

   Podemos estudiar organizacion de buffers, timing, VBlank, minterms, layouts de
   blitter, trucos de copper, tablas precalculadas y presupuestos implicitos. No
   copiamos mecanicamente la arquitectura si contradice nuestras capas.

3. Las capas del engine absorben hardware.

   Cada replica debe empujar funcionalidad hacia `FramePlan`, `CopperScheduler`,
   `CopperTimeline`, `BlitterQueue`, `BlitterBudget`, drivers graficos, recursos
   runtime o math/effects reutilizables.

4. Cada demo debe ser tutorial.

   El codigo fuente compartido, especialmente cabeceras y helpers, debe explicar
   que problema resuelve, que restriccion del Amiga existe debajo, que decision de
   engine oculta esa restriccion y que queda pendiente.

5. VBlank es una frontera de commit.

   Las demos pueden construir buffers y preparar listas durante el frame, pero la
   publicacion visible debe sincronizarse con VBlank cuando afecte a punteros de
   bitplanes, copperlist activa, scroll fino `BPLCON1`, cambio de buffer visible,
   sprites hardware o paletas globales visibles.

## Flujo por efecto

Para cada efecto de `demoscene-repo/effects`:

1. Crear una ficha tecnica breve: ruta original, hardware usado, buffers, datos
   precalculados, capa del engine que debe crecer y riesgos de timing/memoria.
2. Diseñar la API limpia antes de escribir la demo.
3. Implementar una demo propia en `demos/NNN_nombre`, con README y analizador si
   produce imagen.
4. Verificar build, ejecucion en WinUAE-DBG, `side-channel READY`, captura PNG,
   analizador especifico y `tools/test-regression.ps1`.
5. Documentar el descubrimiento en README, `DEVELOPMENT_LOG`, `CONTINUATION_CONTEXT`
   y roadmap cuando aparezca una abstraccion nueva.

## Numeracion recomendada

- `000..099`: base del engine y validaciones close-to-the-metal.
- `100..199`: replicas limpias de efectos 2D, tilemap y copper.
- `200..299`: replicas con BOBs, sprites y blitter avanzado.
- `300..399`: replicas 3D fixed-point, wireframe, flatshade y stencil.
- `400..499`: audio, Paula y sincronizacion musical.
- `900..999`: estres, profiler y presupuestos.

## Primer efecto recomendado: `tiles16`

Motivo:

- coincide con el trabajo actual de `TileBlockCopy`;
- fuerza a disenar `TileScrollDriver`;
- combina blitter, copper, doble buffer y VBlank;
- es util directamente para plataformas y aventuras con fondos grandes.

Objetivo de nuestra replica:

- tilemap retenido;
- dirty bits por buffer;
- zona no visible real;
- coarse scroll por punteros de bitplane;
- fine scroll por `BPLCON1`;
- commit sincronizado con VBlank;
- demo limpia sin registros custom en la logica del efecto.

## Regla de cierre

Una replica se considera util cuando el efecto se puede explicar desde arriba:

> "El efecto pide estas intenciones; estas capas del engine las traducen al Amiga;
> estas pruebas demuestran que se mantiene dentro del presupuesto."

Si para entender la demo hay que leer registros custom en la logica principal, aun
no hemos terminado la abstraccion.
