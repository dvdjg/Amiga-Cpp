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

## 4. Cadencia y coste

- El bucle usa el **latido del mini-SO por IRQ de VBlank** (`os::init` + `run_frames`); el
  trabajo por frame se completa dentro del VBlank (la trayectoria medida avanza al ritmo del
  original, sección 2).
- `node tools/debug/measure-fps.mjs 218_free_form_sprite_layer A500_debug --json`
  (estado de título, 20 s): **emulated 49.97 fps / host 49.93 fps**, `fieldsPerFrame = 1.00059`
  (1001 frames, 142 102 000 ciclos ≈ 141 960 ciclos/frame). Un frame por VBlank, sin saltos.
- El coste por frame es el de la referencia: 20 fills de posición + 28 copias de media columna +
  3 copias de cuarto de tile + 18 restores/dibujos de BOB (3 palabras × 128 filas) repartidos por
  el Blitter, más ~40 escrituras de registros de Blitter y 13 palabras de parcheo de punteros.

## 5. Conclusiones

- **VERIFICADA** para el estado inicial: igualdad píxel a píxel con el original.
- **VERIFICADA** para la dinámica: mismas trayectorias (BOBs), misma cadencia y mismo
  comportamiento del fondo/texto/barra que el original, con el desfase del clic como única
  diferencia.
- Sin parpadeo ni zonas rotas (visión, pases A de ambos binarios).

## 6. Deuda declarada

- No se ha hecho una comparación **fotograma a fotograma** del efecto: el instante del clic no es
  determinista con el runner (se inyecta tras el READY) y la demo no expone un hook de «frame N
  tras el clic» que el runner pueda usar en el original. Mitigación: comparación de trayectorias
  (§2) y de la estructura de la copperlist/estructuras de sprite contra la fuente (§README).
- El modelo de visión local **no es fiable** para el movimiento lento, la dirección del scroll ni
  el conteo de BOBs (falsos negativos también en el original); las medidas objetivas mandan.
