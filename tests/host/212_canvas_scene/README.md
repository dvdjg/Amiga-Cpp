# HOST-212: `CanvasScene` (driver planar con `Surface`)

Test host de `eng::graphics::drivers::CanvasScene`: cierra el hueco «efecto → dibujo con
`Surface`» envolviendo un `field::CanvasPlayfield` (bitmap planar interleaved) y
exponiendo una `Surface` de dibujo más la copperlist de display.

## Qué comprueba

1. **`init`** reserva bitmap (interleaved) + copperlist en Chip RAM y reconstruye la lista
   (`ok()`, `copper_words() > 0`).
2. **`surface()`**: `Surface::fill_polygon` pinta sobre la escena; se lee el color del
   bitmap interleaved (interior con el color pedido, exterior vacío).
3. **Contrato de driver**: `takeover`/`install` instalan el puntero de la copperlist
   (`GraphicsDriver`/`DisplayDriver` satisfechos, `static_assert`).
4. La copperlist usa los módulos interleaved del `Playfield`
   (`BPL1MOD = planes*row − row`) y los punteros BPLx por plano.

## Salida de referencia

```
OK: CanvasScene (Surface sobre CanvasPlayfield + copperlist interleaved).
```

## Ejecutar

```bash
CXX="/c/.../g++.exe" bash tools/run-host-tests.sh tests/host/212_canvas_scene
```
