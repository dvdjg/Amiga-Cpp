# Invariantes Hardware Y Microtests Recomendados

Plan para acumular conocimiento low-level en forma de microtests pequenos,
binarios y faciles de depurar. La idea es no depender solo de efectos visuales
compuestos para demostrar que el engine entiende el hardware.

## Objetivo

Complementar los casos visuales grandes con pruebas de invariantes como:

- residencia CHIP,
- orden de activacion del copper,
- `WAIT` seguro,
- relacion entre `BPLCON1`, punteros y modulos,
- y separacion entre "cola para el siguiente frame" y "reinicio inmediato".

## Principio

Un microtest no intenta demostrar "una demo bonita".
Intenta demostrar una verdad concreta del hardware.

## Microtests prioritarios

### MI01 - DMA requiere CHIP

Prueba:

- mismo bitmap en RAM normal y en CHIP;
- evidencia de que solo la version en CHIP es valida para bitplanes DMA.

Invariante:

- `BPLxPT` fuera de CHIP => caso no aceptable.

### MI02 - COP1LC sin COPJMP1

Prueba:

- dos copperlists alternas muy visibles;
- actualizar `COP1LC` una vez por frame sin reinicio inmediato.

Invariante:

- la nueva lista entra en el siguiente frame sin franjas mid-frame.

### MI03 - COPJMP1 mid-frame rompe raster

Prueba:

- misma base que `MI02`, pero forzando `COPJMP1` durante barrido.

Invariante:

- el test debe mostrar o registrar la corrupcion esperada y documentarla.

Valor:

- convierte un bug historico en una leccion objetiva del engine.

### MI04 - WAIT seguro >255

Prueba:

- cambios visibles por copper por encima de linea 255;
- comparar helper simplificado frente a helper seguro.

Invariante:

- sin manejo de overflow vertical, los cambios caen en linea incorrecta.

### MI05 - Scroll fino vs coarse

Prueba:

- playfield ancho con landmarks;
- avanzar scroll fino y coarse por separado.

Invariante:

- `BPLCON1` solo no resuelve todo el scroll;
- punteros coarse deben avanzar cuando corresponde.

### MI06 - Wrap por BPL1MOD/BPL2MOD

Prueba:

- bitmap alto con marcas horizontales;
- aplicar wrap solo con modulo dinamico.

Invariante:

- el cambio de modulo ocurre en la linea correcta y no genera franja.

### MI07 - Reutilizacion de sprites hardware

Prueba:

- un sprite hardware reprogramado varias veces en el mismo frame;
- varias instancias visibles sin tocar bitplanes de fondo.

Invariante:

- el hardware puede reutilizarse durante el frame si el tiempo y el script del
  copper lo permiten.

### MI08 - Copper reprograma sprites

Prueba:

- cambiar `SPRxPOS`, `SPRxCTL` o puntero de sprite durante scanout.

Invariante:

- la IA debe entender que el recurso no es "un sprite fijo", sino una ventana
  temporal de hardware reutilizable.

## Criterio de diseno

Cada microtest deberia declarar:

- init,
- por frame,
- scanout,
- DMA,
- evidencia minima,
- invariante exacta.

## Uso futuro

Estos microtests deben servir como base para:

- efectos importados desde `demoscene-repo`,
- refactors del engine,
- y futuros juegos complejos como shooters con dual playfield y copper
  dinamico.
