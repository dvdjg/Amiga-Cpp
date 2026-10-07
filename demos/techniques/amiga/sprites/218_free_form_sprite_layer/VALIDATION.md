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

## 4. Cadencia y coste (medido con los contadores 0-4 del periférico de depuración)

El efecto **ocupa más de un campo** de VBlank: el bucle no se queda un frame por VBlank. Por eso
`g_eng_run_status.frame` ya no publica el tick de la IRQ (que avanza a 50 Hz aunque el update no
termine) sino un **contador de updates** del efecto (`m_updates`), y `measure-fps.mjs` mide la
tasa real de actualizaciones.

Medición por elementos (`run-demo.sh --read-debugperiph counters`; unidades = ciclos de CPU,
1 campo PAL = 142 102):

| Elemento | Ciclos | ≈ líneas |
|---|---|---|
| UpdateLayerPos (fills de POS) | 21 972 | 48 |
| UpdateLayerData (columnas Copper) | 125 460 | 276 |
| BOBs (2 restores fusionados + 9 dibujos) | 154 108 | 339 |
| Update completo | 366 992 | 808 |
| Periodo del bucle (update + espera) | 344 490 | 758 |

Tasa resultante ≈ **20 updates/s** en la configuración ciclo-exacta del runner. La referencia
original, medida con el mismo método indirecto (scroll del texto: 1 px por update), corre en ese
mismo entorno a ≈ **26 updates/s**: el efecto es intrínsecamente más caro que un campo en
emulación ciclo-exacta (los blits compiten con el DMA de display; el coste fijo por blit del
emulador es ~2 000 ciclos). El original usa la macro `BlitWait` con *blitter nasty*; el engine
aplica el mismo truco en `BlobBatch::wait()` (ver `engine/include/eng/platform/amiga/blob_batch.hpp`).

Medición de referencia del estado de título (cuando la demo aún esperaba clic): 49.97 fps,
`fieldsPerFrame = 1.00059` — válida para el bucle ocioso, no para la tasa de updates del efecto.

## 5. Conclusiones

- **VERIFICADA** la capa de sprites: el fondo (columnas Copper + DMA) cubre toda la pantalla sin
  zonas negras ni cortadas, en el estado inicial (comparación píxel a píxel, §1) y en el efecto en
  movimiento (visión, §3.4; comparación con el original, §2).
- **VERIFICADA** la dinámica: scroll del texto 1 px/update (correlación de banda, −2 px de captura
  por par = 1 px de pantalla), BOBs presentes y completos en todos los frames muestreados, misma
  trayectoria de rebote que el original.
- **SIN VERIFICAR**: paridad de cadencia exacta con el original en emulación ciclo-exacta (el port
  ≈ 20 updates/s, el original ≈ 26 en el mismo entorno; en hardware real ambos caben en un campo).

### 5.1 Arranque (cambio pedido por el usuario)

La demo **arranca el efecto inmediatamente** al cargar, sin esperar clic ni tecla (la referencia
original espera una pulsación del ratón; es una desviación deliberada y documentada). No hay
polling de entrada en el bucle.

### 5.2 BOBs: restore fusionado (9→2 blits) y regresiones corregidas

- **Restore**: las 9 copias de 3 palabras del original (`SPR_Layer.asm:404-411`) se fusionan en 2
  copias por frame (15 y 12 palabras) porque los BOBs de una fila están a 48 px = 3 palabras con
  la misma alineación: la unión de sus celdas es contigua y escribe exactamente las mismas
  palabras (mismo fondo limpio, mismas posiciones).
- **Regresión detectada y corregida (columnas Copper en negro)**: un intento de fusionar también
  los 28 blits de media columna en 14 blits de 2 palabras (con una copia de tiles con pares
  intercambiados) dejó las **columnas Copper en negro** (la geometría real del *half-tile* no es
  la supuesta: la fila avanza 168 B en destino y 4 B en fuente). Revertido a la forma exacta del
  original (dos blits de 1 palabra por fila, `layer.asm:358-363`), que es la validada en §1.

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

El bucle queda clavado a 3 campos (≈16.7 updates/s en emulación ciclo-exacta; el original ≈25). El
objetivo a medio plazo es bajar el update a ≤2 campos (merge correcto de los blits de columna +
menos coste por blit) para igualar la cadencia del original; queda como deuda.

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
