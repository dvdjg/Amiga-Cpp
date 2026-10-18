# VALIDATION — demo 302 `hud_band`

Informe de validación de la fase **H1** del [`ROADMAP_HUD.md`](../../../../../docs/guides/roadmap/ROADMAP_HUD.md). Intención: [`src/INTENT.md`](src/INTENT.md) · Visión canónica: [`src/VISION.md`](src/VISION.md).

## Qué se valida

Un juego mínimo (campo a 4 planos con bloques estáticos y un actor que rebota) con un **HUD de acción en franja propia de 48 líneas a 2 planos con paleta propia** (blanco/amarillo/rojo) compuesta con `ModeSwitchZone` en la línea de display 208: contador de puntos (6 dígitos), medidor de energía lineal (que pasa a rojo bajo el 25 %) y 4 *pips* de vidas, más la línea de depuración obligatoria `HUD BAND vX.Y.build F NNNNN`.

## Medidas (A500_debug)

| Medida | Valor | Herramienta |
|---|---|---|
| fps / ciclos por frame | **49,67–49,77 fps · ~142,7k ciclos · 1,005 campos** (3 medidas) | `node tools/debug/measure-fps.mjs ui_amiga_302_hud_band A500_debug --json` |
| Suelo del engine (referencia) | 49,92 fps · 142.102 ciclos (demo `122_circles`) | idem |
| CPU libre por frame (perfil) | media **48 %** libre; ningún frame agotó el campo | `node tools/debug/winuae-profile.mjs ui_amiga_302_hud_band A500_debug 60` |
| Presencia por color (franja) | blanco 7.332 px constante; amarillo 3.332→2.760 (barra vaciándose); rojo 0 en esa ventana | `node tools/analyze/check-elements.mjs out/run/.../sequence "#FFFFFF,#FFFF00,#FF0000" --expect "#FFFF00=200"` |
| Verificador de la franja | texto/marco y amarillo presentes; 10/11 pares consecutivos con cambio (barra de energía por deltas) | `node tools/analyze/verify-hud-band.mjs --sequence-dir out/run/.../sequence` |
| Flicker | **1 candidato** (movimiento del actor, SSIM ≥ 0,996) y VLM sin anomalía; `flicker-baseline.json` declarado (`max_candidates: 1`) | `tools/test-regression.sh --flicker --require-flicker-ok --demo demos/features/ui/amiga/302_hud_band` |

**Optimización medida (A/B, una variable):** el HUD pintado por CPU con repintado completo costaba ~460k ciclos/frame (11,9 fps). Se aplicó, por este orden: presentador con `draw_text_words` para celdas alineadas (HUD y texto por palabras), **borrado por deltas** (la barra solo repinta el tramo que cambia), contador a 5 Hz, **actor por Blitter** (`fill_box`, `FillRect` por plano) y línea de versión de **longitud fija sin borrado** (el clear de 318×8 cada segundo hacía que el 2 % de los frames ocuparan 2 campos). Resultado: 1,005 campos/frame. El estado del juego (`Hud<>` ~7 KB) vive en **BSS**: en la pila local de `main` (16 KB de pila Amiga) desbordaba y corrompía el modelo.

## Visión (protocolo A/B/C, `DEMO_VISUAL_DEBUG.md` §6.4)

Frames: `out/run/ui_amiga_302_hud_band/A500_debug/sequence/frame_000_f0029.png` … `frame_011_f0040.png` (12 capturas frame a frame).

### A — Inventario sin contexto (frame 5, respuesta cruda, resumen literal)

> «Rectángulos púrpuras (alto y ancho variado): al menos 5. Uno largo vertical en la izquierda… Un rectángulo azul claro (pequeño)… Una barra amarilla horizontal: forma parte del indicador de energía… Texto y números blancos y amarillos: "SCORE 000003", "ENERGY" con barra, "LIVES" con cuatro cuadrados amarillos, "HUD BAND v1.0.4604 F 00000"… No se observan discontinuidades, cortes ni zonas incoherentes… No hay ruido, bloques extraños ni texto raro.»

### B — Dinámica de la barra de ENERGY (frames 0, 6 y 11, una imagen por llamada)

> frame 0: «…una barra horizontal blanca, de la cual una pequeña parte (aproximadamente un cuarto) está llena de amarillo».
> frame 6: «Muestra una barra de progreso amarilla que está llena aproximadamente al 50 %».
> frame 11: «Muestra una barra de energía que está parcialmente llena (aproximadamente un tercio)».

Lectura coherente con la intención (la energía baja de 60 a 0 y vuelve; la ventana capturada cae en la bajada: ~52 % → ~42 % → ~33 %). Sin saltos ni desapariciones.

### C — Verificación dirigida (frame 6)

> (1) franja con marco blanco: presente; (2) etiquetas SCORE/ENERGY/LIVES: presentes; (3) contador de 6 dígitos: presente («000003»); (4) barra de energía: presente, «no se observa rojo en esta imagen» (correcto: en ese frame la energía supera el 25 %); (5) cuatro pips: presentes, todos amarillos (ninguna vida perdida en esa ventana); (6) línea de versión: presente; (7) cuadrado claro en el campo: presente. «No se observa parpadeo ni corrupción.»

La dinámica fina la cubren las capas deterministas (verificador de franja + flicker + `check-elements`), no la visión de una captura.

## Conclusiones

- La franja se compone con su modo propio (2 planos + paleta) y el HUD se repinta por deltas; el coste entra en **1 campo** (1,005 de media, dentro del ruido del medidor frente al suelo de 122).
- Sin *tearing* observable: la escritura de la franja ocurre tras el VBlank y el beam tarda ~208 líneas en llegar a la banda; el gate de flicker declara 1 candidato (actor) y el VLM no ve anomalía. **Decisión H1**: la franja no necesita doble buffer mientras el contenido se escriba en la ventana de VBlank; la vía de doble buffer por punteros queda documentada en `HUD_SYSTEM.md` §6.3 para contenido que lo requiera.
- Sin errores de contenido en la visión (A y C coherentes; A manda).

## SIN VERIFICAR

- Build **release** (solo se midió `A500_debug`).
- Estabilidad en ejecución larga (>20 s) y con Fast RAM presente.
- La ruta del atlas de iconos (`IconAtlas`/`Blit`) no se usa en esta demo (llega en H6); el estado `warn` rojo se validó por color en la secuencia de 250 ms (rojo 256–976 px), no en la ventana frame a frame.
- Interacción (H2/H3): la demo no consume ratón.
