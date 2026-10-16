# 083 — FBM NOISE: intención detallada

Documento de **intención** de la demo (gate `tools/check/demo-docs.mjs`): qué hace, qué pretende demostrar, qué hay en pantalla y cómo evoluciona en el tiempo. La evaluación del modelo de visión vive en `VISION.md` (prompt canónico, sin pistas).

## Qué hace

Genera un **campo fbm 2D** (suma de 3 octavas de value noise, `eng::math::fbm2`) sobre una rejilla gruesa de 16×16 celdas y lo pinta como **mapa de altura animado** sobre un display **copper chunky** de 288×256 (36×64 bloques de 8×4): la CPU escribe cada frame los `COLOR00` de la rejilla de bloques y **el Copper los pinta por líneas, sin bitplanes**. Una **paleta por altura** (6 paradas: azul profundo, azul, arena, verde, roca, nieve) convierte el valor del campo en color. Por frame el fbm **no se re-evalúa**: se muestrea **bilinealmente** la rejilla cocida al inicio con un offset animado en Q8, de modo que el coste por frame es `cols×rows` escrituras de color, no miles de evaluaciones de ruido. El muestreo bilineal usa `mul_wide<s16>` (`muls.w`), nunca `int` (que emitiría `__mulsi3`).

## Qué pretende demostrar

1. Que un **campo procedural fbm** (relieve sin formas geométricas sencillas) cabe en el presupuesto del **68000**: cocer el ruido una vez y muestrear en cada frame.
2. El camino **copper chunky** del engine (`eng::effects::CopperChunky`): color por bloque vía `COLOR00` + Copper, sin bitplanes ni C2P.
3. El escalar **`MiniFloat16`** como tipo de trabajo del efecto (`fbm2<MF>`; `Mat`/`Vec` no intervienen aquí) **sin usar `float`/`double` en runtime** (los literales `MF(0.25f)` son constantes de compilación).
4. La **conversión decimal de texto del MF en el propio 68000**: el HUD compone el valor fbm actual con `eng::util::to_chars` (`scalar_decimal<MiniFloat16>`: por bits, sin `float`, sin libcalls) — es la verificación en hardware de ese punto de extensión, contrastada contra el mismo cálculo en host (`0.663` en ambos).

## Elementos en pantalla

- **Campo de terreno de 288×256 px** (`36×64` bloques), coloreado por altura: azules (agua) en las zonas bajas, arena en la orilla, verdes en las medias, marrón/roca y blanco (nieve) en las cimas; el reparto concreto depende del offset muestreado.
- **HUD de depuración** en la esquina superior izquierda del overlay de WinUAE (el display es solo copper: no hay bitplanes donde poner texto): la etiqueta `fbm ` seguida del valor **con 3 decimales** (p. ej. `fbm 0.663`), en blanco.

## Evolución temporal

- Cada frame el offset de muestreo avanza **1 unidad Q8 por eje** (~1/114 de bloque por frame, ≈ 0.07 px): la ventana del campo se **desliza lentamente** y el terreno se desplaza de forma continua; al llegar a ±3 celdas gruesas el sentido se **invierte** (ping-pong), así que no hay salto de costura.
- El HUD se **re-formatea cada 16 frames** (fuera del camino caliente); su valor cambia cuando el índice de celda (`offset >> 8`) cambia, cada 256 frames.
- La deriva es **sutil y autosimilar**: el diff determinista entre frames consecutivos da SSIM ≈ 0.997–0.998 y 128–2.560 px cambiados por par (uniforme, sin picos de glitch), pero el modelo de visión de la evaluación canónica **no percibe el movimiento** en ventanas de pocos segundos y describe la escena como estática (limitación del VLM ante deriva lenta sobre ruido autosimilar; queda documentada aquí; el movimiento queda evidenciado por el frame-diff determinista, que prevalece ante una respuesta dudosa del modelo).
