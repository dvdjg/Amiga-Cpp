# Free-form sprite layer

- **Referencia:** [Free Form Sprite Layer](https://www.powerprograms.nl/amiga/spr-layer.html)
- **Idea:** Componer una capa tipo “sprites” o fondo sin patrones repetidos usando hardware sprites, attach, prioridad con playfields y a veces copper para reutilizar canales.
- **Coste:** Sprites son DMA fijos; más de 8 aparentes implica multiplexación en el tiempo (copper + posición por línea).
- **Límites:** Ancho 16 píxeles (o 64 con attach), colores por sprite limitados; prioridad vs bitplanes en `BPLCON2`.
- **AHRM:** Capítulo Sprite.
- **Lab:** Escena mínima con 2–3 sprites y fondo dual playfield; capturar con MCP.
