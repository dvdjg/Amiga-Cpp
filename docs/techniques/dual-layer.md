# Dual layer graphics

- **Referencia:** [Dual Layer Graphics](https://www.powerprograms.nl/amiga/dual-layer.html)
- **Idea:** Dos capas visibles: típicamente **dual playfield** (fondo + frente con prioridad y scroll independiente en OCS/ECS dentro de límites), o composición con sprites, o mezcla con blitter.
- **Coste:** Dual playfield: hasta 3+3 planos en baja resolución estándar; consume más DMA y chip RAM que un playfield simple. Sprites: canales fijos y límites de ancho/posición.
- **Límites:** Resolución y número de colores por capa; en A500 el margen para juegos es ajustado frente a AGA (más colores y modos).
- **AHRM:** `BPLCON0` (`DBLPF`), `BPLCON2` prioridades; capítulo Playfield.
- **Lab:** Implementar segundo playfield en un efecto dedicado o extender el engine (`engine_copper_*`, planos); validar con captura MCP.
