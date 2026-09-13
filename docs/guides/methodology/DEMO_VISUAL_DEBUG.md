# Diseño de demos y depuración visual (lecciones)

Reglas prácticas para que una demo **muestre claramente** lo que demuestra y para **cazarlo**
cuando no lo hace. Nace de la demo 112 (RoboCod): el efecto parecía funcionar en una captura
y estaba roto en plena animación (el fondo desaparecía al scrollear).

## 1. Cómo plantear una demo para que se entienda

- **Muestra TODAS las características de golpe y sin ambigüedad.** Si la demo es "fondo que
  scrollea lento tras un primer plano", se deben ver **las dos capas** y **el movimiento
  relativo** entre ellas en cualquier instante.
- **Separa con contraste.** Capas con **colores contrastados** (p. ej. FG cálido sobre BG frío)
  y **cobertura desigual** para que se distingan. Un patrón uniforme/busy oculta la frontera.
- **Usa áreas amplias para el efecto protagonista.** Si la técnica es el fondo, deja que **domine**
  la imagen (como el RoboCod original: zonas grandes del plano de fondo). Si el FG tapa el 90%,
  el fondo no se aprecia.
- **Anima en los ejes que importan.** "Área mayor que la pantalla" ⇒ **movimiento en ese eje**
  (o rebote) para que se note; un scroll en un solo eje no lo demuestra.
- **Objetos reconocibles.** Mejor un motivo/forma legible (plataformas, un personaje) que un
  ruido de píxeles: comunica la capacidad del chipset de un vistazo.
- **Lo especial, visible.** Si hay un truco (parallax, bitplane extra, raster colors), que se vea
  **sin explicación**; si solo se ve en el código, la demo falla.

## 2. Cómo depurar a nivel visual (no basta una captura)

- **Captura una SECUENCIA, no un frame.** Muchos fallos (desaparición de contenido, costuras,
  cortes al envolver) solo ocurren en **ciertas fases** de la animación. Una captura suelta
  puede caer en un frame "bueno" y dar falso OK.
  - `run-demo.sh <demo> --sequence-frames N --sequence-interval-ms M`
  - Mira **frames tardíos** (rebotes, extremos del scroll), no solo el primero.
- **Inspecciona los frames tú mismo**, además de con visión artificial. La IA de visión
  **alucina y es amable**: con una pregunta genérica dirá "limpio y geométrico" aunque haya
  defectos.
- **Pregunta crítica y específica.** Pregunta por **defectos concretos** y por su **zona**
  ("¿hay huecos negros abajo? ¿tiles cortados? ¿costuras? ¿capas incoherentes? di el frame y la
  zona"). Comparar **frames consecutivos** ayuda ("¿qué cambia entre el 2 y el 3?").
  - `node tools/analyze/ollama-desc.mjs <dir_seq> <idx0,idx1,...> "<pregunta>"`
- **Histograma de color** para cazar paletas erróneas: `node out/tmp/fullcheck.mjs <png>`.
  Ejemplo real: un **teal `#007777`** delató que el "oscurecido" del grid no preservaba el tono.
- **Análisis de movimiento**: `analyze_frame_sequence.js --expect-animated` (la secuencia debe
  cambiar) y un `analyze-sequence.sh` propio que además compruebe **telemetría** (p. ej. la
  cámara avanza) leyendo `g_eng_run_status.detail`.
- **Analizador de captura propio** (`analyze-screenshot.sh` de la demo) cuando la paleta no
  encaja con el genérico (que exige overlay verde/amarillo/blanco).

## 3. Fallos típicos a buscar (checklist)

- **Huecos negros** donde debería haber contenido (fondo que "desaparece" en una zona/altura).
- **Contenido que solo está arriba/abajo** (indicio de que el ring/bitmap no cubre el rango).
- **Costuras o tiles cortados** al envolver (X/Y) o en los rebotes.
- **Incoherencia entre capas** (se mueven igual cuando deberían ir a distinta velocidad; o una
  se "arrastra").
- **Colores que cambian de tono** (paleta mal mapeada; contrastes que se pierden).
- **Artefactos del Blitter** en zona visible (contenido que no debería estar).
- **Frame estático** (la animación no avanza) o **parpadeo**.

## 4. Lección de la 112 (caso real)

- **Síntoma**: la captura inicial se veía bien; en un **frame tardío (011)** el **fondo
  desaparecía en la mitad inferior** (negro) al scrollear en Y.
- **Causa**: single-playfield de 5 planos: el **blit interleaved del FG escribe el plano de
  fondo a 0** y lo borra en las filas entrantes (el Blitter no salta un plano intermedio).
- **Fix correcto**: emitir el blit del FG **por-planos** (`bitplane_count = planes-1`), de modo
  que cubra solo los 4 planos del FG y **no pise el 5.º** (el plano RoboCod). Así se tiene el
  RoboCod **fiel de 5 planos** (ver `docs/reference/amiga/techniques/robocod-layered-scroll.md`).
  (Alternativa: DPF de 2 capas con bitmaps separados.)
- **Cómo se cazó**: secuencia + frame tardío (no la captura inicial) + pregunta crítica a
  Ollama + histograma de color. La pregunta genérica de Ollama NO lo detectó.
