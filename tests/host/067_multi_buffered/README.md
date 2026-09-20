# HOST-067: doble/triple buffer de display en `scene::compose`

Test host del **doble/triple buffer de display** en el modelo de escena
(`engine/include/eng/graphics/composition/compose.hpp`): `SceneResources.buffers` reserva N bitmaps
y `Scene::commit()` repunta los `BPLxPT` al buffer trasero (swap sin `COPJMP1`). Sustituye a
`MultiBuffered<Driver, N>` y lo observa con `Scene::display_plane_uses` (sin recorrer la
copperlist con punteros crudos).

## Qué comprueba

1. `buffers = N` reserva N bitmaps **distintos** y arranca dibujando en el trasero.
2. `commit` repunta los `BPLxPT` al buffer publicado y rota (`0→1→0` con `N=2`).
3. `N=1`: un solo buffer; `commit` no cambia de buffer.
4. `reverse_ptrs` registra los parches incluso en orden inverso (caso fire-rgb).

Todo con un backend de pega que solo registra qué copperlist se toma/instala: sin hardware ni
RAM Amiga.

## Salida de referencia

```
OK: scene::compose doble buffer (parcheo de BPLxPT en commit).
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/067_multi_buffered
```
