# 066: showcase de BOBs + Sprites HW

Vitrina técnica de **todos los usos de BOB** del motor (la matriz de HOST-439 en vivo) más
sprites hardware. Referencia por píxeles exactos:
`tests/host/graphics/439_bob_render_matrix`.

## Zonas y técnicas

| Zona | Técnica | API |
|------|---------|-----|
| A | orbes rebotando: Or ($FC) + borrado por caja | `bob_draw(Or)` + `bob_erase_box` |
| B | deslizadores con padding **FastCopy**: copia opaca ($F0) que tapa la posición previa sin borrado | `bob_draw(Opaque)` |
| C | caminante **cookie-cut planar** ($CA con máscara en plano aparte) sobre damero: la máscara preserva el fondo; el borrado previo limpia solo los planos 0-1 | `bob_draw(CookieCut)` + máscara |
| D | patrullero cookie-cut + **save-under** (restaura el damero byte a byte) | `bob_save_box`/`bob_restore_box` |
| E | barras que borran su caja y se redibujan | `bob_erase_box` |
| F | joya multicolor de 4 planos con padding | `bob_draw(Opaque)` |
| G | **dos naves HW** (cada una par adjunto DAT/DATB = 15 colores) armadas por Copper | `SpriteManager::arm_object` |

Fondo: degradado de `COLOR00` por bandas + arte estático (marcos, dameros, iconos, estrellas) en
los planos 2-3 dibujado una vez.

## Estado

- **fps**: 34 fps (síncrono, `execute_frame_plan`) con la carga actual — ver
  `docs/debugging/investigaciones/066-async-blit-irq-bajo-el-so.md` para por qué el camino async
  (que da 49.9 fps) no es viable bajo un SO vivo y qué queda pendiente para llegar a 50.
- Render de la matriz de bobs: verificado con visión (secuencia) — orbes, deslizadores, cookie-cut
  sobre damero, save-under, barras y joya OK; **sprites HW con artefactos** (pendiente, misma
  investigación).
- Validación de secuencia + informe de visión: `docs/validation/066/` (cuando se cierre el render).

## Ejecución

```bash
bash tools/build/build-demo.sh demos/features/engine/amiga/066_bob_showcase --debug
bash tools/run/run-demo.sh demos/features/engine/amiga/066_bob_showcase --keep-running
node tools/debug/measure-fps.mjs engine_amiga_066_bob_showcase A500_debug --json
```
