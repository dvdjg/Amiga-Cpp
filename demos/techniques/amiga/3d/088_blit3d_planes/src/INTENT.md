# INTENT — 088_blit3d_planes

## Qué hace

La demo dibuja un cubo convexo de seis caras en un viewport de 320×232 sobre display planar OCS de dos bitplanes. El build estático fija una pose con `K_CLIP_CASE`; el build animado (`K_CLIP_ANIMATE=1`) rota el cubo y lo mueve por los cuatro lados y esquinas, con doble buffer.

## Qué pretende demostrar

- Que el clipping de cámara conserva las partes visibles de las caras que cruzan near o cualquiera de los cuatro laterales, y descarta caras completamente fuera.
- Que el culling por normal omite caras traseras y que las caras visibles se rellenan por bitplane sin triangular el cubo.
- Que el relleno Blitter conserva la cobertura de la referencia `fill_polygons_by_plane_cpu` dentro de ±2 px, tolerancia de la convención ONEDOT/EOR + FILL_XOR.
- En modo animado, que rotación, paso por los límites y repintado en dos páginas mantienen el objeto y la franja HUD legibles sin restos de la posición previa.

## Elementos en pantalla

- **Cubo convexo**: caras visibles azules (índice 1), rojas (2) y magentas (3) sobre fondo negro (0); las caras se recortan con el borde del viewport.
- **HUD inferior**: nombre y versión; el modo estático muestra pose, orientación, repetición y cantidad de buffers, mientras el animado muestra `PATH`, `B2`, `AN/R1` y el contador `Fxxxx`.

## Evolución temporal

- En modo estático, la geometría permanece inmóvil; `K_CLIP_CASE` selecciona centro, un lateral, una esquina o una pose completamente fuera. `K_CLIP_ORIENTATION=1` fija la inclinación de 45°.
- En modo animado, los tres ángulos avanzan por sumas por frame y la traslación recorre derecha, arriba, izquierda y abajo hasta cerrar la trayectoria. El contenido puede quedar parcialmente fuera del viewport durante los cruces.
- La validación usa HOST-481 para el clip de cámara, L1-003 para el rectángulo seguro del contorno y L1-005 para CPU/Blitter en esquinas y en una pose congelada de la animación. La evidencia y sus límites están en `../VALIDATION.md`.
