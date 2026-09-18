# `tools/cards/` — simulación de póker en host

Herramientas host para **ajustar el nivel** de los motores de naipes (`eng::cards`)
sin emulador. Usan el propio engine header-only, así que el binario mide exactamente
las mismas reglas, IA y perfiles de memoria que consumirá el juego.

## `selfplay.sh` — partidas CPU vs CPU

```bash
tools/cards/selfplay.sh [hands] [--seats N] [--seed S] [--stack N] [--sb N] [--bb N]
  [--profile N20|N64|N128|N256|N512] [--sessions N]
  [--table-samples N] [--range-classes N] [--styles tp,ta,lp,la,eq]
  [--no-range] [--out ruta.txt] [--quiet]
# por defecto: 200 --seats 6 --seed 1 --profile N512
```

Juega `hands` manos con botón rotando y recompra por mano, con estilos distintos por
asiento, perfil de memoria `N20`…`N512`, **tabla preflop** de 169 clases y **rango de
rival** derivado de ella. Reproduce exactamente la configuración que usará el juego
(la misma `run_session`), así que sirve para:

- comparar **perfiles** (cuánto aporta el Monte Carlo frente a la heurística);
- comparar **estilos** (`tp` tight-pasivo, `ta` tight-agresivo, `lp` loose-pasivo,
  `la` loose-agresivo, `eq` equilibrado);
- medir **bb/100**, showdowns, actividad y el máximo de acciones por mano.

La tabla preflop se construye una vez con Monte Carlo determinista
(`--table-samples`, por defecto 64) y `--range-classes` fija cuántas clases entran en
el rango del rival (por defecto 40). `--no-range` desactiva el modelo de rango.

Ejemplo: mesa de 6 con estilos fijos, 500 manos y semilla reproducible:

```bash
tools/cards/selfplay.sh 500 --seats 6 --seed 7 --styles ta,lp,eq,la,tp \
  --profile N512 --out out/cards/selfplay/mesa6.txt
```

En consola imprime, por asiento, el estilo, el net, el **bb/100** y las manos; y al
final los totales de showdowns, retiradas y acciones. Con `--out` escribe el informe
a `out/cards/selfplay/` (gitignored).

Compilación: usa `CXX` (por defecto `g++` del PATH) con `-I engine/include`.
