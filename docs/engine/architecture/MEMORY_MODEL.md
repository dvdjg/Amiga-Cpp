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

## Asignadores y vidas útiles

El modelo vigente separa dos vidas útiles:

- `MemBank<Chip>`/`BlockPool`: destino de recursos persistentes liberables. La integración
  productiva del backend Amiga todavía está en migración y puede usar `configure_backing`.
- `MemBank<Fast>`/`MemBank<Slow>`: datos de CPU, con selección efectiva `Fast` si existe y `Slow`
  como fallback.
- `ScratchArena`: memoria temporal de setup/fase/frame, reiniciable y sin liberación individual.
- `MemoryManager`: fachada interna que coordina los bancos; `MemorySystem` conserva el scratch y
  las compatibilidades de composición que aún están en migración.

Los recursos DMA persistentes no deben reservarse directamente mediante `LinearArena`. Mientras la
migración no esté cerrada, esta regla es una condición de arquitectura pendiente, no una garantía
automática de todas las rutas existentes.

Ninguna demo debe hacer asignaciones dinamicas durante el bucle principal salvo que
la fase lo declare explicitamente como una prueba de fallo.
