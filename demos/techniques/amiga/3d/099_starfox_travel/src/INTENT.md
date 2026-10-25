# Intencion de la demo 099 - foreground 3D por bitplanes

## Que hace

Muestra solo elementos de foreground en 3D sobre negro: Arwing, dos cazas, arco y edificio, con camara yaw/pitch/roll. No hay fondo, entrada, colision, puntuacion ni audio.

## Que pretende demostrar

El coste real de rellenar poligonos por bitplane en A500 con **4 bitplanes de display pero solo 3 renderizados** y **7 colores** (1..7; plano 3 a 0).

- **Ruta por objeto**: por cada objeto se calculan caras visibles, bbox y se lanza un `fill_polygons_by_plane_dirty` con 3 pasadas de Blitter. El bbox por objeto reduce clear/area fill frente a una pasada global.
- **Router de prueba** (`K099_FILL_MODE`): CPU exacto por cara, per-plane por objeto y automático con histórico de costes.
- El histórico usa geometría del frame actual (área, caras visibles) y coste real del frame anterior; calibra CPU/planos durante dos frames y decide con margen.
- El objetivo de la prueba es comprobar si “pequeño CPU / grande Blitter” merece la pena.

## Resultado

- Modo per-plane por objeto: **3,76 fps** (el mejor).
- Per-plane global: 3,26 fps.
- Router automático: 3,11 fps.
- CPU de referencia: 3,34 fps.
- El reparto pequeño-CPU no gana en esta escena: las naves también van mejor con per-plane por objeto. El CPU del edificio se cuelga.
- `VISION.md` ve movimiento y contornos limpios; flicker 7/220 dentro de baseline; 088/090 con la línea sin espera tienen capturas limpias; falta regresión completa.

## Límites

- El clear de bbox por objeto no compone con objetos detrás dentro del bbox; con fondo negro se acepta.
- El objetivo >6 fps sigue abierto.
- Sin commit.
