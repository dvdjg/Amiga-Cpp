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

