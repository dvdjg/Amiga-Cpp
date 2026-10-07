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
- **Objetos singulares para ver la evolución temporal.** Para validar **movimiento**, incluye
  **objetos de color único** (no un patrón uniforme/periódico) cuya trayectoria se siga frame a
  frame (a ojo y con el modelo de visión). Un patrón uniforme o de alta frecuencia da resultados
  **ambiguos**: no permite decidir si el algoritmo está bien. Además, un **marcador de color
  irrepetible** permite medir el paso exacto de forma determinista
  (`tools/vision-review/motion-check.py --track R,G,B`). Ejemplo: la demo 128 con bloques de
  colores distintos sobre fondo tenue.
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
  - **Elige los instantes: cubre las transiciones internas.** Una rejilla uniforme puede caer siempre en la fase «cómoda». Captura además los puntos de cambio del efecto (primer frame tras el arranque, rebote/extremo del recorrido, envoltura o cruce del bucle, cambio de frame/paleta/modo, final de cada fase) y anota qué instante representa cada captura: la continuidad se rompe ahí. Si el runner no permite fijar el instante, añade una variable/fase de depuración o repite capturas hasta cubrirlo.
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
- **Objetivos de rendimiento**: las demos Amiga deben completar **un frame de juego por cada VBlank PAL** (frecuencia nominal 50 Hz; WinUAE suele medir ~49,9–50,0 fps); las demos cuyo efecto principal sea relleno de polígonos deben alcanzar al menos **25 fps**. Medir en `A500_debug` con `tools/debug/measure-fps.mjs` y contrastar ciclos de CPU por frame con el control de VBlank. Si hay pérdida real de campo, perfilar con `tools/debug/profile.mjs`/`tools/debug/winuae-profile.mjs` y optimizar el coste; no cambiar el ritmo del runner ni omitir trabajo visual para inflar la cifra.
- **Sin tirones y con CPU baja (obligatorio)**: verificar **siempre** que la animación mantiene **cadencia uniforme** (ningún frame que ocupe 2 VBlanks, sin picos de ciclos por frame) y que el **trabajo por frame es escaso** (CPU holgada, sin hotspots puntuales). Un **tirón** o un **pico de procesamiento** en ciertos momentos es señal de que **la técnica puede no ser válida**: perfilar ese instante (`.amigaprofile` + IA local) antes de dar la demo por buena y, si el pico es intrínseco al algoritmo, replantear la técnica en vez de maquillar la medida.
- **Animación obligatoria y fluida**: movimiento continuo dentro del presupuesto de FPS, sin tearing ni flicker. La suavidad forma parte de la demostración. **Nunca** una demo de efecto por píxel puede quedarse en un único frame convertido en `init`.
- **Color y contexto**: paletas ricas (degradados reales, EHB, transparencias) y un fondo con contexto; no un par de colores planos ni una rampa de grises «de test».
- **Legibilidad**: debe verse de un vistazo qué efecto se está implementando (plasma, fuego, rotozoom, túnel, scroll, etc.) y, si procede, un rótulo/texto que lo nombre.
- Estas condiciones **sustituyen** al antiguo criterio de aceptación «compila, llega a Ready y el pixel-assert pasa»: ese gate sigue siendo necesario, pero **no suficiente**.

### 6.2 Validación visual con visión local

- **Ninguna demo o efecto se da por terminado sin pasar Ollama con modelo de visión** (`qwen3-vl:8b-instruct-q8_0`). Los gates automáticos (cobertura, nº de tonos, rampa, `analyze-demo`) **no ven glitches**: costuras, saltos de 16 px, filas duplicadas, planos descolocados, parpadeos o geometrías deformes pasan como «OK». El feedback humano llega tarde o no llega.
- **Validar secuencias, no solo una imagen**: analizar **varios frames** del efecto (`tools/profile/ai-analyze.mjs <out> <n> --demo <demo> --prompt "..."`), porque hay fallos que solo aparecen en movimiento (cruces de tile, flip de buffer, tearing, parpadeo entre frames). Una sola captura **no** es evidencia suficiente.
- **Análisis por elementos y evolución temporal (demos con carga gráfica).** Con capturas repartidas por la animación y que cubran sus **transiciones internas** (§2), pide al modelo que analice **cada elemento** de la escena: forma, composición, tamaño y color, y cómo evoluciona **a lo largo del tiempo** —si cambia de posición, tamaño, forma o color—, su **relación con los demás** (orden, solapes, capas) y si su **trayectoria es coherente** o muestra **glitches o parpadeos**. Pregunta explícitamente por el elemento esperado y su cuenta (p. ej. «¿cuántas chispas hay, de qué colores, y alguna está quieta o parpadea?»); una descripción genérica no es veredicto. Herramienta: `node tools/analyze/ollama-desc.mjs <dir_seq> <idx0,idx1,…> "<pregunta>"` sobre la secuencia de `run-demo.sh` (o `tools/profile/ai-analyze.mjs --mode montage`). Como todo veredicto de visión, es **filtro de sospecha**: confírmalo con un gate objetivo (MD5/diff de frames, frame-scope, pixel-contract) y nunca pidas coordenadas en píxeles.
- Las demos estables declaran `flicker-baseline.json` con `max_candidates: 0` y `max_blocks_low: 0`; el gate es `tools/test-regression.sh --flicker --require-flicker-ok`. FPS, flicker y contrato de movimiento son criterios independientes.
- El prompt debe pedir explícitamente **anomalías** y lo que **se pretendía** ver (`--prompt "scroll horizontal fino; ¿hay saltos de 16 px o costuras entre tiles?"`), no una descripción genérica.
- Guardar el veredicto como evidencia (salida del informe) y, si hay glitch, **no dar la demo por hecha**: anotarlo y arreglarlo o marcarla como pendiente.
- **El veredicto de visión es un filtro de sospecha, no una prueba**: puede sobre-reportar en texturas de alta frecuencia (caso real: `qwen3-vl` acusó «permutación de planos» en la 061 y un gate objetivo de 7 estados —rotación/zoom/paneo— la descartó) y también **dar falsos negativos** (dijo «los frames son idénticos, no hay movimiento» en la 083, cuyos 3 frames tenían MD5 distintos). Toda anomalía señalada **y toda afirmación de «no se mueve»** se confirma o refuta con un gate objetivo (gate de estados, diff de frames/MD5, comparación por fase); si el gate no cubre ese estado (p. ej. la 061 solo comparaba la identidad), **ampliarlo** antes de dar nada por bueno. Ojo también con el **ritmo de captura**: si la demo va a menos fps que el intervalo de captura, los frames salen iguales por muestreo, no por falta de animación (083: 2,3 fps → capturar cada 1,5 s).
- **Un aviso de visión sobre el CONTENIDO es bloqueante** (falta un elemento, algo se corrompe o parpadea): se confirma o refuta **mirando los frames y con conteo objetivo** (color/región, `band-diff`, MD5/diff) — jamás con un gate de movimiento/flicker, que miden cambio, no corrección. Protocolo de prompts: §6.4.
- Herramientas: `tools/profile/ai-analyze.mjs` (`--mode frames|montage|all`), `tools/analyze/verify-scroll-directions.mjs`, `tools/amiga-tiles/run-vision-verify.mjs`. Ollama en `127.0.0.1:11434`; alternativa por MCP: `winuae_profile_ollama`. Nota: el camino `--demo` de `ai-analyze.mjs` no levanta el canal lateral; hoy lo fiable es `run-demo.sh <demo> --sequence-frames N` (deja `out/run/<demo>/<cfg>/sequence/`) y analizar esos frames con el modelo de visión.

### 6.3 Validación de optimizaciones de render

- Una optimización que toque el **render** (registros/blits/orden de operaciones) **NO se da por buena con `verify-*` de cobertura/tonos**: hay que validarla **visual o estructuralmente** contra la referencia.
- Gate mínimo con el emulador: capturar una **secuencia** y compararla con el original por **fase** (mejor IoU + MAD de color; ver `tools/analyze/bestphase.mjs` o `tools/analyze/phasecmp.mjs`) o pedir una descripción a Ollama preguntando explícitamente por **anomalías** (caras deformes, aristas que no cierran). `verify-116` pasó con el sólido deformado: cobertura y nº de tonos no bastan.
- Ejecutar el gate **después de cada** cambio de render y **revertir** si empeora, aunque el cambio parezca inocuo (p. ej. fijar los comunes del Blitter 1×/frame rompió flatshade-convex).

### 6.4 Protocolo de interrogatorio al modelo de visión (prompts exactos)

Herramienta: `node tools/analyze/ollama-desc.mjs <dir_seq> <idx0,idx1,...> "<prompt>"` (modelo `qwen3-vl:8b-instruct-q8_0`, Ollama local). Estructura en **tres pases, en este orden**:

1. **A · Inventario SIN contexto** (el modelo no sabe qué pretende la demo; no condicionar).
2. **B · Dinámica/seguimiento** de un elemento concreto del inventario.
3. **C · Verificación dirigida** con la intención y los elementos esperados.

Reglas del interrogatorio:

- Si A y C se contradicen, **manda A** (C puede inducir complacencia).
- **Una imagen por llamada** o **hoja de contacto etiquetada** (una sola imagen con paneles rotulados `FRAME_nnn.PNG`). El modelo **no atiende de forma fiable varias imágenes en un mensaje**: en la 110, con 4 imágenes declaró «ambos frames» y con 2 declaró «un único frame» (evidencia en su `VALIDATION.md`). El montaje etiquetado sí se compara panel a panel, pero **pierde resolución**: úsalo para inventario/presencia de elementos **grandes** (en la 085 el disco desapareció en la hoja; en la 110 sirvió para el inventario); para detalle, **una imagen por llamada**.
- **Prohibido pedir coordenadas en píxeles** (alucina columnas: respondió «columna 14/15»); zona relativa: arriba/centro/abajo, izquierda/centro/derecha.
- Guardar la **respuesta cruda** como evidencia junto a los frames (y en el `VALIDATION.md` de la demo).
- **Un aviso de contenido es bloqueante**: el agente abre los frames y aporta conteo objetivo (por color/región, `tools/analyze/check-elements.mjs`, bbox/traslación) antes de concluir. Los gates de movimiento/flicker **no** refutan contenido.
- Límites medidos (sesiones 085/110): detecta parpadeo/pérdida de un elemento cuando se pregunta por anomalías concretas (dijo «parpadeo», «zonas corrompidas» en 110 y era cierto); describe elementos y su presencia si se le pregunta por elementos; **falsos negativos en movimiento lento y patrones periódicos** (dijo «nave estática» y «fondo fijo» cuando la bbox y la traslación demuestran que se mueven); sobre-reporta en texturas de alta frecuencia; no sustituye la mirada del agente. **Falso negativo con bandas alternas (110):** ante una hoja de 100 frames con una banda horizontal negra en uno de cada dos frames, respondió «sin parpadeo» — la cazó el agente al mirar la hoja; con patrones periódicos, **el agente mira siempre la hoja** antes de aceptar el «no veo nada raro».

**Prompt A (rellenar N):**
```text
Vas a analizar N capturas consecutivas (frames 0..N-1) de la misma animación. No conoces el programa ni lo que pretende mostrar; describe SOLO lo que observas.
1) Inventario: enumera los elementos distintos que ves (forma, color, tamaño relativo) y cuántos hay de cada tipo.
2) Dinámica: para cada elemento, ¿cambia de posición, tamaño, forma o color entre frames? ¿cómo se relaciona con los demás (orden, solapes, capas)?
3) ¿Algún elemento aparece o desaparece en algún frame? ¿alguno parpadea (está en unos frames y en otros no)?
4) ¿Ves discontinuidades, cortes, zonas incoherentes o "basura" (ruido, bloques, texto raro)? Indica zona relativa (arriba/centro/abajo, izquierda/centro/derecha) y en qué frames.
5) ¿El conjunto se mueve de forma coherente? ¿hay algo que debería moverse y no se mueve?
Responde en español con observaciones concretas. No inventes coordenadas en píxeles.
```

**Prompt B (rellenar {ELEMENTO} y {LO QUE DEBERÍA HACER}):**
```text
Céntrate en "{ELEMENTO}". Sigue su evolución frame a frame: posición (zona relativa), tamaño y aspecto; di si su trayectoria es suave y continua o si da saltos, se para o desaparece. ¿Es coherente con "{LO QUE DEBERÍA HACER}"?
Responde en español.
```

**Prompt C (rellenar {INTENCIÓN} y {LISTA DE ELEMENTOS}):**
```text
Esta animación PRETENDE mostrar: {INTENCIÓN}. Los elementos esperados son: {LISTA: p. ej. fondo de N filas de tiles que scrollea hacia arriba; 1 nave abajo; balas blancas subiendo; 1 torreta verde que apunta a la nave}.
Para CADA elemento esperado indica en cada frame: presente / ausente / a medias, y si su posición y forma son coherentes. Después responde: ¿el movimiento es continuo o hay saltos o costuras? ¿algo parpadea o se corrompe? Señala zona relativa y frames. No des por bueno nada; si algo es ambiguo, dilo.
Responde en español.
```

La fase C no sustituye a A/B: es la comprobación contra la intención, no la fuente de la verdad.

**Informe por demo.** Cada demo mantiene un `VALIDATION.md` en su carpeta con: prompts exactos enviados (A/B/C), **respuestas crudas** del modelo, medidas objetivas (fps/ciclos, `tools/analyze/check-elements.mjs`, band-diff) y conclusiones. Es la evidencia de F4/F5 del `PROCEDIMIENTO_DEMOS_Y_JUEGOS.md` y se actualiza en cada pasada que cambie el render o el coste.

### 6.4.1 Secuencias temporales con ventanas deslizantes (evaluación del modelo)

Evaluación de `qwen3-vl:8b-instruct-q8_0` (2026-10, demo 110) pasando **ventanas de 12 frames consecutivos** (misma resolución) en **una sola llamada** con la leyenda de orden temporal de `vision-run.mjs` y un prompt pixel-a-pixel (plantilla abajo). Resultados medidos:

| Prueba | Respuesta del modelo | Veredicto |
|---|---|---|
| Ventana con vaivén X (f0010–f0021) | Evolución **frame a frame con etiquetas** (nave x=38→46, proyectil y=57→46, torreta fija), «sin glitches ni artefactos» | **Sí trata la secuencia como temporal**; útil para continuidad y evolución |
| Ventana con reversión X (f0030–f0041) | Frame a frame, sin discontinuidades | No detectó nada en la reversión (no había fallo) |
| Ventana tras el reinicio del mundo (f0016–f0027) | «El fondo cambia abruptamente en f0018… reemplazo total» | **Detecta transiciones bruscas** (el teleport del reinicio, real) |
| Ventana tardía (f0070–f0081) | Frame a frame | — |

Límites confirmados con este prompt: **direcciones/coordenadas no fiables** (dijo «derecha» con la nave moviéndose a la izquierda; el §6.4 prohíbe pedir píxeles), **falso negativo con fondo periódico** («completamente estático» con scroll de 2 px/frame) y **no cazó la banda alterna** de la 110 sin este prompt (la cazó el agente en la hoja). Conclusión operativa: las ventanas de 12-24 frames sirven para **continuidad, aparición/desaparición y transiciones**, no para dirección ni para movimiento periódico lento; el agente mira siempre la hoja de contacto además de la respuesta.

Plantilla del prompt (usada y verificada):

```text
Analiza esta secuencia de frames consecutivos (N imágenes en orden temporal, misma resolución) de un juego de arcade retro pixel-art. Compara frame a frame de forma precisa: 1) Detecta cualquier parpadeo, cambio de un solo píxel, sprite que aparece/desaparece incorrectamente o cambia de forma anómala. 2) Señala glitches, discontinuidades en animaciones, artefactos de captura o emulación. 3) Describe la evolución exacta de cada entidad (nave, proyectiles, torreta, fondo) indicando en qué frames ocurre cada cambio. Sé extremadamente literal y pixel-oriented. Si algo parpadea o tiene un fallo de 1-2 frames, indícalo claramente. Responde en español.
```

Protocolo recomendado: **ventanas deslizantes** de 12-24 frames consecutivos (avance ~50% del tamaño), `--prompt` de arriba; la hoja de contacto del mismo tramo la revisa el agente; los hallazgos van al `<demoId>_report.md` (§6.5).

**Comparativa de modelos (misma ventana, f0024–f0035):** `qwen3-vl:8b-instruct-q8_0` da evolución frame a frame y señala cambios concretos (p. ej. el detalle amarillo de la torreta que cambia de forma); `gemma3:12b` es más verboso pero **inventa movimientos** (dice que la torreta se desplaza, siendo estática) y sus «anomalías» son vagas («posible variación de color… difícil determinar»). **Preferido: qwen3-vl**; gemma3 queda como segunda opinión. Además: para el flicker 1-de-cada-2 frames (bandas alternas) existe el check objetivo `node tools/analyze/check-alternating-bands.mjs <dirSeq>` (por bandas de 16 filas y alternancias de luminancia), que no depende del modelo.

### 6.5 Artefactos de una pasada de visión: nombres y ubicación

Toda pasada de visión deja **dos artefactos** con nombre canónico, **en la propia carpeta de la demo** (junto a su `src/`), ignorados por git (reglas `demos/**/vision/` y `demos/**/*_report.md` del `.gitignore`; **nunca** se copian a `docs/`):

| Artefacto | Ruta | Nombre |
|---|---|---|
| Capturas analizadas | `<demoDir>/vision/` | `<demoId>_fNNNN.png` (NNNN = frame real; si el índice de la secuencia no es el frame, `<demoId>_sNNN.png`) |
| Informe de ejecución | `<demoDir>/` | `<demoId>_report.md` — **idéntico al nombre del directorio de la demo** |

El informe (`<demoId>_report.md`) contiene: comando exacto, tramo de secuencia y frames analizados, medidas de la pasada, **prompts crudos y respuestas crudas** y la conclusión con el alcance probado y lo SIN VERIFICAR. Es el crudo de la pasada; el `VALIDATION.md` de la demo (commiteado) es el resumen canónico.

Herramienta: `node tools/analyze/vision-run.mjs <demoId> <dirSeq> <idx…> --prompt "…" [--prompt "…"] [--model …] [--demo-dir ruta] [--per-frame] [--conclusion "…"]`.

- **Modo por defecto: SECUENCIA** — todas las imágenes en **una sola llamada**, precedidas de la leyenda `imagen k = frame fNNNN` en orden temporal. Es el modo para preguntas de **movimiento, continuidad, saltos y parpadeo**; sin la leyenda el modelo tiende a describir «un único frame» o a confundir el orden.
- `--per-frame`: una imagen por llamada (detalle de una captura concreta; §6.4).
- El índice `idx` es el número de la secuencia (`frame_<idx>_*.png`); de ahí sale el frame real para el nombre.
