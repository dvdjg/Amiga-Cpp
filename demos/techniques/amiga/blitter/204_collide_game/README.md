# 204 - Juego mínimo con colisión pixel-perfect por Blitter

Bucle de juego que usa `blitter_collide` (AND `$80` + escaneo del resultado) por frame. Es el
consumidor de juego que faltaba de `blitter_collide` (antes verificado solo por el self-test de
077).

## Mecánica

- El **jugador** (un rombo de 16×8) se mueve en pasos de 16 px y rebota; el **obstáculo** (mismo
  rombo) está fijo.
- Cada frame se limpian dos **bandas** de 1 plano y se OR-ponen (`blitter_or_bobs`) la máscara del
  jugador y la del obstáculo a su posición (word-aligned).
- `blitter_collide(bandaA, bandaB, scan, …)` hace el AND con el Blitter y escanea: si hay solape
  **pixel-perfect** (no solo de caja), se enciende una barra de aviso (`flash`) unos frames.
- La visualización (jugador/obstáculo) va por `Surface::fill_rect`; la colisión usa máscaras de 1
  bit independientes, así que es pixel-perfect sobre el rombo, no sobre el rectángulo.

```
  mascara jugador ─┐
                   ├─ OR a su posicion ─► banda A ─┐
  mascara obstaculo┘                               ├─ blitter_collide (AND + scan) ─► flash
                   └─ OR a su posicion ─► banda B ─┘
```

## Verificación

- `analyze-screenshot.sh`: jugador (`0x0cf`) y obstáculo (`0xf33`) presentes.
- `analyze-sequence.sh`: el jugador se mueve y el **flash** (`0xff0`) aparece en algún frame
  (colisión detectada).

```bash
bash ./tools/test-regression.sh --demo demos/techniques/amiga/blitter/204_collide_game
```
