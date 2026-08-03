# Modulo tricks (bitplanes)

- **Referencia tutorial:** [Modulo Tricks (powerprograms.nl)](https://www.powerprograms.nl/amiga/modulo-tricks.html)
- **Idea:** `BPL1MOD` y `BPL2MOD` alteran el incremento de puntero entre líneas de cada grupo de bitplanes; permite saltar memoria, ventanas, efectos de “skew” o límites de anchura sin recalcular todos los punteros a mano cada línea.
- **Coste:** Bajo en CPU si la lista copper o el setup por frame ya programa los registros; el DMA consume los mismos ciclos de lectura de bitplanes que el modo elegido.
- **Límites:** Debe cuadrar con `DIWSTRT`/`DIWSTOP`, `DDFSTRT`/`DDFSTOP` y número de planos; errores dan garbage o líneas corruptas. Dual playfield reparte planos entre campos pares/impares (ver AHRM Playfield).
- **AHRM:** Capítulo 3 Playfield; registros `BPL1MOD`, `BPL2MOD` — índice [amiga-hardware-manual-index.md](../amiga-hardware-manual-index.md).
- **Lab en repo:** Technique lab muestra valores actuales de `BPL1MOD`/`BPL2MOD` en overlay; amplía el efecto para animar módulos y validar en MCP con `winuae_custom_registers`.
