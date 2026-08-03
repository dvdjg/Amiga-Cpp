# Dual playfield fast BOBs

- **Referencia:** [Dual Playfield ‘Fast Bobs’](https://www.powerprograms.nl/amiga/dpl-fastbobs.html)
- **Idea:** Colocar BOBs en el playfield de frente con máscaras/minterms mientras el fondo es el segundo playfield, reduciendo restauración del fondo en algunos diseños.
- **Coste:** Blitter + DMA de planos; depende del tamaño del BOB y del número de planos activos.
- **Límites:** Prioridades y colores por capa; clipping y scroll deben ser coherentes entre capas.
- **AHRM:** Blitter + Playfield dual.
- **Lab:** El demo `demo_scroll_bobs` es la base; variantes con `DBLPF` explícito y comparación de ciclos documentada en este repo.
