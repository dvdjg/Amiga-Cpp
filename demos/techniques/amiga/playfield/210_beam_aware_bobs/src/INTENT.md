# INTENT — 210 BOBs de un plano

## Qué hace

Seis BOBs de bitmap rebotan sobre un framebuffer interleaved de 5 planos (320×256, fondo liso). Cada BOB es una hoja planar de 12×12 (arte 8×8 + 2 px de padding) que escribe **un solo plano** del destino; el color depende del plano elegido. La hoja se dibuja con un blit opaco `D=A`.

La demo usa `eng::scene::PlaneBobLayer`: los actores declaran posición, frame y plano; la capa lleva la huella previa. Con movimiento de 1 px/frame el padding cubre el borrado y cada BOB cuesta un job. La API permite agrupar, pero la demo no activa el scheduler beam-aware.

## Qué pretende demostrar

- Que seis BOBs opacos de un plano caben en **50 fps** en un A500 con 5bpp y sin save-under.
- Que el juego no necesita calcular palabras, guardas, módulos ni cajas: lo hace la capa.
- Que el camino estable actual es el **no agrupado**; la agrupación beam-aware conocida baja a ~42,7 fps y se documenta como pendiente, no como éxito.

## Elementos en pantalla

- Fondo navy liso de 320×256.
- Seis BOBs circulares de colores (rojo, verde, azul, amarillo, cian, blanco) rebotando y cambiando de frame.
- HUD de host con nombre, versión, frames, fps y `J/B/D` (jobs, arranques de Blitter, bobs diferidos).
- Canarios de memoria fuera del bitmap y de la hoja.

## Evolución temporal

1. **Init**: reserva Chip, construye la hoja, compone la Copper list y dibuja el primer frame.
2. **Frames**: los BOBs avanzan 1 px/frame, rebotan en los bordes y cambian de frame; la capa dibuja y conserva la huella.
3. **Régimen**: 49,9 fps estables, una secuencia de 30 frames sin banda, cortes ni estelas.
