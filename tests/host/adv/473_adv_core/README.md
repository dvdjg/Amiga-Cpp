# HOST-473: núcleo de contenido de aventura (`eng::adv`)

Test host de `engine/include/eng/adv/{types,walkbox,hotspot,room}.hpp`: la capa de **contenido** de la aventura gráfica — el destino del `SIM v2` que produce el pipeline de importación (diseño: [ADVENTURE_LAYER.md](../../../../docs/engine/architecture/ADVENTURE_LAYER.md); plan: [ROADMAP_ADVENTURE_IMPORT.md](../../../../docs/guides/roadmap/ROADMAP_ADVENTURE_IMPORT.md)).

## Qué comprueba

1. **`Walkbox`**: `valid()` descarta el centinela SCUMM de las cajas hueco (cuatro esquinas en −32000); `contains()` acepta cualquier sentido de giro (cuadrilátero convexo, borde incluido); `closest_point()` devuelve el punto de borde más cercano y su distancia al cuadrado (base del *walk-to* fuera de caja).
2. **`box_at()`**: gana la **última** caja que contiene el punto (el frente, mismo orden que el original); las cajas centinela se saltan; fuera de todo → `kWalkboxInvalid`.
3. **`Hotspot`/`hit_test()`**: rectángulo con bordes incluidos y `min`/`max` en cualquier orden; gana el último (frente); sin pareja → `kInvalidHotspot`.
4. **`RoomContent`**: id válido + dimensiones positivas (`ok()`), y los campos son **vistas** (`Span`), no copias.
5. **Generalidad de escalar** (AGENTS §1.11): toda la geometría se ejercita con **`eng::s16` y `float`** en el mismo test.

## Salida de referencia

```
Adv core (walkbox, hotspot, sala):
OK: adv core (Walkbox/box_at/Hotspot/hit_test/RoomContent, s16+float)
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/adv/473_adv_core
```
