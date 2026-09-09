# Demo 062: fuego 32 colores + c2p (port de effects/fire-rgb)

Importa el efecto `05-fire-rgb` de `demoscene-repo-orig` adaptado a la nueva
estructura del engine. **Fiel al original** en geometría: el fuego se calcula a
80×64 y se muestra a 320×256 (escalado 4×4 pixelado).

## Decisión de color: 32 colores (5 bitplanes), sin HAM

El original usa `MODE_HAM` (con una LUT `dualtab` que mapea cada valor de fuego a
un patrón HAM). Esta réplica usa **32 colores directos (5 bitplanes)** con una
paleta de degradado (negro→rojo→naranja→amarillo→blanco), que se ve igual de suave
y evita el coste y los artefactos del HAM (que necesita píxeles intermedios para las
transiciones de color). Verificado: 25 tonos distintos en la captura, degradado
suave (no a bandas).

## Flujo por frame

1. `generate_fire()`: buffer chunky 80×64, índice 0..31.
2. `c2p_1x1_naive(..., planes=5)`: chunky → planar 80×64 (5 planos).
3. `scale4x()`: expande a 320×256 (byte ×4 horizontal, línea ×4 vertical) con
   escrituras `u32` nativas (equivalente al escalado del original).

El `c2p_1x1_naive` (N planos, en `c2p.hpp`) es correcto por construcción y rápido
para 80×64; para pantalla completa de 5/6 planos se portaría el `c2p_1x1_5`/`_6` de
Kalms (merge optimizado). `c2p_1x1_4` (Kalms) sigue para 4 planos.

## Build & run

```bash
tools/build/build-demo.sh demos/amiga/062_fire_c2p --clean
tools/run/run-demo.sh       demos/amiga/062_fire_c2p --warp
```

## Criterio de aceptación

- Compila y llega a `Ready`.
- Captura: llamas pixeladas (bloques 4×4) con degradado suave rojo→amarillo,
  subiendo desde abajo y ocupando todo el ancho.
