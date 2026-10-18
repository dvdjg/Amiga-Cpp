# HOST-475: A0 de aventura — navegación por walkboxes y cámara de mundo

Test host de `engine/include/eng/adv/{navigation,camera}.hpp` (hito **A0** de [ROADMAP_ADVENTURE.md](../../../../docs/guides/roadmap/ROADMAP_ADVENTURE.md)):

## Qué comprueba

1. **`walkbox_route`** (`navigation.hpp`): la adyacencia se deriva de la geometría (bordes que se tocan o quedan dentro de una tolerancia); ruta entre cajas con punto de paso en el borde y llegada al destino; caso de una sola caja; destino fuera de toda caja → sin ruta; cajas con hueco de 1 px se conectan con tolerancia 2 y no con 0. Con **`s16` y `float`**.
2. **`Camera<S>`** (`camera.hpp`): dead-zone (no se mueve si el objetivo está dentro), desplazamiento mínimo al salir, clamp a los límites del mundo, y paneo temporizado (`pan_to`) que termina exacto y avanza de forma monótona. Con **`Fixed<s16,12>` y `float`** (los paneos usan `inv_lerp`/`mul_norm`/`smoothstep`, que exigen fracciones normalizadas).

## Salida de referencia

```
Adv A0 (navegacion + camara):
OK: adv A0 (walkbox_route + Camera, s16/float/Fixed)
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/adv/475_adv_navigation_camera
```
