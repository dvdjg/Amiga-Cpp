# Modelo de memoria inicial

Perfil inicial: `A500_1MB_Slow`.

Esta configuracion suele significar:

- 512 KB de Chip RAM.
- 512 KB de Slow RAM/trapdoor.
- 0 KB de Fast RAM real.

La Slow RAM aumenta la capacidad disponible para codigo, scripts, tablas, textos y
datos no usados directamente por DMA, pero no elimina la contencion del bus como lo
haria una Fast RAM autentica.

## Regla principal

Todo recurso leido por el chipset debe vivir en Chip RAM:

- bitplanes;
- copperlists;
- sprites hardware;
- audio DMA;
- buffers fuente/destino del blitter cuando el blitter accede a ellos.

La Slow RAM puede usarse para:

- logica de juego;
- scripts;
- tablas de rutas;
- textos;
- recursos comprimidos antes de descomprimir/cocinar a Chip RAM;
- metadatos de escena;
- estructuras de entidades no usadas por DMA.

## Arenas iniciales

El engine arranca con estas ideas:

- `ChipArena`: recursos DMA persistentes o temporales de frame.
- `SlowArena`: datos no DMA, staging y metadatos.
- `FrameScratch`: memoria temporal reiniciada cada frame.

Ninguna demo debe hacer asignaciones dinamicas durante el bucle principal salvo que
la fase lo declare explicitamente como una prueba de fallo.

