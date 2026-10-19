# VALIDATION - 305_hud_overlay

## Contenido (regla 25) y H4/H6

Campo en movimiento (fondo 640×256 con scroll por blit de ventana + actor en lissajous) con **dos overlays** de políticas distintas (`GameRedraws` y `Backing` con buffer Chip propio), pintados con las **operaciones del modelo** aplicadas por `apply_hud_ops` sobre superficies de la escena, **skins** de medidor y contador cambiados en caliente, y **degradación por presupuesto** medida sobre la sección HUD con la **escalera del engine** (`eng/hud/budget.hpp`, H4: histeresis 7/8, telemetria `changes()`).

## Medidas (A500_debug, ventana de 10 s; espera de VBlank activa)

| Métrica | Valor | Fuente |
|---|---|---|
| `vbl` / `frame` | **602 / 420** → ratio **0,70** (demo pesada: scroll + paneles + backing; la cadencia honesta se mide contra el reloj de VBlank del mini-SO, DT-036) | contadores 0/1 |
| `ov_ok` / `ov_bad` | **178-225 / 0** (ops de ambos paneles aplicadas) | contadores 6/2 |
| `bk_bad` | **0** (restauraciones del backing del panel B fallidas; el recuento de éxitos se retiró para liberar el slot de `ov_ok`) | contador 7 |
| `blit_bad` | **0** (blits del juego) | contador 4 |
| `deg_changes` | **351-420** (la escalera degrada/recupera con el coste real; no se queda clavada) | contador 5 |

**Evidencia de memoria** (gate `analyze-sequence.sh`): `vbl >= 400`, ratio `frame`/`vbl >= 600/1000`, `ov_ok >= 1`, `ov_bad = bk_bad = blit_bad = 0` y `deg_changes >= 2` — todos los mecanismos (ops de overlay, restauración por *backing*, blits y degradación) funcionan, sin depender del vídeo.

## Visión

`src/VISION.md` (prompt canónico). **Cautela:** en las capturas de este entorno el vídeo puede servirse **congelado** (DT-023/035; se observó el frame pre-render con todo en paleta[0]); el verificador `verify-hud-overlay.mjs` detecta el congelado (frames idénticos) y lo declara no evaluable, dejando la memoria como gate duro. La composición visual (paneles + scroll + actor) queda **SIN VERIFICAR por visión** hasta una captura con refresco.

## Notas

- El presupuesto se mide **solo la sección HUD** (medir el frame entero incluía los blits del juego y la degradación quedaba clavada).
- El blit de bitmap exige `plane_count == destino` (4) y `destination.x` múltiplo de 16: el actor avanza en pasos de 16 y las hojas usan 4 planos (2 con contenido).
- La Surface del overlay usa coordenadas **absolutas** con su rect como clip (los ops relativos quedan fuera y `apply_hud_ops` los descarta).
