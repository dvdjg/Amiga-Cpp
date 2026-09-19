# `games/200_holdem` — póker Texas Hold'em sobre el engine

Primer consumidor real de `eng::cards`. El jugador (asiento 0) juega contra dos bots
CPU (asientos 1 y 2) con el joystick.

## Controles

- **Izquierda/derecha**: eligen la acción legal resaltada (pasar, paso, igualar,
  subir, all-in).
- **Fuego**: confirma la acción. Al terminar la mano, fuego reparte la siguiente.

## Qué motor usa

- `eng::cards::Table` (Texas Hold'em No-Limit): ciegas, botón, calles (preflop/flop/
  turn/river), acciones legales, **botes laterales** por niveles de aportación y
  showdown.
- `decide_with_plan` con perfil **N20** (heurística preflop + fuerza de mano, sin
  Monte Carlo) para los bots: cabe sobradamente en un A500 (ver
  `demos/amiga/124_cards_bench`).
- Display: `StaticEhbScene` (320×256) rasterizado a los bitplanes por CPU (patrón de
  la demo 060).

## Visual

Cartas comunitarias, bote y apuesta viva, stacks de los tres asientos, las dos cartas
del jugador, las de la CPU tapadas, y el menú de acciones resaltando la elegida.

## Build / run / analyze

```bash
bash tools/build/build-demo.sh games/200_holdem --debug --clean
bash tools/run/run-demo.sh games/200_holdem
bash tools/analyze/analyze-demo.sh games/200_holdem
```

## Estado

- **Compila y enlaza** con el flujo canónico (`build-demo.sh`, verificado).
- **Ejecutado en WinUAE** (`run-demo.sh --warp`): alcanza READY y captura
  `out/run/200_holdem/A500_release/screenshot.png`; `analyze-demo.sh` da **OK**
  (`nonBlue=48384`, texto blanco y felt verde). Evidencia build → run → analyze.
- **Pendiente**: pulido visual (tamaños de carta, colores), reloj/marcador de manos,
  selector de perfil/variante en el juego y una `analyze-sequence.sh` propia.
