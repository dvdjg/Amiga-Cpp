# L1-005: clipping 3D en las cuatro esquinas

Sonda on-target que rota un cubo 45° de forma estática, lo recorta con `Camera3D`, proyecta las caras supervivientes y compara el relleno por plano del Blitter contra `fill_polygons_by_plane_cpu` en las cuatro esquinas. Cada esquina tiene una traslación independiente dentro del mismo bitmap.

## Qué valida

- Las cuatro posturas inclinadas cruzan simultáneamente los dos planos de cámara de su esquina y conservan una región visible.
- El resultado del Blitter conserva la cobertura por plano de la referencia CPU con la tolerancia documentada de ±2 píxeles del trazador (`../WinUAE-DBG/blitter.cpp:929-969`).
- No aparecen píxeles del cubo en las franjas exteriores a las cuatro regiones previstas ni escrituras en la banda del HUD.
- El conteo de caras recortadas coincide con las seis caras visibles esperadas (una en cada esquina superior y dos en cada esquina inferior).

La tolerancia no convierte el test en una prueba de identidad píxel a píxel: detecta huecos o excesos mayores que la convención de rasterizado del Blitter. La geometría, el color y las cajas se comparan por plano, no mediante una descripción visual genérica.

## Ejecución

```bash
bash tests/amiga/l1_backend/005_render3d_corners/verify.sh
bash tests/amiga/l1_backend/005_render3d_corners/verify.sh --edge-stress
bash tests/amiga/l1_backend/005_render3d_corners/verify.sh --edge-stress-cpu
```

El modo predeterminado valida las cuatro esquinas. `--edge-stress` congela la pose del frame f0473 de la 088 y compara CPU/Blitter en la ROI inferior derecha; el `detail` conserva el inventario de colores, las diferencias exactas por plano y las excepciones de ±2 px en el borde del bitmap. `--edge-stress-cpu` ejecuta la misma aserción y muestra en pantalla la página CPU para una comparación visual A/B.

Salida esperada del modo predeterminado: `[L1-005] OK: cuatro esquinas 3D recortadas; cobertura por plano dentro de ±2 px y sin píxeles fuera de región.` La pose de estrés informa además el número de diferencias exactas aunque la correspondencia por vecindad de ±2 px pase.
