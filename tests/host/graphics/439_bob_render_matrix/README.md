# HOST-439 - `bob_render_matrix` (matriz de render de BOBs)

Test host de **todos los usos de BOB** del motor, validados por **píxeles exactos**: cada
caso construye un `FramePlan` con las APIs reales (`bob_draw`, `bob_erase_box`,
`bob_save_box`/`bob_restore_box`) y lo ejecuta con `tests/host/support/software_blitter.hpp`,
un **ejecutor software del `BlitJob`** que replica el encoder único
(`graphics/blit_queue.hpp:blitter_job_from`) y la semántica del Blitter (AHRM cap. 6;
contrastado con WinUAE-DBG `blitter.cpp:530-532`): minterm ABC, barrel shifter con hold
(`AFWM`/`ALWM` sobre la palabra cruda), módulos por canal (avance `words·2 + modulo`),
strides de plano y layouts planar/interleaved.

Casos (bitmap 128×64×4 planos + canarios de memoria antes/después):

| # | Caso | Qué blinda |
|---|------|------------|
| T1 | `OrBlob` alineado (`$FC`) | patrón completo, planos 2-3 intactos, sin escrituras fuera |
| T2 | `OrBlob` con shift 5 | barrel que cruza palabra y guarda de hoja |
| T3 | `Opaque` (`$F0`) | la copia tapa (los píxeles transparentes del patrón limpian el destino) |
| T4 | cookie-cut planar `$CA` (máscara separada) | imagen dentro de la máscara, destino preservado fuera |
| T5 | layout **interleaved** (destino y hoja) | un pase, filas físicas = alto×planos |
| T6 | cookie-cut interleaved **par** (`$CA`, un blit) | par `[imagen][máscara]` sin guarda, rabo transparente |
| T7 | `bob_erase_box` (`ClearRect`) | caja a cero, exterior intacto |
| T8 | `bob_save_box` + `bob_restore_box` | roundtrip save-under byte a byte |
| T9 | FastCopy con **padding** | la copia opaca tapa la posición anterior (sin rastro) |
| T10 | x < 0 | contrato: **no se dibuja** (el barrel no puede recortar a la izquierda); x=0 sí |
| T11 | bob de 4 planos (0..15) | los 4 planos y la paleta de valores |
| T12 | `And` (`$C0`) | minterm AND sobre relleno previo |

Hallazgos corregidos en el motor: `bob_draw` dibujaba los bobs con x<0 en posición errónea
(`x&15 + 16`, "wrap"); ahora se descartan como los de y<0 (mismo contrato documentado).

Es freestanding (sin hardware, sin heap, sin STL).

## Ejecución

```bash
bash tools/run-host-tests.sh tests/host/graphics/439_bob_render_matrix   # solo este
bash tools/run-host-tests.sh                                             # todos
```
