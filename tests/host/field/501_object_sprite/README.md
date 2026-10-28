# HOST-501: sprite de objeto de mapa animado

Valida `eng::map::map_object_sprite` y `object_frame_at_tick` sin WinUAE:

- construye un `Sprite` planar con el layout de `object_sheet.bin` (fila con guarda);
- conserva ancho, alto, frames y stride del frame;
- calcula el frame por tick con las duraciones de `MapObjectAnimationView`;
- el objeto puede colocarse en píxeles arbitrarios con `MapSprite`/`MapSpriteCache`.

Ejecución: `bash tools/run-host-tests.sh tests/host/field/501_object_sprite`.
