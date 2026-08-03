# CPU assisted blitting

- **Referencia:** [CPU Assisted Blitting](https://www.powerprograms.nl/amiga/cpu-blit-assist.html)
- **Idea:** Mientras el blitter trabaja en una zona, la CPU procesa otra (p. ej. otra banda del bitmap) para mejorar throughput en A1200+ con más ancho de banda y 68020.
- **Coste:** Requiere particionar el trabajo y respetar `DMACON`/`BLTCON` y esperas (`blitter busy`); más complejidad de código.
- **Límites:** En A500 el cuello de botella suele ser distinto; medir antes de asumir ganancia. No pisar memoria que el blitter aún lee/escribe.
- **AHRM:** Capítulo Blitter; `DMACONR` (`BBUSY`).
- **Lab:** Par de rectángulos: un blit largo + CPU rellenando otra región; perfilar con frame profiler si está disponible.
