# HOST-498 — lógica pura de Caverna

Valida la lógica de juego separada del engine en `games/500_caverna/src/caverna_gameplay.hpp` y la regresión de datos del mapa PF2:

- colisión de una caja 16×16 contra las caras de un tile sólido (interior, derecha, superior y aire);
- cámara con dead-zone: quieta dentro de la banda, sigue al jugador fuera y se acota al mundo;
- clasificación del contacto jugador/enemigo: `Stomp` al caer encima, `Damage` lateral, `None` sin solape;
- **regresión del bloque de staging**: las filas 14..19 del mapa PF2 deben estar vacías (el walk horizontal las lee y, con contenido, las revela arriba); la fila 13 sigue siendo terreno y la plataforma de (20,8) es sólida.

## Ejecución

```bash
bash tools/run-host-tests.sh tests/host/scene/498_caverna_gameplay
```
