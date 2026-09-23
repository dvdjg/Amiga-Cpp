# HOST-230: GUI G7 — compositor con backing store

Test host de la fase **G7**: `eng/ui/compositor.hpp` + `eng/ui/backing.hpp`. Ver `ROADMAP_GUI.md`
(G7) y `GUI_LIBRARY.md` §14.

Cada ventana tiene su `WindowBacking` (lienzo planar contiguo + `Surface`); la pantalla solo
**compone** rectángulos de atrás hacia delante. Se leen los píxeles de la pantalla del mapeo planar.

## Qué comprueba

1. **Composición** fondo → frente: la ventana de delante tapa a la de detrás; fuera = escritorio.
2. **`move_window`**: daña origen y destino (una región fusionada) sin marcar `needs_repaint` de
   ninguna ventana (no se repintan vecinas).
3. **`resize_window`**: falla si no cabe en el backing; si cabe, marca **solo** su ventana.
4. **Pool**: al llenar `kMaxWindows`, `add()` devuelve `nullptr`.

## Salida de referencia

```
OK: GUI G7 (compositor + backing) validado.
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/ui/230_ui_compositor
```
