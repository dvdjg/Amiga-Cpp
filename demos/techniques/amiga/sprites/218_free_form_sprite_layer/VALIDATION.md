# VALIDATION — 218_free_form_sprite_layer

Validación de la recreación frente al **ejecutable original** de la referencia
(`../spr_layer/Sprite_Layer/SPR_Layer`, Jeroen Knoester 2018), que se ejecuta en el mismo
entorno del runner (A500, KS 1.3, `out/run/spr_layer_ref/`).

## 1. Comparación estática (pantalla de título)

Ambos ejecutables, sin clic, capturados con el runner (`screenshot` interno de WinUAE, 756x576)
y comparados píxel a píxel en toda el área visible (x 110..690, y 30..516):

| Medición | Resultado |
|---|---|
| Píxeles distintos (umbral de color 60/765) | **0 / 281 880 (0.00 %)** |
| Barra del sub-buffer | **0.00 %** |
| Texto del título | **0.00 %** |
| Fondo (capa de sprites) | **0.00 %** |

La capa de sprites, el playfield (texto y paleta), la barra del sub-buffer y la paleta del
sub-buffer son **idénticos** al original en el estado inicial.

**Determinismo** (3 ejecuciones consecutivas del port y 2 del original): todas las capturas del
port dan **0.00 %** frente a la del original, y las dos del original **0.00 %** entre sí.

> **Hallazgo durante la validación**: sin congelar el SO antes del trabajo de `init`, la IRQ de
> VBlank de AmigaDOS podía reprogramar el Blitter entre dos blits de la capa y dejar **filas de
> tile a medias en las columnas Copper** (dos arranques del port diferían un 5.62 %, en bandas de
> 16 líneas). El original lo evita en su wrapper (`AllOff`: DMA e IRQ a cero antes de `_main`); el
> port lo reproduce aparcando el Copper con una lista mínima (`WAIT $ffff`) antes del init. Con
> el arreglo, los tres arranques son idénticos al original.

## 2. Comparación dinámica (efecto tras el clic)

Cada ejecutable se lanza con el mismo clic (`--mouse-click-at 5,5`) y una secuencia de 24
capturas a ~100 ms (real, sin warp). La **fase** no puede alinearse (el clic se inyecta tras el
READY del runner, que ocurre en instantes distintos en cada binario), así que se comparan
**trayectorias** y **cadencia**, no fotogramas homólogos.

Posición (capture) de las dos filas de BOBs por frame de secuencia:

| frame | original | port |
|---|---|---|
| 0 | (sin detección) | 291 |
| 3 | 211, 371 | 247, 335 |
| 6 | 123, 459 | 211, 371 |
| 9 | 159, 423 | (sin detección) |
| 12 | 243, 339 | (sin detección) |
| 15 | 247, 335 | 123, 459 |
| 18 | 163, 419 | (sin detección) |
| 21 | (sin detección) | 215, 367 |

Las **mismas parejas de posiciones** aparecen en ambos (247/335, 211/371, 123/459…), en el mismo
orden de la trayectoria de rebote, con un desfase de ~1 frame de secuencia (~0.2 s) atribuible al
instante del clic. (Las celdas «sin detección» son frames donde los BOBs se solapan con el texto
o la montaña y el detector de color verde falla; ocurre igual en los dos.)

Como el rebote avanza 2 px/frame y las posiciones medidas coinciden **en el mismo instante de
muestreo**, la **cadencia de frames del port es la misma que la del original** (1 frame por
VBlank; sin saltos de VBlank, que se verían como un avance más lento de la trayectoria).

## 3. Visión (Ollama local, `qwen3-vl:8b-instruct-q8_0`)

Herramienta: `node tools/analyze/ollama-desc.mjs <dir> 0,4,8,12,16,20 "<prompt>"`.

### 3.1 Pase A — inventario sin contexto, **port**

Respuesta cruda (recortada a lo esencial; el texto completo está en el registro de la sesión):

> **Inventario:** bloques de texto en naranja sobre azul; barra amarilla con texto negro «15
> colour FG / 4 colour BG / 50Hz»; múltiples sprites de forma circular con anillos concéntricos
> (borde azul, anillo rojo, anillo verde, centro blanco); fondo de montañas oscuras con picos
> blancos y cielo azul; nubes blancas arriba; en el frame 2 aparece un patrón de líneas
> ondulantes verdes y un bloque gris a la derecha.
> **Dinámica:** los sprites cambian de fila y cantidad aparente; el texto y el fondo permanecen;
> el elemento de la derecha aparece en el frame 2 y se desplaza en el 3.
> **Aparición/desaparición:** aparece el elemento de la derecha (patrón + bloque) en el frame 2;
> **no hay parpadeo**.
> **Discontinuidades:** el cambio de disposición de sprites entre el frame 1 y el 2 es brusco;
> el bloque gris de la esquina inferior derecha es nuevo; no hay cortes ni basura.

### 3.2 Pase A — inventario sin contexto, **original**

Respuesta cruda (lo esencial):

> **Inventario:** texto naranja sobre azul + barra amarilla «15 colour FG / 4 colour BG / 50Hz»;
> círculos concéntricos (dianas) con anillos de colores; montañas oscuras con cimas blancas;
> nubes; **estructuras verticales** (columnas con textura en espiral) a la derecha que aparecen
> gradualmente; bloques grises sólidos abajo a la derecha en el último frame.
> **Dinámica:** los círculos no cambian de posición/tamaño; el texto y el fondo permanecen; las
> estructuras verticales aparecen gradualmente.
> **Aparición:** las estructuras verticales aparecen en los frames 1-3; los bloques en el último;
> **no hay parpadeo**.
> **Discontinuidades:** texto parcialmente cortado; las estructuras superpuestas a los círculos
> pueden parecer incoherentes; sin cortes ni basura.

**Comparación:** los dos inventarios coinciden elemento a elemento (texto, barra, BOBs anillados,
montañas, nubes, la «estructura vertical» = la planta del fondo que entra con el scroll, y el
bloque gris del sub-buffer). Las «anomalías» que reporta el modelo (aparición de la planta, texto
cortado) aparecen **igual en el original**: son el scroll de la capa de sprites, no defectos.

### 3.3 Pase C — verificación dirigida, **port**

Prompt con la intención (fondo free-form, scroll 1 px/frame, 9 BOBs en dos filas que rebotan,
barra estática). Respuesta cruda:

> «La animación muestra un patrón de movimiento que no coincide con el efecto esperado… se
> observa un movimiento de desplazamiento horizontal hacia la derecha, con un ritmo constante.
> Los BOBs (círculos anaranjados) no se mueven verticalmente ni rebotan; permanecen en posiciones
> fijas mientras el fondo y el texto se desplazan. La barra amarilla permanece estática, lo cual
> es correcto.»

Interpretación (límites conocidos del modelo, documentados en
`DEMO_VISUAL_DEBUG.md` §6.4): acierta la presencia y el scroll del fondo/texto y la barra
estática; **falla la dirección** (la capa se desplaza a la izquierda) y **no ve el rebote** de los
BOBs por el submuestreo de frames (0,4,8,…) — el mismo falso negativo que produce en la secuencia
del **original** («los círculos no cambian de posición»). La medición objetiva de la sección 2
demuestra que el rebote existe y coincide con el original.

### 3.5 Pase de regresión del doble buffer y del merge de tiles

Prompt sobre el screenshot del build final (doble buffer + service-off + merge 28→14):

> «This is a frame of a demo with scrolling text (several lines of orange/white text) over
> mountains and 9 ring objects. Check CAREFULLY: (a) is any text line torn, split horizontally,
> shifted, duplicated or with garbled/missing glyph rows? (b) are all 9 rings complete circles,
> none cut? (c) do the text lines read as coherent sentences? Answer concisely per item.»

Respuesta cruda: `(a) No. (b) Yes. (c) Yes.`

El conteo de anillos con el detector de color del screenshot da **4 arriba + 5 abajo = 9**; en la
secuencia *step* (CPU detenida en el probe) los conteos varían (2-4 arriba) por el artefacto de
frontera de escaneo descrito en §5.5, también presente en el build base.

### 3.6 Detección con visión de la discontinuidad de 1-2 px del fondo (32 frames)

Síntoma real (reportado por el usuario): cada 32 updates el fondo de montañas da una costura de
1-2 px. El pase de visión genérico de §3.5 **no lo detectaba** (la escena tiene texto, BOBs y
objetos grandes que dominan la atención y el detalle de 1 px no sobrevive al reescalado del
modelo). Método que sí funciona (procedimiento general en `DEMO_VISUAL_DEBUG.md` §6.4.2):

1. **Simplificar sin recompilar**: `run-demo.sh 218_free_form_sprite_layer --sequence-step-frames
   40` (frames consecutivos, 1 update aparte) y de cada frame se recorta la **mayor caja sin
   texto/anillos** (118x445 px en el borde derecho, detectada por máscara de colores + dilatación)
   ampliada x2 con vecino más cercano.
2. **Prompt genérico por pares** (sin mencionar el error ni el elemento):

```text
The attached images are consecutive frames of an animation, in chronological order (image 1 is
the first). The scenery scrolls horizontally. Go pair by pair (1-2, 2-3, 3-4, ...) and estimate the
horizontal displacement of the scenery texture between each pair, in pixels. Then answer: is the
displacement the same in every pair, or does any pair differ from the rest? If a pair differs, say
which one and describe exactly what the scenery does in that pair. Be precise and concise.
```

Respuesta cruda: «Pair 3-4 (frames f0031 to f0032) differs from the rest. In every other pair, the
scenery moves a consistent 1 pixel to the left. However, in pair 3-4, the scenery moves 2 pixels
to the left. This is evident because the white, jagged edge of the lower grey terrain… has moved
two distinct pixel positions…».

Confirmación objetiva: la correlación enmascarada del fondo (solo píxeles de montaña) da en el
mismo par un residuo de **41.5** frente a ~16 del resto, con patrón `-2,0,-2,0…` roto en
`f0031→f0032` y `f0032→f0033`. El mismo salto aparece en el build **anterior a este turno**
(HEAD~1: residuo 41.5 en `f0031→f0032`), así que **no es una regresión de los cambios de §5.5**;
el estado por update (traza temporal de `c32/ctl/cpos/listas`, retirada tras el diagnóstico) es
fiel al original. La causa raíz queda **acotada** a la costura contenido↔posición del relevo de
las dos parejas de copperlist en la frontera de 32 (el modelo y la correlación coinciden en
±1-2 px); aislarla del todo requiere poder apagar por *regiones/líneas* el render desde el
emulador (petición registrada: comando `render` en el canal lateral de WinUAE-DBG). **Fix
pendiente**.

## 4. Cadencia y coste (medido con los contadores 0-4 del periférico de depuración)

El efecto **ocupa más de un campo** de VBlank: el bucle no se queda un frame por VBlank. Por eso
`g_eng_run_status.frame` ya no publica el tick de la IRQ (que avanza a 50 Hz aunque el update no
termine) sino un **contador de updates** del efecto (`m_updates`), y `measure-fps.mjs` mide la
tasa real de actualizaciones.

Medición por elementos (`run-demo.sh --read-debugperiph counters`; unidades = ciclos de CPU,
1 campo PAL = 142 102). La captura es **single-shot** en READY+500 ms: dentro de un build es
determinista (tres runs dan el mismo valor ±8 ciclos), pero entre builds la fase del contador de
32 frames puede caer en un frame con más o menos trabajo (p. ej. con o sin tira de tiles), así que
solo se comparan las cifras del **mismo build**:

| Elemento | Ciclos | ≈ líneas |
|---|---|---|
| UpdateLayerPos (fills de POS) | 37 244 | 82 |
| UpdateLayerData (columnas Copper) | 63 982 | 141 |
| BOBs (2 restores fusionados + 9 dibujos) | 137 064 | 302 |
| Update completo | 246 486 | 543 |
| Periodo del bucle (update + espera) | 284 232 | 626 |

El periodo es **2 campos exactos** (284 204): el update termina ~17.7k ciclos antes del VBlank que
publica su copperlist, y el *blitter nasty* (`BlobBatch::wait()`, macro `BlitWait` del original,
`GFX/blitter.i:26-31`) hace que el CPU no retrase al Blitter. Tasa resultante ≈ **25 updates/s**
en la configuración ciclo-exacta; la referencia original corre a ≈ 26 updates/s con el mismo
método indirecto (scroll del texto: 1 px por update).

Camino hasta aquí (medido):

- **Servicio de blit de fondo desactivado** (`engine.set_blit_service_enabled(false)`): con él
  instalado, `wait_blitter` llama al servicio en **cada** iteración del sondeo de `BBUSY`
  (`engine/src/platform/amiga/amiga_internal.hpp:210-221`); con la cola vacía eso es ~130k
  ciclos/update de llamadas inútiles. Sin servicio, la espera es el sondeo «nasty» puro del
  original. El update bajó de ~376k a ~246k.
- **Escrituras de 32 bits** en los lotes de Blitter (`BLTCON0`+`BLTCON1`, `AFWM`+`ALWM`, módulos
  empaquetados, punteros `move.l`): la sección de datos bajó de 125 806 a 63 982 ciclos.
- **Restore de BOBs fusionado** (9→2 blits, §5.2) y **tiles de FG fusionados** (28→14 rachas,
  §5.5; los mismos words de DMA, la mitad de arranques de blit).

Medición de referencia del estado de título (cuando la demo aún esperaba clic): 49.97 fps,
`fieldsPerFrame = 1.00059` — válida para el bucle ocioso, no para la tasa de updates del efecto.

## 5. Conclusiones

- **VERIFICADA** la capa de sprites: el fondo (columnas Copper + DMA) cubre toda la pantalla sin
  zonas negras ni cortadas, en el estado inicial (comparación píxel a píxel, §1) y en el efecto en
  movimiento (visión, §3.4; comparación con el original, §2).
- **VERIFICADA** la dinámica: scroll del texto 1 px/update (correlación de banda, −2 px de captura
  por par = 1 px de pantalla), BOBs presentes y completos en todos los frames muestreados, misma
  trayectoria de rebote que el original.
- **Cadencia**: el update cabe en **2 campos exactos** (284 204 ciclos) y la tasa es ≈ 25
  updates/s, en línea con las ≈ 26 del original en el mismo entorno (su update también supera un
  campo en emulación ciclo-exacta).

### 5.1 Arranque (cambio pedido por el usuario)

La demo **arranca el efecto inmediatamente** al cargar, sin esperar clic ni tecla (la referencia
original espera una pulsación del ratón; es una desviación deliberada y documentada). No hay
polling de entrada en el bucle.

### 5.2 BOBs: restore fusionado (9→2 blits) y regresiones corregidas

- **Restore**: las 9 copias de 3 palabras del original (`SPR_Layer.asm:404-411`) se fusionan en 2
  copias por frame (15 y 12 palabras) porque los BOBs de una fila están a 48 px = 3 palabras con
  la misma alineación: la unión de sus celdas es contigua y escribe exactamente las mismas
  palabras (mismo fondo limpio, mismas posiciones).
- **Regresión detectada y corregida**: un intento previo de optimizar el pegado de tiles dejó las
  **columnas Copper en negro**. La causa real no era la geometría del half-tile sino la escritura
  de 32 bits de los lotes: `write_long(kBltcon0, base_con0)` sin desplazamiento dejaba `CON0=0` y
  el valor en `CON1` (modo FILL) y corrompía los blits `Opaque`. Corregido con `<< 16`; los
  lotes de 32 bits (y el merge de tiles de §5.5) revalidados con visión y comparación con el
  original (§2, §3.4).

### 5.3 Scroll fino de la capa (arreglado)

Síntoma: la capa de sprites **no avanzaba**, solo vibraba ±1 px con la paridad de `SPRxCTL`; el
mundo daba un salto de 16 px cuando entraba la columna. Causa: el port leía `cpos_offset` pero
**nunca lo incrementaba**, y las 8 posiciones DMA se escribían sin el offset. Correcciones
(transcripción literal del original):

- caso 0 de `UpdateLayerPos` incrementa `cpos_offset` (+1 = 2 px, vuelta a 0 tras 7) y lo usa ya
  actualizado (`layer.asm:88-98`);
- las 8 `SPRxPOS` de las estructuras DMA llevan `− cpos_offset` (`layer.asm:213-234`).

Verificación objetiva (correlación por pares de frames consecutivos, enmascarando texto naranja y
BOBs para quedarse solo con el fondo): el desplazamiento medido es **uniforme, −1 px de pantalla
cada 2 updates (0.5 px/update)**, patrón `0,−2,0,−2,…` en píxeles de captura. Antes del arreglo el
patrón era de vibración sin avance neto.

Cross-validación con el original (§2 del método): capturas a 40 ms, con el desplazamiento del
texto (1 px/update) como reloj; en los pares limpios el fondo del original da
**ratio dxFondo/dxTexto ≈ 0.4–0.6**, es decir, también ~0.5 px/update. La precisión del método está
limitada (el emulador avanza varios updates por captura, el patrón de puntos es periódico y el
original no expone contador de updates); los valores se documentan como indicio fuerte, no como
medida exacta.

### 5.4 Temblor del fondo: anclaje de fase del bucle (`WaitRaster 0x2c`)

Síntoma: con el scroll fino ya avanzando, el fondo temblaba de forma **no constante**. Causa: el
efecto ocupa más de un campo (≈2.7), así que el bucle de la demo encadenaba iteraciones sin esperar
al VBlank (la señal ya estaba latchada) y la **fase del update respecto al haz derivaba**; las
escrituras «vivas» del update (`SPRxCTL` y las `SPRxPOS` de las estructuras DMA del caso 3, que no
van por la copperlist) caían cada frame en una posición de haz distinta y barrían la capa a media
visualización. El original lo evita esperando **siempre la línea 44** al inicio de cada iteración
(`WaitRaster 0x2c`, `PhotonsMiniWrapper.asm:89-95`). El port reproduce esa espera al inicio de
`update()`.

Evidencia (contadores 0/4 del periférico, 3 arranques):

| Medición | Antes | Después |
|---|---|---|
| Periodo del bucle (unidades = ciclos CPU) | 344k–422k (deriva) | **426350 / 426310 / 426334** (= 3×142102, 3 campos exactos) |
| Coste del update | 338k–403k (±10 %) | **389.5k / 389.5k / 391.6k (±0.5 %)** |
| Patrón de desplazamiento del fondo | uniforme salvo deriva | uniforme y **periódico estable** |

El bucle queda clavado a 3 campos con el estado de entonces (≈16.7 updates/s). Con las
optimizaciones de §4 (servicio de blit desactivado + longs de 32 bits) el update cabe en **2
campos exactos** y la cadencia es ≈25 updates/s, en línea con el original: el anclaje de fase se
mantiene y pasa a ser la base del doble buffer (§5.5).

### 5.5 Doble buffer del FG (desgarro de BOBs) y servicio de blit

Síntoma: los BOBs aparecían incompletos (4-6 de 9) en capturas con la CPU parada en el *probe*.

Causa raíz (doble):

1. **El FG no se dobla**. El original selecciona el buffer FG/restore con
   `btst #0,c32frame_cn_o+1` (`SPR_Layer.asm:378-398`), el byte alto de un contador 0..31 —
   siempre 0: usa siempre `fg_buf2`, el buffer **visible**. Las tablas de restore por buffer y el
   selector existen para doblar, pero el selector es código muerto. Con un update de más de un
   campo, el haz lee los BOBs a medio escribir y el desgarro depende de la fase (por eso el
   conteo varía entre frames y entre builds). El port reproduce el selector muerto
   (`m_c32 & 0x100`) hasta esta sesión.
2. Con el **servicio de blit de fondo** instalado, `wait_blitter` llama al servicio en cada
   iteración del sondeo de `BBUSY` (`amiga_internal.hpp:210-221`): ~130k ciclos/update de coste
   inútil con la cola vacía.

Arreglo:

- **Doble buffer real**: el buffer alterna por update (paridad de `m_updates`) y los BOBs se
  dibujan en el oculto; el visible siempre está completo. Es la intención del selector original,
  documentada como desviación deliberada (el render por frame es idéntico salvo el desgarro).
- **Servicio de blit desactivado** en `main()`: sin tareas de fondo en esta demo, la espera es el
  sondeo «nasty» puro del original.

Verificación: el update (246 486 ciclos) termina **~17.7k ciclos antes** del VBlank que publica su
copperlist (264 228), así que el buffer mostrado nunca está a medio dibujar; el screenshot en
marcha muestra **9 anillos (4+5)** y la visión confirma texto sin roturas, 9 anillos completos y
frases coherentes (§3.5). Los conteos de BOBs en la secuencia *step* (CPU parada en el probe) no
son fiables: la frontera de escaneo mezcla dos campos y las bandas de BOBs (que se mueven 2 px
por update) caen a distinto lado; el mismo artefacto aparece en el build base validado.

### 5.6 Tiles de FG: 28→14 rachas por ciclo

El original reparte el pegado de cada tile de 32×32 en 28 frames por ciclo de 32 (`tilemap.asm`):
dos rachas de 32 planelíneas (2 palabras) por tile, en los frames t y t+14, que escriben la mitad
alta y la mitad baja de la fila de tiles. El port las fusiona en **una racha de 64 planelíneas**
en el frame t (los frames t+14 quedan sin trabajo): la fuente de las dos mitades es contigua en el
tile (mitad superior/inferior completa) y el destino es el mismo porque solo entra
`fg_offset & 0xfffc`, que no cambia dentro del par (el +2 de `fg_offset` solo salta en el frame 31,
fuera de los pares). El segundo cuarto queda escrito 14 frames antes, fuera de la ventana visible.

Efecto: se mantienen los mismos words de DMA (el coste del Blitter no cambia) y se evitan ~42
arranques de blit por ciclo (≈60-130 ciclos de CPU por update, <0.05 %): **no medible** con los
contadores single-shot, que además muestrean fases distintas entre builds. Verificado equivalente
por construcción (los mismos bytes en las mismas direcciones) y con visión (§3.5: sin líneas de
texto rotas ni desplazadas, 9 anillos completos).

## 6. Deuda declarada

- No se ha hecho una comparación **fotograma a fotograma** del efecto: el instante del clic no es
  determinista con el runner y el original no expone un hook de «frame N». Mitigación: comparación
  de trayectorias (§2) y de la estructura de la copperlist/estructuras de sprite contra la fuente
  (§README).
- El modelo de visión local **no es fiable** para el movimiento lento, la dirección del scroll ni
  el conteo de BOBs (falsos negativos también en el original); las medidas objetivas mandan.
  En la pasada de secuencia (§3.4) acierta el fondo y los 9 BOBs completos, y falla el scroll de
  1 px/update del texto (imperceptible en la rejilla de 12 imágenes).
- El contador de *updates* es local a la demo: `measure-fps.mjs` sigue midiendo el tick de la IRQ
  en las demás demos.

### 6.1 Pasada de visión por secuencia de updates (§3.4)

`node tools/analyze/vision-run.mjs 218_free_form_sprite_layer <dirSeq> 0..11 --prompt "<pixel-oriented>"`,
12 frames consecutivos (1 update por frame). Respuesta cruda (extracto):

> «(1) El fondo **cubre toda la pantalla**. No hay zonas negras ni cortadas a la derecha. […]
> (3) En cada uno de los 12 frames se muestran **exactamente 9 bobs** […]. Todos los bobs están
> completos y bien formados en todos los frames. […] ningún bob está roto, incompleto o ausente
> en ninguno de los 12 frames. […] **No hay frame roto.**»

El informe crudo queda en `218_free_form_sprite_layer_report.md` (no versionado).
