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

### 2.1 Parpadeo/glitch: enfoque híbrido (determinista + visión)

Los modelos de visión son **poco fiables** en glitches **temporales** (parpadeo de 1–2 frames,
tearing, corrupción de copperlist) y **alucinan** coordenadas si se les pide buscarlos a ciegas.
El flujo fiable es de **dos capas**:

1. **Determinista** (`tools/vision-review/temporal-detect.py`, OpenCV): diferencia + *optical flow*
   (Farneback) + análisis por bloques. Localiza candidatos (`flicker`/`tearing`/`corruption`) y
   **descarta el movimiento coherente** (objetos/scroll). Es la **referencia**.
2. **Visión** (`flicker-check.mjs`, Ollama): solo sobre la **región candidata** con frames de
   referencia+contexto, con respuesta **estructurada** (sí/no + tipo + **zona relativa**, sin
   píxeles, + confianza). El modelo confirma/descarta una sospecha ya localizada.

Además, el **frame-diff determinista** (`out/tmp/framediff.cjs <seq>`) es la forma directa de
distinguir movimiento de glitch: cuenta píxeles cambiados y su bbox entre frames consecutivos. Si
una **zona estable** cambia erráticamente, es glitch; si solo cambian las zonas que se mueven, es
movimiento. Ante discrepancia con el modelo, prevalece el frame-diff. Modelo recomendado:
**Qwen3-VL** (evitar LLaVA clásico). Regla: nunca pedir coordenadas en píxeles al modelo.

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

## 5. Lección de la 116 (caso real): el `verify-*` puede dar PASS con la imagen rota

- **Síntoma**: el port asm de `flatshade-convex` daba `verify-116` **PASS**
  (`balon convexo flat-shaded (15 tonos, 512x498)`) pero la imagen era un amasijo de bandas
  y triángulos. Señal de alarma: el balón C++ usa **7-8 tonos** y el roto **15** (más tonos,
  no "mejor": son regiones incoherentes).
- **Por qué el verify no lo cazó**: comprobaba cobertura, bbox, ratio de aspecto, centroide y
  `tonos >= 4`. Un relleno XOR desmadrado mantiene esos números dentro de rango.
  **Cobertura y nº de tonos no miden la forma.**
- **Cómo se cazó**: (1) mirar la captura y compararla con la referencia; (2) **Ollama**
  pidiendo anomalías concretas (devolvió banda horizontal, contorno desalineado, triángulos
  sueltos); (3) wireframe (`-DFLATSHADE_SKIP_FILL=1`) **al mismo ángulo**: si el objeto gira,
  hay que **congelar el ángulo** antes de comparar, o cada captura cae en otra fase y el diff
  no significa nada.
- **Gate añadido**: `verify-116` ahora exige **silueta convexa** (salto máximo del borde
  izquierdo/derecho entre filas consecutivas `< 0.08 · ancho`): balón correcto ~14 px, roto
  ~378 px. Un objeto convexo no puede dar saltos de contorno grandes.
- **Regla**: ante cualquier cambio de **render**, no fiar el resultado a `verify-*`; pasar
  **Ollama** (preguntando por anomalías concretas) o comparar con una referencia por fase
  (IoU + MAD). Y si el objeto se mueve, comparar **al mismo ángulo**.

## 6. Reglas obligatorias (demos y validación visual)

Estas reglas son de obligado cumplimiento y `AGENTS.md` enruta aquí. Complementan las lecciones de las secciones anteriores.

### 6.1 Demo atractiva

- **Una demo no es un test.** Su objetivo es **entrar por los sentidos** y hacer evidente la capacidad que demuestra. Una demo nueva (o al reescribir una existente) debe cumplir estas condiciones; si no, no se considera terminada.
- **Requisito de técnica exclusiva**: debe mostrar algo que **solo se puede hacer con la técnica que implementa** y que **se vea de un vistazo** por qué esa técnica lo habilita (p. ej. chunky → efecto por píxel que en planar puro no podrías pagar; sprites → multiplexado; copper → split/rasters; blitter → rellenos y máscaras; EHB → degradados de 64 tonos). No vale un patrón plano ni un rectángulo de color sobre negro.
- **Animación obligatoria y fluida**: movimiento continuo (50 fps si el presupuesto lo permite; si no, la máxima tasa que se sostenga sin tearing perceptible), no una imagen estática. La suavidad forma parte de la demostración. **Nunca** una demo de efecto por píxel puede quedarse en un único frame convertido en `init`.
- **Color y contexto**: paletas ricas (degradados reales, EHB, transparencias) y un fondo con contexto; no un par de colores planos ni una rampa de grises «de test».
- **Legibilidad**: debe verse de un vistazo qué efecto se está implementando (plasma, fuego, rotozoom, túnel, scroll, etc.) y, si procede, un rótulo/texto que lo nombre.
- Estas condiciones **sustituyen** al antiguo criterio de aceptación «compila, llega a Ready y el pixel-assert pasa»: ese gate sigue siendo necesario, pero **no suficiente**.

### 6.2 Validación visual con visión local

- **Ninguna demo o efecto se da por terminado sin pasar Ollama con modelo de visión** (`qwen3-vl:8b-instruct-q8_0`). Los gates automáticos (cobertura, nº de tonos, rampa, `analyze-demo`) **no ven glitches**: costuras, saltos de 16 px, filas duplicadas, planos descolocados, parpadeos o geometrías deformes pasan como «OK». El feedback humano llega tarde o no llega.
- **Validar secuencias, no solo una imagen**: analizar **varios frames** del efecto (`tools/profile/ai-analyze.mjs <out> <n> --demo <demo> --prompt "..."`), porque hay fallos que solo aparecen en movimiento (cruces de tile, flip de buffer, tearing, parpadeo entre frames). Una sola captura **no** es evidencia suficiente.
- El prompt debe pedir explícitamente **anomalías** y lo que **se pretendía** ver (`--prompt "scroll horizontal fino; ¿hay saltos de 16 px o costuras entre tiles?"`), no una descripción genérica.
- Guardar el veredicto como evidencia (salida del informe) y, si hay glitch, **no dar la demo por hecha**: anotarlo y arreglarlo o marcarla como pendiente.
- **El veredicto de visión es un filtro de sospecha, no una prueba**: puede sobre-reportar en texturas de alta frecuencia (caso real: `qwen3-vl` acusó «permutación de planos» en la 061 y un gate objetivo de 7 estados —rotación/zoom/paneo— la descartó) y también **dar falsos negativos** (dijo «los frames son idénticos, no hay movimiento» en la 083, cuyos 3 frames tenían MD5 distintos). Toda anomalía señalada **y toda afirmación de «no se mueve»** se confirma o refuta con un gate objetivo (gate de estados, diff de frames/MD5, comparación por fase); si el gate no cubre ese estado (p. ej. la 061 solo comparaba la identidad), **ampliarlo** antes de dar nada por bueno. Ojo también con el **ritmo de captura**: si la demo va a menos fps que el intervalo de captura, los frames salen iguales por muestreo, no por falta de animación (083: 2,3 fps → capturar cada 1,5 s).
- Herramientas: `tools/profile/ai-analyze.mjs` (`--mode frames|montage|all`), `tools/analyze/verify-scroll-directions.mjs`, `tools/amiga-tiles/run-vision-verify.mjs`. Ollama en `127.0.0.1:11434`; alternativa por MCP: `winuae_profile_ollama`. Nota: el camino `--demo` de `ai-analyze.mjs` no levanta el canal lateral; hoy lo fiable es `run-demo.sh <demo> --sequence-frames N` (deja `out/run/<demo>/<cfg>/sequence/`) y analizar esos frames con el modelo de visión.

### 6.3 Validación de optimizaciones de render

- Una optimización que toque el **render** (registros/blits/orden de operaciones) **NO se da por buena con `verify-*` de cobertura/tonos**: hay que validarla **visual o estructuralmente** contra la referencia.
- Gate mínimo con el emulador: capturar una **secuencia** y compararla con el original por **fase** (mejor IoU + MAD de color; ver `tools/analyze/bestphase.mjs` o `tools/analyze/phasecmp.mjs`) o pedir una descripción a Ollama preguntando explícitamente por **anomalías** (caras deformes, aristas que no cierran). `verify-116` pasó con el sólido deformado: cobertura y nº de tonos no bastan.
- Ejecutar el gate **después de cada** cambio de render y **revertir** si empeora, aunque el cambio parezca inocuo (p. ej. fijar los comunes del Blitter 1×/frame rompió flatshade-convex).
