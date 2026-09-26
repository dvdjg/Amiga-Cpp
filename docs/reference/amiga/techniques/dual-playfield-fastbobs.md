# Dual playfield fast BOBs (copia con padding)

- **Referencia:** [Dual Playfield ‘Fast Bobs’](https://www.powerprograms.nl/amiga/dpl-fastbobs.html) (Jeroen Knoester / Roondar; idea de Mega Typhoon, Bernhard Braun).
- **Idea:** con **dual playfield**, el playfield de fondo (PF2) lleva el escenario y el frontal (PF1) arranca **vacío** (color 0). Los BOB viven en PF1, así que no hay fondo real que guardar/restaurar: se dibujan con una **copia** de su bitmap (que incluye **padding de color 0** alrededor) y el propio padding limpia lo que quede del frame anterior. Dibuja **y** limpia en un solo blit.
- **Coste:** un blit de **copia** (minterm `$F0`, canales A+D: 2 DMA) por BOB, en vez de cookie-cut (`$CA`, A+B+C+D: 4 DMA) + save/restore. Ganancia típica ~2,5×–4× en número de BOB (A500). Sin `SaveBuffer` por objeto.
- **Límites:** el desplazamiento por frame **no** puede superar el padding (si lo supera quedan restos); los BOB **no** pueden solaparse (la copia del segundo borraría al primero); el Blitter trabaja a palabras de 16 px + shift, así que el rectángulo copiado se alinea a palabra. Exige un PF frontal dedicado y vacío de origen.
- **AHRM:** Blitter (minterms) + Playfield dual (`DBLPF`).
- **Lab:** el engine lo expone como política en `eng/scene/bobs.hpp` (`FastBobLayer`); ficha hermana del BOB interleaved de un solo blit en [interleaved-bob-single-blit.md](interleaved-bob-single-blit.md).

## Mecanismo

El bitmap del BOB incluye padding de color 0 cuyo ancho por lado es **mayor o igual** al desplazamiento máximo por frame en la dirección opuesta (si un BOB puede moverse 3 px a la izquierda y 2 a la derecha, necesita ≥ 2 px de ceros a la izquierda y ≥ 3 a la derecha; igual en vertical). Con doble buffer, el padding se dobla (el rectángulo previo a limpiar es el del buffer que se va a escribir).

Al copiar el bitmap (con padding) en la posición nueva, los píxeles de padding sobrescriben la parte del BOB anterior que ya no se solapa con el nuevo:

```text
 Frame previo        Posicion nueva      Resultado tras la copia
 .........           .........           .........
 ..xxx....           .. xxx...           ...xxx...
 ..x.x....           .. x.x...           ...x.x...
 ..x.x....           .. x.x...           ...x.x...
 ..xxx....           .. xxx...           ...xxx...
 .........           .........           .........
```

Los puntos son color 0. El espacio vacío de la fuente actúa de borrador de lo que quedó fuera.

```text
  PF2 (fondo, escenario)      PF1 (frente, BOBs, arranca vacio)
  ──────────────────────      ────────────────────────────────
  se escribe cada frame       solo contiene los BOB del frame actual
  (scroll/tiles)              copia con padding = dibuja + limpia
```

## Degradación cuando hay conflicto

Si el movimiento supera el padding, o dos BOB comparten área de memoria en el mismo frame, la copia pura falla (restos o borrado del otro). Entonces esos BOB se dibujan por el camino lento: **clear rectangular del área previa + cookie-cut** (`$CA`, con transparencia), mientras el resto sigue por el camino rápido. Por frame:

1. Limpiar (clear rectangular) las áreas previas de los BOB que degradan.
2. Copiar (con padding) todos los BOB no conflictivos: dibujan y limpian a la vez.
3. Dibujar con cookie-cut los BOB conflictivos.
4. (Opcional) sprites hardware / copperlist.

Muchos juegos evitan el solape por diseño (trayectorias de enemigos que no se cruzan, jugador como sprite hardware) y solo unos pocos BOB usan el camino lento.

## En el engine

- **Política de aplicación** (el juego solo mueve actores): `eng::scene::FastBobLayer` (`engine/include/eng/scene/bobs.hpp`). Se le da la hoja **con padding** (`set_sheet`, copia opaca) y la hoja **con máscara** de degradación (`set_slow_sheet`, cookie-cut `BobMaskPack::InterleavedPair`), y por frame se llama `emit(plan, scene.bob_target())`. La capa decide por actor: copia con padding si no se movió más que el padding ni se solapa con otro; si no, clear del área previa + cookie-cut. Verificado en el test host `HOST-355`.
- **Copia (camino rápido):** `BobDraw::Opaque` sobre destino intercalado (un blit `$F0`, `height = alto × planos`).
- **Limpieza:** `eng::scene::clear_box(plan, target, x, y, w, h)` (un `ClearRect` intercalado).
- **Degradación:** `BobDraw::CookieCut` con `BobMaskPack::InterleavedPair` (un blit `$CA`).
- **Composición DPF 3+3 + bandas:** el playfield dual lo materializa `eng::field::XLimitedComposer` (`compose(pf1, pf2)`, `DBLPF`/`BPLCON2`), y las bandas de distinta geometría (p. ej. una franja inferior de 0 planos para efectos *copper chunky*) van por `eng::graphics::ModeSwitchZone` (`planes = 0` desactiva el DMA de planos) sobre el `copper::Scheduler`.
