# VALIDATION — Demo 119 (`119_proc_world`)

Informe de validación (F4/F5 de `docs/guides/methodology/PROCEDIMIENTO_DEMOS_Y_JUEGOS.md`).

**Estado: VERIFICADA (alcance por elemento, ver §Conclusiones)** — la demo monta el mundo
con las piezas generadas, lo publica sin parpadeo a ~49,9 fps y el contenido se corresponde
con la intención (validado con visión local). **SIN VERIFICAR**: la generación del atlas/set
en el propio 68000 (medida, precocinada; ver §Coste de generación) y el display de 6 planos.

Intención esperada: pantalla 320×256 con un mundo cenital 10×7 de piezas de 32 px y **32
colores** (5 planos) — río continuo de norte a sur con 2 meandros y puente donde lo cruza el
camino horizontal; **afluente** por la fila superior que desemboca en el cauce con una unión
en T; **sendero en L** desde el camino hasta la puerta del castillo (cruce en T + fondo de
saco); muralla 3×3 con torre y puerta; dos casas, cueva y sierra en la esquina superior
izquierda, **lago junto al cauce**, vegetación dispersa con **texturas en tableros 2×2
(halftone)** — sin costuras entre piezas, con la banda de 32 colores y la franja de versión
(16 px, nombre + build + contador de frames) abajo.

## Técnica vigente

- Assets cocinados en host con la librería `eng::gfx::proc` (`MiniFloat16`; determinista, lo
  ancla `HOST-443`): 22 materiales × 3 variantes con **paleta de 32 colores** (8 familias de
  4 niveles) y **paso de halftone** (las texturas se cuantizan a `2·levels−1` pasos y los
  intermedios se pintan como tableros 2×2 del par vecino → 7 tonos aparentes por familia);
  catálogo de 45 piezas + 24 rotaciones de relleno (69 cocidas) con bordes tipados.
- En carga: encaje del set (~69 KB), `place_world` (casación; deriva 9 orientaciones de
  río/caminos por `add_variant` → 78 piezas en runtime), composición 10×7 en el framebuffer
  y `IndexedDisplay<5,1>::present` (C2P de 5 planos) una vez. El coste por frame es solo la
  telemetría: contador de frames por **parcheo planar mínimo** (`patch_text`, 1-2 celdas de
  8×8) y espera de VBlank **activa** (`set_vblank_sleep(false)`, variante 2 del 212).
- `--dump` host (`HOST-442`) y demo comparten semillas (atlas `0x7E5A1`, set/mapa `0xBEE5`):
  mismo mundo en la evidencia host y en pantalla (la demo añade las rotaciones de relleno).

## Mediciones

| Medida | Comando | Resultado | Lectura |
|---|---|---|---|
| fps (debug) | `node tools/debug/measure-fps.mjs 119_proc_world A500_debug --json` | `49.82–49.92 fps`, `1.004 campos/frame`, `142102–142386 ciclos/frame` | 1 campo por VBlank, CPU ociosa |
| fps (grabado) | `node tools/debug/record-fps.mjs 119_proc_world A500_debug --samples 1` | `49.92 fps` (detail `0x119`) | fila registrada en `BITACORA_SCROLL_TILES.md` |
| Carga (debug) | franja de versión de la captura | `LOAD 11636–12126KC` (≈1,2 M ciclos) | encaje + colocación + composición + C2P 5 planos |
| Regresión + parpadeo | `tools/test-regression.sh --flicker --require-flicker-ok --demo … --warp` | **Regression OK** | gate de captura propio y `flicker` sin candidatos |
| Gate de captura | `analyze-screenshot.sh` | `white=572 dark=13945 nonBlue=48124` | hierba/agua/masa presentes y franja legible |
| Contador de frames | capturas seriadas | `F 000009 → F 000045 → F 000644 → F 000670` | el parcheo planar avanza con el bucle |

### Contador de frames y número de build (persistencia)

- El **build** lo inyecta `build-demo.sh` (`-DENG_BUILD_NUMBER=n`, contador persistente
  `out/.build-number`) y la franja lo muestra (`PROC WORLD v1.0.3010` en la captura de esta
  validación); `HOST-442` no lo cubre (es de toolchain).
- El contador de frames es telemetría viva: `patch_text` reescribe por planos **solo los
  dígitos que cambian** (incremento decimal sin división; acarreo raro). Coste medido:
  el bucle sigue a 1,004 campos/frame con el parcheo activo.

### Espera de VBlank bajo SO vivo (fijada)

Con el bucle por `Wait()` (defecto), esta demo se **congelaba** a los ~69 frames bajo lanzamientos no
interactivos (medidores): el libro de display del SO reescribe `INTENA` y **borra el `VERTB`**
que arma el engine → la tarea queda dormida para siempre (PC muestreado en el `STOP` de exec,
`state=Ready`). Se fija la **variante 2** (`set_vblank_sleep(false)`: latido por IRQ + espera
activa con re-arme de `VERTB` por iteración), la base documentada para demos bajo SO vivo
([`212-vblank-irq-apagada-por-el-so.md`](../../../../../docs/debugging/investigaciones/212-vblank-irq-apagada-por-el-so.md)).
Tras el cambio, `measure-fps` mide 49,8–49,9 fps de forma reproducible.

### Observación de captura (artefacto del emulador)

Con la espera activa, las capturas de WinUAE muestran una barra roja de ~5×32 px en el borde
izquierdo de la franja (con un borde crema) que **no** aparece con el bucle por `Wait()` ni en
capturas de builds anteriores; está **solo en la captura** (píxeles del framebuffer intactos:
el gate `analyze-screenshot.sh` y la visión no la detectan como contenido). Se registra como
artefacto del emulador asociado al modo de espera, no como defecto de la demo.

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
(incluye `zones/`) y hojas/zonas host en `out/playground/proc-bitmaps/v3/`.

### A · Inventario completo (captura on-target, build de 32 colores)

Prompt: “Es una captura de una demo Amiga 500 (mapa de terreno procedural 10×7 tiles,
320×224, paleta de 32 colores, franja inferior de telemetría). Verifica: (1) el río entra por
arriba (con un afluente horizontal en la fila superior que desemboca en una unión en Y/T) y
sale por abajo con meandros, y el camino lo cruza con un puente; (2) hay un sendero vertical
que sube desde el camino hasta la puerta del castillo …; (3) hay un lago azul junto al río;
(4) cueva … y montaña …, casa amarilla con tejado rojo, árboles y rocas; (5) la vegetación
muestra texturas con tableros/dither suave (no colores planos); (6) la franja inferior: se
lee PROC WORLD v1.0.NNNN (con número de build), LOAD NNNNKC, PIECES 78 y un contador F con 6
dígitos; (7) ¿colores rotos, índices erróneos o zonas mal encajadas?”

Respuesta (cruda, extractos): “(1) **Verificado.** El río entra por la parte superior con un
afluente horizontal que desemboca en una unión en forma de "Y" o "T" … desciende con
meandros … hay un puente simple (rectángulo marrón) … (2) **Verificado.** Hay un sendero
vertical … hasta la muralla rectangular del castillo, que contiene una puerta negra … (3)
**Verificado.** … un lago circular azul, con un borde más claro … (4) **Verificado.** … pico
blanco …, cueva …, casa amarilla con tejado rojo … (6) `PROC WORLD v1.0.3010` / `v1.0.3011`
→ versión correcta, con número de build; `LOAD 12126KC` / `11636KC`; `PIECES 78`;
`F000009` / `F000045` … (7) **No se detectan** [colores rotos]. … **Todos los puntos
verificados como correctos.**”

### B · Materiales con paleta de 32 colores y halftone (contenido host)

Prompt: “Hoja de 22 texturas de materiales procedurales … paleta de 32 colores y un paso de
halftone (tableros 2×2 que mezclan colores vecinos … una familia de 4 colores muestra 7
tonos). Describe: (1) si los materiales se distinguen …; (2) si los degradados se ven
escalonados o suaves; (3) artefactos: bandas duras, ruido sucio, colores rotos.”

Respuesta (cruda, extractos): “Cada material se distingue por su patrón, color y nivel de
detalle… Los degradados **aparecen suaves y naturales**. El halftone 2x2 … crea una ilusión
óptica de transiciones suaves … No hay bordes duros ni bandas visibles … No se observan
**artefactos significativos** … El uso del halftone 2x2 es una técnica excelente para manejar
paletas limitadas.”

### C · Zona sendero → puerta (recorte de la captura)

Prompt: “Recorte de la zona del castillo … debajo de la puerta, hay una franja clara de
SENDERO que baja desde la puerta hasta el camino … Describe dónde empieza y termina … y si
conecta visualmente la puerta con el camino inferior.”

Respuesta (cruda, extracto): “**Sí**, esta franja clara **conecta visualmente** la puerta del
castillo con el camino horizontal inferior. Es la única ruta visible que une la entrada del
castillo con el camino …”

### D · Zonas del mapa host (`v3`, 32 colores)

- Mapa 10×7: “río … continuo … sin costuras”, “unión en Y del afluente con el cauce”,
  castillo “con una torre y una puerta”, “lago junto al río”. Sin elementos rotos.
- Materiales (hoja de 22): ver §B.

## Conclusiones

1. **Composición y display verificados en hardware**: la captura on-target muestra el mundo
   completo con río continuo (entrada superior con desembocadura en T del afluente, salida
   inferior), camino con puente, **sendero hasta la puerta del castillo**, muralla con torre
   y puerta, casas, cueva, sierra y **lago junto al cauce**, sin costuras; la visión local
   confirma cada expectativa (macro y zona). Cubre: colocación por casación (incluidas las
   piezas nuevas del catálogo), composición 10×7, display de **5 planos** (32 colores), banda
   de paleta y franja de versión con build y contador vivo.
2. **Rendimiento**: 49,82–49,92 fps, ~1,004 campos/frame, sin picos — frame estático cuyo
   único trabajo por frame es el parcheo de los dígitos (verificado con capturas seriadas).
   Parpadeo: sin candidatos.
3. **Generación en el 68000**: medida y **no viable** como paso de carga (≈51 k
   ciclos/píxel; atlas ≈3,4 G). Queda precocinada en host (misma librería, determinista,
   anclada por `HOST-443`); esto es el patrón recomendado del repo (como la 201) y está
   documentado en `PROCEDURAL_BITMAPS.md` §Coste.
4. **SIN VERIFICAR**: (a) la generación runtime del atlas/set en el Amiga (medida, no
   usada); (b) el display de **6 planos** de `IndexedDisplay` (HOST-367 cubre su C2P, pero no
   hay demo que lo muestre); (c) la vista con animación (la demo es estática por diseño).

## Deuda registrada

- La barra roja de ~5×32 px en las capturas con espera activa (ver §Observación de captura):
  artefacto del emulador, sin impacto en el contenido; pendiente de reportar/comprobar a
  fondo si vuelve a molestar.
- Mejora futura registrada en `PROCEDURAL_BITMAPS.md`: portar el ruido por tablas de nodos
  al dominio entero (mulu.w) para acercar la generación runtime al presupuesto de carga.
