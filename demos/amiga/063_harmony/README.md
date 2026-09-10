# Demo 063: harmony — armonía reconocible (3 canales de música + SFX mixer)

Toca el "Himno a la Alegría" (Beethoven) a 3 voces por los canales de música
(AUD1=melodía, AUD2=terceras, AUD3=bajo) mientras el SFX mixer (AUD0) reproduce
un bajo continuo en bucle. Demuestra los 4 canales de Paula a la vez con notas
reconocibles.

La muestra es una onda cuadrada de 32 muestras (bucle), así los períodos
Protracker (C-2=428..B-2=226) suenan en ~C4..B4 (261..495 Hz). El bajo del mixer
usa una onda cuadrada preprocesada (±24) más larga para sonar grave.

```bash
tools/build/build-demo.sh demos/amiga/063_harmony --clean
tools/run/run-demo.sh       demos/amiga/063_harmony
```

Evidencia: `mark_ready` se emite cuando `DMACONR` muestra los 4 canales activos
(`AUD0`+`AUD1`+`AUD2`+`AUD3` = `0x38f`).
