# Politica close-to-metal y ROM kernel

El engine se desarrollara principalmente close-to-metal, usando como referencia
el Amiga Hardware Reference Manual local:

```text
C:\Users\David\Documents\Programa\Amiga\Universal-Asset-Format\doc\amiga-manuals\books\hardware-reference-nvg\html\Amiga-Hardware-Reference-Manual.html
```

## Regla general

El hardware directo se usa cuando necesitamos control exacto de:

- Copper;
- Blitter;
- DMA;
- bitplanes;
- sprites hardware;
- audio DMA;
- VBlank/HBlank;
- prioridades y registros custom.

El ROM kernel se usa, opcionalmente, cuando aporta valor sin romper el objetivo:

- reservar/liberar memoria en modo OS-friendly;
- abrir/cerrar librerias;
- usar DOS durante herramientas o demos tempranas;
- restaurar el sistema al salir;
- compatibilidad con sesiones de debug y Workbench;
- prototipado antes del takeover completo.

## Modos previstos de backend

### OS-friendly

Modo actual de las primeras demos.

- Usa `AllocMem`/`FreeMem`.
- No toma completamente el sistema.
- Facilita debug, captura y pruebas.
- Menos control sobre la memoria completa.

### Mixed

Modo intermedio.

- Usa Exec para arranque, reserva y restauracion.
- Toma partes del hardware durante la ejecucion.
- Permite usar ROM kernel fuera de rutas criticas.

### Takeover

Modo final para demos/juegos exigentes.

- Desactiva el sistema cuando sea necesario.
- Programa custom chips directamente.
- Administra rangos de memoria conocidos.
- Requiere restauracion muy cuidadosa al salir.

## Toma de control del display (`takeover_display`)

La transicion de OS-friendly a takeover del video se concretiza en un solo punto
del backend: `MinimalBackend::takeover_display(const u16* copper_words)`
(`engine/src/platform/amiga_minimal/amiga_minimal.cpp`). Se invoca una sola vez
desde `init()` (o desde la primera composicion del driver), antes del bucle de
frames.

Que hace y por que es necesaria:

1. Escribe `INTENA=0x7FFF` e `INTREQ=0x7FFF`: apaga toda la cadena de
   interrupciones que AmigaDOS/Workbench dejo armada (exec/graphics/intuition
   tienen handlers de VBL, puertos y CIAA). A partir de aqui el engine no vuelve
   a llamar a exec; la sincronizacion se hace por espera activa sobre VPOSR.
2. Espera un Blitter en vuelo (`wait_blitter`) y escribe `DMACON=0x7FFF`: apaga
   TODO el DMA del sistema (sprites del puntero, disco, audio, blitter, bitplane
   y copper). Si no se apagaran los sprites, el canal de sprite del puntero del
   Workbench seguiria fetchando datos stale y dejaria barras verticales de colores
   de paleta a mitad de pantalla.
3. Programa COP1LC y espera el arranque de VBlank (linea 311 -> 0) para que el
   Copper no arranque a media pantalla.
4. Arranca master + copper y dispara COPJMP1 ya en la linea 0: el primer frame
   sale completo y limpio.

Los swaps por frame (doble buffer) NO repiten esta secuencia: usan
`install_copper_list`, que solo actualiza el puntero COP1LC. El Copper recarga
ese puntero por si solo al comienzo de cada VBlank, asi que nunca se debe disparar
COPJMP1 desde un swap (reiniciaria el Copper a media pantalla y produciria el
glitch de "banda de 1 frame" documentado en `docs/debugging/DEBUG_DEMO_ARRANQUE_DOBLE_TEXTO_BANDA.md`).

Los drivers y compositores exponen la misma division: `takeover(Backend&)` para
la primera instalacion y `install(Backend&)` para los swaps. `install_copper_list`
mantiene retrocompatibilidad: si se llama sin haber tomado el control, delega en
`takeover_display`.

## Estado actual de memoria

La gestion actual no administra toda la memoria fisica del Amiga. Funciona asi:

1. El backend pide bloques a Exec con `AllocMem`.
2. El engine administra esos bloques mediante `LinearArena`.
3. Las demos no hacen asignaciones sueltas durante el frame.

Esto es intencionado. Nos permite validar arquitectura, C++23, capturas y regresion
sin empezar por un takeover completo. Mas adelante se anadira una politica de memoria
capaz de construir arenas sobre rangos fisicos cuando el backend este en modo takeover.

## Reglas de implementacion

- Las cabeceras compartidas deben documentar intencion, coste y restricciones.
- Cada unidad nueva debe leerse como un tutorial pequeno.
- Si una funcion toca un registro custom, debe indicar que registro/area del hardware
  esta usando.
- Si una funcion usa ROM kernel, debe indicar por que es aceptable en esa fase.
- Ninguna abstraccion debe ocultar asignaciones, copias grandes o esperas de hardware.

