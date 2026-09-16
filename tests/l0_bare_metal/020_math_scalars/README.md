# L0-020 · math_scalars (batería de matemáticas sin float)

Test **en hardware Amiga** que comprueba la matemática del engine con los **tres
escalares** y con operaciones **entre** ellos, **sin usar `float`/`double` en ningún
momento**. Es la red de seguridad del vocabulario genérico de `eng/core` + `eng/retro`:
si un cambio en `linalg`, `interp`, `geometry`, `spline`, `noise`, `minifloat_math` o
`minifloat_fixed` rompe un contrato, este test lo dice en el propio 68000.

```
                 ┌──────────────────────────────────────────────────────────┐
                 │  mismo algoritmo genérico (plantilla sobre el escalar S)  │
                 └──────────────────────────────────────────────────────────┘
                        │                    │                     │
                   ┌────▼────┐         ┌─────▼─────┐         ┌─────▼─────┐
                   │   MF    │         │    q12    │         │    q8     │
                   │ 16b flt │         │  Fixed<12>│         │ Fixed<8>  │
                   │  1|5|10 │         │  (4.12)   │         │  (8.8)    │
                   └─────────┘         └───────────┘         └───────────┘
                        └──────────── operaciones entre tipos ────────────┘
                          transform(Mat<MF>, Vec<q12|q8|q0>)
                          mul_fixed / mul_fix / mul_fix88
                          fixed_to_mf / mf_to_fixed / transform_point / project
```

## Qué ejercita

| Grupo | Funciones | Escalares |
|---|---|---|
| `linalg` | `Vec + -`, `dot` fusionado (2–4), `dot(fila,vec)`, `Mat*Mat`, `mul_norm`, `div_norm` | MF · q12 · q8 |
| `interp` | `clamp`/`saturate`/`lerp`/`inv_lerp`/`remap`, `smoothstep`/`smootherstep`, `ease_*_quad`/`_cubic`/`_back`/`_sine`/`_expo`, `smooth_damp`, `repeat`/`pingpong` | MF · q12 · q8 |
| `scalar_ops` | `min`/`max`/`abs`/`sign`/`move_towards`/`deadzone` | MF · q12 · q8 · q0 |
| `geometry` | `length(_sq)`/`distance`/`normalize`/`cross2`/`perp`/`rotate2`/`vscale`/`vlerp` | MF · q12 |
| `spline` | `hermite`/`catmull_rom`/`bezier2`/`bezier3` | MF · q12 · q8 |
| `noise` | `value_noise1/2`, `fbm1` (rango, determinismo, tileable) | MF |
| `minifloat_math` | `sqrt`/`exp`/`exp2`/`log`/`log2`/`pow`/`hypot`, `sin`/`cos`/`tan`, `atan2`/`asin`/`acos`/`wrap_angle`/`angle_diff` | MF |
| `minifloat_fixed` | `mul_fixed`/`mul_fix`/`mul_fix88`, `fixed_to_mf`/`mf_to_fixed`, `transform(Mat<MF>,Vec<fixed>)`, `transform_fix`, `transform_fix88`, `transform_point`, `project` | MF↔fixed |

Lo que **no** define un escalar queda fuera por diseño, no por olvido: el fixed no tiene
`sin`/`exp2`/`sqrt`, así que los easings trigonométricos, `smooth_damp`, `length`/
`normalize` y el ruido solo se prueban con MF (es el mismo contrato que documenta
`SCALAR_LIBRARY.md`).

## Cómo comprueba (sin float)

- El error de un escalar se mide **siempre en unidades de 1/4096** (el ULP de 4.12):
  - `q12`: el propio valor crudo.
  - `q8`: valor crudo ×16 (1/256 → 1/4096).
  - `q0`: valor crudo ×4096.
  - `MF`: `mf_to_fixed<12>(|d|)` (conversión entera, sin `float`).
- Cada caso declara su tolerancia en esas unidades y pasa si `max_err <= tol`.
- Los valores de referencia son **invariantes** (identidades y rangos: `f(0)=0`, `f(1)=1`,
  `M·I=M`, `sin²+cos²=1`, `repeat` dentro de `[0,len)`, …) más unos pocos **dorados**
  enteros (`sqrt(4)=2`, `exp2(1)=2`, `dot` exacto, rotación de 90°, …).
- **Cero `float`**: no hay `double`, ni `printf("%f")`, ni `libm`; el binario corre en el
  68000 con `-mcpu=68000`.

## Contrato de verificación

El test rellena `g_math_report` (estructura POD, símbolo C localizable en el `.map`) y lo
publica por el **canal lateral**:

```
offset  campo         tamaño
0       magic         u32   "MATH"
4       version       u32
8       case_count    u32
12      failed_count  u32
16      checks        u32
20      cases[...]    MathCase × 100

MathCase (32 B): name[20] · pass i16 · kind i16 · max_err i32 · tol i32   (big-endian)
```

`kind`: `0`=MF · `1`=q12 · `2`=q8 · `3`=q0 · `4`=mixto. `max_err`/`tol` en 1/4096.

Además, `g_eng_run_status.state = Ready` y `detail = (failed_count << 16) | case_count`; el
test espera ~240 frames y vuelve a Workbench (no toca el display).

## Cómo ejecutar

```bash
# Compilar
bash ./tools/build/build-demo.sh tests/l0_bare_metal/020_math_scalars --debug

# Compilar + ejecutar + verificar por canal lateral (falla si algún caso no pasa)
bash tests/l0_bare_metal/020_math_scalars/verify-math.sh

# Opciones: --skip-build, --port N, --wait-ms N, --verbose (imprime todos los casos), --keep
```

`verify-math.sh` compila el `.ts` a `dist/` con `npm run build` (si se toca el script) y
lanza `run-demo.sh`, que espera `READY`. Salida esperada:

```
[math] g_math_report v1: 50 casos, 276 comprobaciones
  (todos los casos dentro de tolerancia)
[math] fallos: 0/50
[math] OK: todos los casos (MF/q12/q8/q0 + inter-tipo) dentro de tolerancia.
```

## Por qué existe (y no es redundante con `tests/host/`)

Los tests `HOST-05x/06x` validan la misma matemática en PC usando `double` como
referencia: son rápidos y precisos, pero **usan punto flotante**. Este test es la
evidencia de que el vocabulario funciona en el **68000 real**, con la precisión del
escalar de 16 bits, sin depender de `libm` ni del compilador de PC. Es el complemento de
hardware de los tests host, no un sustituto.
