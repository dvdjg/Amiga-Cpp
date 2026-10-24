# 096 - Silhouette swarm (juego de props planos)

Demo de juego para mostrar dónde sí sirve `render_silhouette`: una formación de rocas/props **de color plano** (8 estáticos, dibujados una vez por buffer) y dos naves dinámicas. La silueta hace **un fill por objeto**; la ruta por cara hace uno por cara. El HUD muestra `SIL` y el A/B real está en `VALIDATION.md`.

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/3d/096_silhouette_swarm --release
bash ./tools/run/run-demo.sh demos/techniques/amiga/3d/096_silhouette_swarm --config A500_release --settle-ms 700 --sequence-frames 4 --sequence-interval-ms 500
```

Ruta por cara para comparar: `EXTRA_DEFINES=-DK096_FACE=1` (misma escena, 2 naves dinámicas). La silueta es la ruta por defecto porque el contenido es de un color plano; no se usa para caras con material ni patrones.

Estado release: **26,9 fps / 1,86 campos** (263.345 ciclos; mínimo 25,31). Ruta por cara: 17,7 fps. La diferencia demuestra la ventaja de la silueta en este tipo de juego.