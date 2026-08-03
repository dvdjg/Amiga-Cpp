# Indice tecnico de `demoscene-repo`

Repositorio externo analizado:

```text
C:\Users\David\Documents\Programa\Amiga\demoscene-repo
```

Este documento no pretende copiar codigo del repositorio. Sirve como mapa de
consulta para extraer tecnicas y convertirlas en abstracciones propias del engine.
La autoridad para registros y timing sigue siendo el Hardware Reference Manual; el
repo de demoscene se usa como catalogo de soluciones probadas.

## Estructura util

- `docs/`: documentacion ya sintetizada del propio repositorio.
- `docs/tutoriales/`: tutoriales por efecto, del 01 al 67, con hardware implicado.
- `effects/`: efectos ejecutables independientes.
- `effects/tiles16/`: referencia inmediata para scroll por tiles 16x16.
- `lib/lib3d/`: matematica 3D fixed-point, visibilidad, luz por cara y ordenacion.
- `include/`: APIs de `gfx`, `blitter`, `copper`, `effect`, `3d`, etc.
- `system/`: inicializacion, interrupciones, VBlank y servicios base.

## Patrones de frame y VBlank

El sistema de efectos del repo separa callbacks:

- `Init`: reserva bitmaps/copper y configura modo.
- `Render`: construye el frame, lanza blits, actualiza copperlists.
- `VBlank`: opcional, para trabajo corto en la interrupcion.
- `Kill`: libera y detiene DMA/copper/blitter.

Para nuestras demos, el criterio sera:

- construir el `FramePlan` y preparar buffers antes del commit visible;
- esperar a VBlank para publicar punteros de bitplanes, copperlist activa o
  cambios estructurales de display;
- evitar capturas en mitad de una secuencia de blits si el resultado visible aun
  no esta estable;
- usar el canal lateral y `g_amg_run_status` como evidencia logica, y capturas
  solo despues del estado `Ready`.

`tiles16.c` usa un patron muy interesante: actualiza el buffer no activo, parchea
punteros de bitplanes y `BPLCON1`, ejecuta la copperlist, espera VBlank y alterna
el buffer activo.

## `effects/tiles16`

Ruta:

```text
C:\Users\David\Documents\Programa\Amiga\demoscene-repo\effects\tiles16\tiles16.c
```

Tecnicas relevantes:

- Mapa de tiles 16x16 con dirty flags embebidos en los bits bajos del indice.
- Dos dirty bits, uno por buffer oculto, para no refrescar el mapa completo.
- Bitmap ligeramente mas grande que la ventana visible: `WIDTH = 320 + TILEW`,
  `HEIGHT = 256 + TILEH`.
- Scroll grueso por punteros `BPLxPT` y scroll fino por `BPLCON1`.
- Doble copperlist + doble pantalla.
- Tiles interleaved (`BM_INTERLEAVED`): un solo blit A->D puede estampar los 5
  planos de un tile, usando altura `TILEH * DEPTH`.
- `WAITBLT` antes de tocar registros del blitter o lanzar el siguiente tile.
- Publicacion visible despues de preparar el buffer: patch de copper, run list,
  VBlank, swap.

Ideas para nuestro engine:

- `TileBlockCopy` debe evolucionar hacia una variante interleaved que pueda copiar
  todos los planos de un tile en un solo job de blitter.
- El tilemap retenido deberia tener dirty bits por buffer o por pagina visible,
  no solo una lista plana de rectangulos.
- El driver de scroll debe distinguir:
  - actualizacion de tiles ocultos;
  - coarse scroll mediante punteros de bitplane;
  - fine scroll mediante `BPLCON1`;
  - commit sincronizado con VBlank.
- `CopperScheduler` debe ser el unico que escriba `BPLCON1` y `BPLxPT`; el tilemap
  aporta intenciones de scroll.

## `lib/lib3d`

Ruta:

```text
C:\Users\David\Documents\Programa\Amiga\demoscene-repo\lib\lib3d
```

Resumen tecnico:

- Matematica 3D CPU-side en fixed-point, sin floats.
- Matriz 3x3 + traslacion.
- Rotacion mediante tablas seno/coseno.
- Transformacion de vertices con bucles/macro desenrollados.
- Back-face culling por producto escalar normal-camara.
- Luz por cara con tabla `InvSqrt[]`, sin raiz cuadrada runtime.
- Ordenacion de caras por Z media o Z minima para painter's algorithm.
- Dibujado desacoplado: wireframe, flatshade, textura o stencil pueden usar CPU,
  blitter y copper segun efecto.

Ideas para nuestro engine:

- Crear mas adelante un modulo `amg::math3d` freestanding con tipos compactos:
  `Vec3s`, `Mat3x4`, `Mesh`, `Object3D`, `Face`.
- Mantener la 3D como productor de comandos de render, no como codigo que escriba
  custom registers.
- Los efectos 3D deberian emitir `FramePlan`/`BlitterQueue`/`CopperIntent`, igual
  que los sistemas 2D.
- Para juegos, empezar por 3D decorativa o de efectos: objetos simples, fondos,
  transiciones, mapas de luz, stencils.

## Catalogo de efectos prioritarios

Lectura recomendada primero:

- `04-plasma`: copper por bloques, tablas seno/coseno, doble buffer.
- `05-fire-rgb`: buffer chunky, conversion chunky-to-planar.
- `08-floor`: scroll/floor con paleta dinamica y copper por linea.
- `09-textscroll`: punteros de bitplane por linea desde copper.
- `11-game-of-life`: minterms creativos del blitter.
- `13-highway`: multiples zonas, sprites y bitplanes por franja.
- `14-metaballs`: blobs, mascaras y doble buffer.
- `39-layers`: dual playfield y prioridad.
- `50-roller`: efecto cilindro con punteros por linea.
- `53-showpchg`: cambios de paleta por linea.
- `55-stencil3d`: mascara 3D y minterms.
- `56-texobj`, `65-uvmap`: UV mapping, CPU + blitter por franjas.
- `60-tilezoomer`: tiles + zoom.
- `67-weave`: orden de pintado y blitter para bandas/cruces.

## Lineas de trabajo para nuestro roadmap

1. Scroll tilemap con zona no visible real:
   - dirty bits por doble buffer;
   - staging de columnas/filas;
   - coarse scroll por `BPLxPT`;
   - fine scroll por `BPLCON1`;
   - commit en VBlank.

2. Blitter interleaved:
   - layout de bitplanes interleaved opcional;
   - tile 16x16 en un blit para N planos;
   - presupuesto separado para `TileBlockCopy`.

3. Copper intents asociados a recursos:
   - scroll fino;
   - paleta por franja;
   - punteros por linea;
   - modulaciones/ondulaciones.

4. 3D fixed-point:
   - portar conceptos, no codigo literal;
   - producir comandos abstractos;
   - validar con demos pequenas: wireframe, flatshade, stencil.

5. VBlank discipline:
   - el engine debe exponer fases claras;
   - las demos no deben cambiar punteros visibles fuera de una ventana controlada;
   - las pruebas automatizadas deben capturar despues del commit estable.
