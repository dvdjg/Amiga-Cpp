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
- **Personajes**: cada asiento tiene una `eng::sim::Persona` (arquetipo) y su
  `PsycheState`. El humano es el **flemático**; la CPU 1 un **pardillo** y la CPU 2 un
  **engreído**. Los bots deciden con `decide_with_persona` (parámetros derivados del
  arquetipo y del estado) y su **cara refleja sus tells** (fugas de `expression.hpp`).
- La mesa **lee** los tells: en cada showdown, `ReadModel` etiqueta los gestos vistos como
  mano fuerte/débil y los rivales aprenden a leer al pardillo. La psique evoluciona por
  mano (ganar/perder → confianza, tilt, racha).
- Perfil de memoria **N20** (heurística + fuerza, sin Monte Carlo): cabe en un A500 (ver
  `demos/amiga/124_cards_bench`).
- Display: `StaticEhbScene` (320×256) rasterizado a los bitplanes por CPU (patrón de la
  demo 060).

## Visual

Cartas comunitarias, bote y apuesta viva, stacks de los tres asientos, las dos cartas del
jugador, las de la CPU tapadas, **avatares** y el menú de acciones resaltando la elegida.
Cada avatar dibuja una **cara** que sonríe, se frunce, sube las cejas, abre la boca o
tiembla según los tells del asiento, y además una **postura corporal** combinada
(`pose_from_gesture`): se inclina al atacar, retrocede al defenderse, se desploma al
desanimarse y se encoge al temblar. Bajo cada cara, una barra muestra **confianza**
(verde) y **tilt** (rojo) del asiento, de modo que la evolución de la psique se ve sin menús.

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
