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

- `MemBank<Chip>`/`BlockPool`: destino de recursos persistentes liberables. El backend Amiga
  configura actualmente estos pools sobre los bloques raíz; `MemorySystem` conserva arenas para
  scratch y compatibilidades de composición. Los owners que descartan el `Block` y el teardown de
  DMA siguen pendientes de cierre.
- `MemBank<Fast>`/`MemBank<Slow>`: datos de CPU, con selección efectiva `Fast` si existe y `Slow`
  como fallback.
- `ScratchArena`: memoria temporal de setup/fase/frame, reiniciable y sin liberación individual.
- `MemoryManager`: fachada interna que coordina los bancos; `MemorySystem` conserva el scratch y
  las compatibilidades de composición que aún están en migración.

Los recursos DMA persistentes no deben reservarse directamente mediante `LinearArena`. Mientras la
migración no esté cerrada, esta regla es una condición de arquitectura pendiente, no una garantía
automática de todas las rutas existentes.

Si se detecta Fast RAM, las reservas CPU-only nuevas deben preferirla mediante la política del
`MemoryManager`; el engine debe resolver el fallback y exponer el banco efectivo. La capacidad
detectada no equivale a una cuota disponible: el setup debe reservar primero el stack elegido y
descontar las necesidades del sistema antes de dimensionar pools persistentes y scratch.

Ninguna demo debe hacer asignaciones dinamicas durante el bucle principal salvo que
la fase lo declare explicitamente como una prueba de fallo.
