# Board games: coherencia en host y rendimiento en Amiga

Informe de verificación del motor de tablero (`eng::board`) mediante la simulación
host (`tools/board/selfplay.sh`) y de las primeras medidas de rendimiento en el
Amiga emulado (`tools/debug/measure-fps.mjs`).

## 1. Coherencia (partida tras partida)

El selfplay incorpora `--verify`: en cada ply comprueba que

- la clave Zobrist incremental coincide con la recomputada,
- **todas** las jugadas legales pasan `make_move` → `unmake_move` dejando la posición
  exactamente igual (tablero, `side`, `castling`, `ep`, `halfmove`, `fullmove`,
  `castle_rook` y clave),
- la jugada elegida pertenece a la lista de jugadas legales.

Resultados:

| Configuración | Partidas | Verificación |
|---|---:|---|
| estándar (con `--swap`) | 3 | **OK** |
| Chess960 (`--seed 0`, 300 semillas) | 300 | **OK** |

Bugs del motor encontrados y corregidos durante esta campaña:

1. `search.hpp`: al abortar por presupuesto descartaba la mejor jugada parcialmente
   evaluada; con rebanadas pequeñas el juego podía quedar sin jugada. Ahora conserva
   la mejor parcial.
2. `board.hpp`: el enroque **Chess960 con solape** (rey y torre intercambian casillas,
   p. ej. rey f1 / torre g1) perdía la torre en `make`/`unmake`; posiciones así solo
   generaban 5 jugadas legales. Corregido con make/unmake conscientes del solape.

Cobertura de tests: HOST-143 (jugada parcial), HOST-181 (enroque con solape) y
HOST-187 (PGN + libro). Toda la batería de tablero (HOST-138…151 y HOST-179…187) pasa.

## 2. Estilos (200 partidas Chess960, `--swap`)

Blancas/negras alternan estilo para eliminar el sesgo de color.

| Bando | Victorias |
|---|---:|
| Agresivo (material/movilidad) | 85 |
| Posicional (peones/desarrollo) | 70 |
| Tablas | 41 |
| Sin acabar (límite de plies) | 4 |

Total por color: blancas 78, negras 77 → sin sesgo de color. Con esta evaluación tan
simple, el estilo agresivo rinde algo mejor.

## 3. Rendimiento en Amiga (A500, emulado)

Herramienta: `node tools/debug/measure-fps.mjs <demo> <config>` (mide **fps y
ciclos/frame en tiempo emulado** con el contador de ciclos del periférico de
depuración, independiente del ancho de banda del host; CPU = 7.09379 MHz).

Línea base (demo ligera, `games/101_go`): **49.75 fps**, 142 576 ciclos/frame
(1 línea/frame) → el emulador y la herramienta son correctos.

`demos/features/board/chess/amiga/123_chess_match` con una rebanada de búsqueda de 20 nodos/frame:

| Config | fps emulado | ciclos/frame | ciclos/nodo (aprox.) |
|---|---:|---:|---:|
| A500_debug | 0.83 | 8 554 540 | ~430 k |
| A500_release | 1.00 | 7 128 784 | ~360 k |

Conclusiones:

- El cuello es la **búsqueda**: `generate_legal` (make/unmake + detección de ataques
  por cada jugada pseudo-legal) y la ordenación dominan; release apenas mejora
  (~1.2×), señal de que el coste no es de instrucciones sino del trabajo por nodo.
- El coste por frame es, en la práctica, el coste de **una llamada a `search`**
  (~350–430 k ciclos), no de 20 nodos "puros": el grueso son la generación de jugadas
  en la raíz y la quiescence.
- Con 20 nodos/frame el motor rinde ~20 nodos/s en el emulador; a 200 nodos/frame una
  jugada (20 frames) tardaría minutos. Por eso la demo baja su presupuesto a
  32 nodos/frame × 8 frames.

Para observarlo en la demo, ampliar el asentamiento:

```bash
bash tools/run/run-demo.sh demos/features/board/chess/amiga/123_chess_match --warp --settle-ms 30000
```

## 4. Próximos pasos de optimización

1. **Generación de jugadas legales sin make/unmake**: filtrar por jaque/alfil de rey
   con máscaras de pines y ataques cacheados en vez de `make`+`is_square_attacked`
   por jugada.
2. **`is_square_attacked` incremental** o tablas de ataque por pieza (evitar escaneos
   de deslizantes en cada comprobación).
3. **Reutilizar la lista de jugadas de la raíz** entre iteraciones de profundidad
   (hoy se regenera) y no reiniciar la ID en cada frame de la demo: mantener el árbol
   con un `StopToken`/presupuesto en vez de llamar a `search` por rebanada.
4. **Reducir el TT** por motor si el acceso a Slow RAM penaliza (medir con el TT en
   Chip frente a Slow).
5. Repetir `measure-fps` tras cada optimización para cuantificar la mejora.
