# INTENT - 216_attached_actors

## Qué hace

Tutorial del camino de actores *attached* end-to-end (**15 colores por sprite**): el juego da de alta actores en `eng::SpriteScene` con contenido de **4 planos de 16 px** (el par *attached* se deriva del arte; el contenido no lleva flags de hardware) y llama `emit`; el engine reparte **dos canales contiguos** por par con `SpriteAllocator` (par 0+1, 2+3…), cocina las dos estructuras DMA del par (`cook_attached_pair`: canal par = planos 0-1; canal impar = planos 2-3 + `ATTACH`) con la DATA del frame vigente de la animación, y publica **dos placements** por actor (misma POS). El juego solo arma los placements con `SpriteManager::emit_placements_into` (primera config en una línea temprana y rearmes por franja).

## Qué pretende demostrar

- Que el camino de actores **deriva el par attached del arte** sin flags en el contenido, y que las gemas (4 planos, animadas) y las chispas (2 planos) pasan por la MISMA fachada.
- Que los actores **fijados a su canal** (`assign_rank` + `preferred_channel`) pueden recorrer toda la pantalla y solaparse en Y sin que el allocator les cambie el par de color (cada par tiene sus `COLORxx`).

## Elementos en pantalla

- **Fondo estructurado** (4 planos): 16 bandas horizontales de color + tablero suave de dos tonos.
- **Dos gemas** (4 planos, animación de 2 frames) y **dos chispas** (2 planos), cada una con su par de color, recorriendo **toda la pantalla en x e y** con fases distintas (triángulos por tabla, sin `*`/`/`).
- **Depuración**: ventana de debug (run-status `sprites << 8 | degradados` + contadores `fps10`, `comp_ok`, `comp_bad`).

## Evolución temporal

1. **Init**: fondo + hojas (gemas/chispas) + pool de cocinado + 4 actores + composición + copperlist (doble buffer); READY con `sprites << 8 | degradados`.
2. **Frames**: los actores avanzan sus trayectorias x/y (fases distintas), las gemas además avanzan su animación; `emit` recompone (pares attached recocinados con la DATA del frame) y el copper se re-emite; swap de COP1LC.
3. **Cada composición**: self-check de memoria — `sprites_hw + degradados == 6` (2 gemas × 2 placements + 2 chispas; `comp_bad` debe quedarse en 0) y `fps10` cada segundo.
