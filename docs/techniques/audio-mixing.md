# Audio mixing for games

- **Referencia:** [Audio Mixing for Games](https://www.powerprograms.nl/amiga/audio-mixing.html)
- **Idea:** Mezclar voces o efectos en un buffer (CPU o copper/DMA creative) y enviar a los canales Paula con periodo/volumen adecuados para mantener coste bajo por frame.
- **Coste:** CPU proporcional a muestras mezcladas; cuatro canales hardware sin mezcla interna → software mixing o priorización de voces.
- **Límites:** 8 bits por canal, volumen y frecuencia discretos; calidad vs tiempo real en 7–50 kHz efectivos según configuración.
- **AHRM:** Capítulo Audio; `AUDxLCH/LCL`, `AUDxLEN`, `AUDxPER`, `AUDxVOL`.
- **Lab:** El demo principal ya usa player P61; para mezcla genérica, añadir efecto mínimo con dos sonidos software-mezclados y medir uso de CPU (overlay frame counter + VBL).
