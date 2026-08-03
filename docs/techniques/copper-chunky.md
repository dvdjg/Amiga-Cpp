# Copper chunky

- **Referencia:** [Copper Chunky](https://www.powerprograms.nl/amiga/copper-chunky.html)
- **Idea:** Cambiar `COLORxx` u otros registros por línea o grupo de líneas para simular píxeles “chunky” o modos pseudo–alta profundidad sin un framebuffer chunky real.
- **Coste:** CPU para generar/actualizar la lista copper; en ejecución, el copper consume su tiempo de coprocesador; muchos WAIT/MOVE por frame pueden limitar complejidad horizontal.
- **Límites:** Resolución efectiva limitada (p. ej. bloques 8×4 clásicos); competición con blitter y bitplanes en la misma línea.
- **AHRM:** Capítulo Coprocessor (Copper); `COP1LC`, `COP2LC`, `COPJMP`.
- **Lab:** Lista copper estática densa + medición de líneas con `winuae_copper_disassemble`.
