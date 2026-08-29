# Algoritmo de scroll DPF en OCS

## Restriccion fundamental

Un bitplane OCS no es una superficie circular horizontal. Durante el fetch,
Agnus incrementa `BPLxPT` word a word y al terminar cada scanline suma
`BPLxMOD`. El modulo es una constante: no permite que el fetch salte desde el
final de una fila al comienzo de esa misma fila.

Por tanto, usar `% framebuffer_tiles_x` al escribir un tile solo cambia la
direccion de escritura. No crea un anillo que Denise pueda leer. Si el puntero
de display llega al final de una fila, Denise seguira con la fila siguiente y
la imagen se corrompera.

La fuente primaria es el *Amiga Hardware Reference Manual*, capitulo
"Telling the System How to Fetch and Display Data": los punteros se
incrementan durante el fetch y `BPLxMOD` se suma al final de la linea.

## Convenciones DPF correctas

- PF1 usa BPL1, BPL3 y BPL5; su delay ocupa `BPLCON1[3:0]`.
- PF2 usa BPL2, BPL4 y BPL6; su delay ocupa `BPLCON1[7:4]`.
- Con PF1 en primer plano y PF2 de fondo:

```c
bplcon1 = (pf2_delay << 4) | pf1_delay;
```

- Para lowres con scroll horizontal se adelanta el fetch a `DDFSTRT=$30` y
  se mantienen `DDFSTOP=$d0` y 21 words por scanline: 42 bytes.
- En una superficie de 384 px, `bytes_per_row=48` y el modulo correcto es
  `48 - 42 = 6`, no 8.
- La formula de scroll fino/coarse usada por el driver es:

```c
fine = scroll_x & 15;
delay = (16 - fine) & 15;
fetch_x = (scroll_x - 1) & ~15;
```

Esto conserva `display_start == scroll_x` al pasar de fine 15 a 0.

## Variante lineal con realineamiento

Una superficie lineal de 384x288 puede avanzar sus punteros y recargar las
franjas que entran mientras queden bytes ocultos a la derecha o filas ocultas
abajo. Al agotar ese margen debe copiarse el area viva al inicio y resetear los
punteros.

Es una tecnica valida para mapas grandes, pero no es un anillo sin copias:
con una ventana de 320 px quedan solo 64 px horizontales. Un realineamiento
tipico copia unos 40 bytes por 288 lineas por plano. En DPF 3+3 son cerca de
69 KB movidos por Blitter por realineamiento. Tampoco se puede envolver la Y
solo cambiando el puntero: cuando la ventana cruza el final del bitmap hace
falta un split Copper o una copia equivalente.

Esta variante no es el objetivo de la demo 105 porque sustituye el shift de
104 por otra copia mas espaciada, pero aun grande.

## Arquitectura de 105: XY ilimitado sin copiar superficies

La demo 105 portara el modelo de `amiga-stuff/scrolling_tricks/xyunlimited2.c`:

1. Reservar un tilebuffer de 384x288 (24x18 tiles) en Chip RAM. El margen de
   64 px permite el fetch adelantado y el trabajo de prefetch.
2. Mantener una ventana lineal que avanza por el buffer mediante `BPLxPT` y
   `BPLCON1`; no desplazar los bitplanes.
3. Repartir durante los 16 pasos del tile el relleno de la fila o columna que
   entrara. Cada paso escribe uno o dos tiles que siguen fuera del viewport.
4. Al cruzar el final vertical, emitir un split Copper que reancla los
   `BPLxPT` al comienzo correcto del buffer antes de la siguiente linea visible.
5. Mantener una linea de cierre del layout lineal. Se copia solo esa scanline
   al cerrar el recorrido fisico; no se copian planos completos.
6. Para DPF 3+3, emitir un blit por tile y playfield: los tres planos de cada
   campo se fusionan usando el stride entre planos. PF1 y PF2 pueden tener
   camaras independientes.

El Copper necesita doble buffer para que una lista que esta siendo ejecutada
no se modifique. El commit alterna la lista en VBlank.

## Coste de memoria estimado

| Recurso | Tamano |
|---|---:|
| 6 bitplanes, 384x288 | 82.944 B |
| caches de 64 tiles por PF | 12.288 B |
| doble copperlist con split | ~6 KB |
| total aproximado | ~101 KB |

Esto cabe en los 120 KB de Chip RAM reservados por las demos sin scratch de
shift. El coste por frame queda acotado a tiles de prefetch y no contiene blits
de rectangulos de pantalla.
