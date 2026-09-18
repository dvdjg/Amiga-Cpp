# `tools/cards/` — simulación de póker en host

Herramientas host para **ajustar el nivel** de los motores de naipes (`eng::cards`)
sin emulador. Usan el propio engine header-only, así que el binario mide exactamente
las mismas reglas, IA y perfiles de memoria que consumirá el juego.

## `selfplay.sh` — partidas CPU vs CPU

```bash
tools/cards/selfplay.sh [hands] [--seats N] [--seed S] [--stack N] [--sb N] [--bb N]
  [--profile N20|N64|N128|N256|N512] [--sessions N]
  [--table-samples N] [--range-classes N] [--range-mode dynamic|table|none]
  [--styles tp,ta,lp,la,eq] [--compare] [--sweep] [--csv ruta.csv]
  [--out ruta.txt] [--quiet]
# por defecto: 200 --seats 6 --seed 1 --profile N512 --range-mode dynamic
```

Juega `hands` manos con botón rotando y recompra por mano, con estilos distintos por
asiento, perfil de memoria `N20`…`N512`, **tabla preflop** de 169 clases y un **rango de
rival**. Reproduce exactamente la configuración que usará el juego (la misma
`run_session`), así que sirve para:

- comparar **perfiles** (cuánto aporta el Monte Carlo frente a la heurística);
- comparar **estilos** (`tp` tight-pasivo, `ta` tight-agresivo, `lp` loose-pasivo,
  `la` loose-agresivo, `eq` equilibrado);
- medir **bb/100**, showdowns, actividad y el máximo de acciones por mano.

Modos de rango del rival (`--range-mode`):

- `dynamic` (por defecto): el rango se deriva de las frecuencias fold/call/raise que el
  `OpponentModel` observa de cada rival (se retira mucho ⇒ rango estrecho).
- `table`: rango fijo con las `--range-classes` mejores clases de la tabla preflop.
- `none`: sin modelo de rango (rival aleatorio puro).

Comparación de nivel:

- `--compare`: fija el asiento 0 como **referencia** (tight-agresivo) y reparte el resto
  de estilos; el bb/100 de la referencia mide si el nivel sube o baja.
- `--sweep`: repite `--compare` para **todos** los perfiles y muestra una fila por perfil
  (plan, bb/100 de la referencia, showdowns y subidas). Es el detector de regresiones de
  nivel por footprint.
- `--csv ruta.csv`: vuelca la tabla (`mode,profile,seat,style,net,bb100_centi,hands,
  showdowns,raises,calls,folds`) para procesarla con otras herramientas.

La tabla preflop se construye una vez con Monte Carlo determinista (`--table-samples`, por
defecto 64).

Ejemplo: mesa de 6 con estilos fijos, 500 manos y semilla reproducible:

```bash
tools/cards/selfplay.sh 500 --seats 6 --seed 7 --styles ta,lp,eq,la,tp \
  --profile N512 --out out/cards/selfplay/mesa6.txt
```

Ejemplo: barrido de perfiles con referencia y CSV:

```bash
tools/cards/selfplay.sh 300 --seats 6 --compare --sweep --sessions 4 \
  --csv out/cards/selfplay/sweep.csv --quiet
```

En consola imprime, por asiento, el estilo, el net, el **bb/100** y las manos; y al
final los totales de showdowns, retiradas y acciones. Con `--out` escribe el informe
a `out/cards/selfplay/` (gitignored).

Compilación: usa `CXX` (por defecto `g++` del PATH) con `-I engine/include`.
