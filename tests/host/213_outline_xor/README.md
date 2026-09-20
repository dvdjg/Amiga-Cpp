# HOST-213: contorno EOR + area fill XOR (`retro`)

Test host de `eng::retro::flat_shade_xor` (`eng/retro/flat_shade_xor.hpp`): la **técnica
Amiga** de relleno por contorno EOR + area fill XOR (la del original de la demo 116),
encapsulada en `retro` para no meterla en `mesh3d` (que es genérico/puro).

## Qué comprueba (secuencia de Blitter sobre un backend mock)

1. **Comunes una vez**: `blitter_lines_eor_begin` se llama exactamente 1 vez.
2. **Horizontales descartadas**: la arista `y0 == y1` no genera `prepare` (paridad).
3. **Una línea por plano del color**: `color & (1<<p)` decide en qué planos se pinta.
4. **Un solo area fill XOR** que cubre los planos y la pantalla pedidos.
5. El número de líneas-plano lanzadas se devuelve.

## Frontera de responsabilidades

- **`mesh3d`/`mesh_renderer`**: *qué* caras y con *qué* color (`mesh_render_poly_filled`,
  `Surface::fill_polygon`) — genérico.
- **`retro::flat_shade_xor`**: *cómo* lo pinta el Blitter Amiga (líneas EOR + area fill
  XOR + paridad) — específico de hardware, no lo conoce `mesh3d`.

## Salida de referencia

```
OK: flat_shade_xor (contorno EOR por plano + un area fill XOR).
```

## Ejecutar

```bash
CXX="/c/.../g++.exe" bash tools/run-host-tests.sh tests/host/213_outline_xor
```

## Validación en hardware

La técnica es **frágil a la paridad**: su gate válido es **visual** (`freeze-diff` /
secuencia + Ollama), no la cobertura. `verify-116` da PASS con la imagen rota.
