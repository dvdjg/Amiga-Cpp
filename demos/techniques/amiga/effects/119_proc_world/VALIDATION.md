# VALIDATION — Demo 119 (`119_proc_world`)

Informe de validación (F4/F5 de `docs/guides/methodology/PROCEDIMIENTO_DEMOS_Y_JUEGOS.md`).

**Estado: VERIFICADA (alcance por elemento, ver §Conclusiones)** — la demo monta el mundo
con las piezas generadas, lo publica sin parpadeo a 49,9 fps y el contenido se corresponde
con la intención (validado con visión local). **SIN VERIFICAR**: la generación del atlas/set
en el propio 68000 (medida, precocinada; ver §Coste de generación).

Intención esperada: pantalla 320×256 con un mundo cenital 10×7 de piezas de 32 px — río
continuo de norte a sur con 2 meandros y puente donde lo cruza el camino horizontal,
muralla 3×3 con torre y puerta, dos casas, cueva y sierra en la esquina superior izquierda,
lago en la inferior derecha, vegetación dispersa — sin costuras entre piezas, con la banda
de 16 colores y la franja de versión (16 px) abajo.

## Técnica vigente

- Assets cocinados en host con la librería `eng::gfx::proc` (`MiniFloat16`; determinista, lo
  ancla `HOST-443`): 22 materiales × 3 variantes, 42 piezas con bordes tipados.
- En carga: encaje del set (42 KB), `place_world` (casación + curvas del río por
  `add_variant`), composición 10×7 en el framebuffer y `IndexedDisplay::present` (C2P) una
  vez. Frame estático; el bucle late con el VBlank (coste ~0).
- `--dump` host (`HOST-442`) y demo comparten semillas: mismo mundo en la evidencia host y
  en pantalla.

## Mediciones

| Medida | Comando | Resultado | Lectura |
|---|---|---|---|
| fps (debug, warp) | `node tools/debug/measure-fps.mjs 119_proc_world A500_debug --json` | `49.92 fps`, `1.0016 campos/frame`, `142102 ciclos/frame` | 1 campo por VBlank, CPU ociosa |
| fps (grabado) | `node tools/debug/record-fps.mjs 119_proc_world A500_debug --samples 1` | `49.97 fps` (detail `0x119`) | fila registrada en `BITACORA_SCROLL_TILES.md` |
| Carga (debug) | franja de versión de la captura | `LOAD 1145KC` (≈1,15 M ciclos) | encaje+colocación+composición+C2P |
| Regresión + parpadeo | `tools/test-regression.sh --flicker --require-flicker-ok --demo … --warp` | **Regression OK** | gate de captura propio (`analyze-screenshot.sh`) y `flicker` sin candidatos |
| Gate de captura | `analyze-screenshot.sh` | `white=521 dark=14191 nonBlue=48086` | hierba/agua/masa presentes y franja legible |

### Coste de generación en el 68000 (por qué se precocina)

Medido con el periférico de depuración (`0xB7E928`) y marcas de etapa en
`g_eng_run_status.detail` durante el desarrollo (build release, warp):

| Etapa/sonda | Ciclos medidos | Equivalencia |
|---|---|---|
| 16× `value_noise2<MiniFloat16>` | ≈315.000 | ≈19,7 k ciclos por muestra (≈2,8 ms) |
| 16× `fbm2` 3 octavas | ≈1.010.000 | ≈63 k por evaluación |
| 1 textura 32×32 (fbm 3 oct + dither) | ≈52.600.000 | **≈51 k ciclos/píxel** |
| atlas completo (66 texturas) | >1,06 G (timeout a 150 s) | ≈3,4 G estimados → **≈8 min** |
| 1024× `fbm2_cached` (rejilla de nodos) | ≈50 M | la caché evita los hashes pero el coste por píxel sigue dominado por `__mulsi3` (32×32) del resto de la ruta MF |

Causa raíz: en 68000 cada multiplicación de 32 bits es una libcall `__mulsi3`
(≈1.100 ciclos; `node tools/analyze/asm-audit.mjs` la localiza en `value_noise2<MiniFloat16>`
y en el camino MF). El hash del ruido usa 16 de esas multiplicaciones por muestra. Por eso
los assets se cocinan en host (misma librería y semillas: contenido idéntico) y la demo solo
hace lo barato en carga; el patrón y las alternativas están en `PROCEDURAL_BITMAPS.md`.

## Prompts y respuestas (crudos)

Modelo local `qwen3-vl:8b-instruct-q8_0` (Ollama); capturas en `out/run/119_proc_world/A500_debug/`
y hojas/zonas host en `out/playground/proc-bitmaps/`.

### A · Inventario sin contexto (captura on-target)

Prompt: “Es una captura de una demo Amiga 500 (pantalla 320x256, paleta de 16 colores).
Describe en espanol SIN suposiciones: (1) Que elementos del terreno reconoces y donde
(rio, camino, muralla/fortaleza, casas, arboles, cueva, montana, lago)? (2) El rio: por
donde entra y sale de la pantalla? Es continuo? (3) La banda inferior: que contiene
(cuenta las franjas de color evidentes) y que texto se lee? (4) Hay costuras, huecos o
partes rotas entre las piezas?”

Respuesta (cruda, extracto): “- Río: Corre verticalmente por la pantalla, desde la parte
superior izquierda hasta la parte inferior central, con una curva a la derecha en la mitad.
Es continuo en la imagen. - Camino: Una franja horizontal marrón … - Muralla/fortaleza: Un
recuadro con borde gris y una abertura negra … - Casas: Dos estructuras … - Montaña: Un cono
de color marrón y blanco en la parte superior izquierda. … (2) El río entra por la parte
superior izquierda y sale por la parte inferior central. Es continuo en la imagen. …
(4) No hay costuras, huecos ni partes rotas entre las piezas.”

### B · Zonas dirigidas (captura on-target, ampliada)

- **Fortaleza** — Prompt: “(1) Describe la estructura gris … (2) El disco claro con anillo y
  contorno oscuro en el muro izquierdo: que parece (torre/bastion…)? (3) Hay una abertura
  oscura? …”. Respuesta: “(1) … una muralla o recinto de forma rectangular … (2) parece una
  torre o bastión circular … (3) Sí, hay una abertura oscura en la parte inferior del
  recinto … parece una puerta o entrada.”
- **Esquina superior izquierda** — Prompt: cueva/montaña. Respuesta: “(1) … un centro negro
  … sugiere un hueco o cueva en terreno rocoso. (2) … forma triangular en color blanco
  (cima) … Parece una montaña con cima clara y ladera en sombra.”
- **Esquina inferior derecha (lago)** — Prompt: “Describe … TODO lo que ves … Hay alguna
  forma azul? …”. Respuesta: “Sí, hay una forma azul. Color: azul oscuro (marino o cobalto).
  … redondeada … en la esquina inferior derecha.”

### B′ · Contenido host (mismas semillas; hojas y zonas)

- Materiales (22 muestras): “parecen en su mayoría texturas de material con detalle
  orgánico … Sólo una muestra es claramente un color plano (blanco puro) [nieve]”.
- Piezas (42): reconoce “muros …, ríos …, lagos …, árboles …, casas …, montañas
  (triángulos grises con cima blanca), cueva (círculo negro sobre fondo gris), camino …,
  puente …” y que “parecen coherentes y reutilizables para montar un mapa”.
- Mapa del colocador 10×7: “río … desde la parte superior hasta la inferior … Es continuo …
  cueva … esquina superior izquierda … montaña con nieve; no hay costuras o cortes raros”.
- Zonas: cueva (“hoyo en terreno rocoso”), montaña (“cima clara y ladera en sombra”),
  castillo (“recinto fortificado” + torre/bastión + puerta), puente (“tramo rectangular …
  parece una estructura construida, como un puente o una pasarela”).

## Conclusiones

1. **Composición y display verificados en hardware**: la captura on-target muestra el
   mundo completo con río continuo (entrada superior, salida inferior), camino con puente,
   muralla con torre y puerta, casas, cueva, sierra y lago, sin costuras; la visión local
   confirma cada expectativa (macro y zonas). Cubre: colocación por casación, composición
   10×7, banda de paleta y franja de versión.
2. **Rendimiento**: 49,92–49,97 fps, 1,0016 campos/frame, sin picos — frame estático de
   coste ~0 por frame (todo el coste es de carga). Parpadeo: sin candidatos.
3. **Generación en el 68000**: medida y **no viable** como paso de carga (≈51 k
   ciclos/píxel; atlas ≈3,4 G). Queda precocinada en host (misma librería, determinista,
   anclada por `HOST-443`); esto es el patrón recomendado del repo (como la 201) y está
   documentado en `PROCEDURAL_BITMAPS.md` §Coste.
4. **SIN VERIFICAR**: (a) la generación runtime del atlas/set en el Amiga (medida, no
   usada); (b) la vista con música/animación (la demo es estática por diseño).

## Deuda registrada

- Ninguna abierta para la demo. Mejora futura registrada en `PROCEDURAL_BITMAPS.md`:
  portar el ruido por tablas de nodos al dominio entero (mulu.w) para acercar la
  generación runtime al presupuesto de carga.
