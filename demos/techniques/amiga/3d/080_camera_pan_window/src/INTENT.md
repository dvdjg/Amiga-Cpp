# INTENT — 080_camera_pan_window (paneo de cámara con frames intermedios)

## Qué hace

Una sala 3D poligonal (tubo: suelo, techo, paredes y pared del fondo) con cuatro paneles verticales de colores en el centro del pasillo. La cámara gira en yaw (paneo triangular, ±29 grados) sobre un framebuffer de 352×208 con el viewport de 320 centrado (margen de 16 px a cada lado). La pasada completa rellena la escena por plano con el Blitter; los fotogramas intermedios solo mueven la ventana de display. El HUD de la franja inferior muestra versión, contadores Full/Fake, el offset del frame, el paneo y el número de fakes por pasada (K).

## Qué pretende demostrar

El **contrato de frames intermedios por paneo** de la técnica `docs/reference/amiga/techniques/camera-pan-fake-frames.md` (fase F1 de `ROADMAP_3D_PRESENTACION.md`):

- **Full** (render real, base ≥12,5 Hz): la pasada completa transforma y proyecta la sala + los paneles y rellena el framebuffer ENTERO (márgenes incluidos) con el relleno por plano del Blitter (`fill_polygons_by_plane`). La ventana se centra (S=16) parcheando `BPLxPT`.
- **Fake** (display ≥25 Hz): no se re-renderiza nada; solo se parchean `BPLxPT` (coarse) y `BPLCON1` (fino) con la convención de `playfield_scroll.hpp` para mover la ventana al offset del paneo, mostrando el contenido de los márgenes. El Fake es **exactamente** el Full anterior desplazado (contrato de píxel; ver `VALIDATION.md`). Entre dos Fulls caben **varios fakes** (K), decididos por el coste medido.
- La política `eng::graphics::pan_window.hpp` (margen 15 px) produce fakes con el offset exacto en la fase lenta (1 px/frame) y fuerza Full cada frame en la fase rápida (16 px/frame: el offset supera el margen y el paneo ya no es válido); el número máximo de fakes seguidos lo fija `eng::graphics::render_pacing.hpp` (EMA del coste del Full en campos PAL, tope K≤3). El reloj es determinista por índice de frame (telemetría en `g_eng_run_status.detail`).
- **Mezcla de colores con damero a nivel de píxel** (opción `-DK080_PATTERN`; `eng::graphics::fill_pattern.hpp`, HOST-474): por defecto la sala va **SÓLIDA** (colores planos, menor coste de relleno). Con el flag, los planos de la sala llevan el damero 1×1 y su inverso (`fill_dither_plane_cpu`; 2 filas por plano) y el relleno por plano los aplica por fases (`D = A & B`) tras el area fill, fundiendo dos índices base por cara. La inestabilidad que mostró la fase de patrón era la espera de VBlank de la familia 212 (resuelta con `run_frames_polling`): con el bucle por sondeo el patrón vuelve a ser estable (4/4 arranques). Los paneles no llevan patrón (sólidos) y el marcador naranja conserva su color único.

## Elementos en pantalla

| Elemento | Color | Papel |
|---|---|---|
| Suelo | marrón (sólido; damero 1×1 de 1/0 con `-DK080_PATTERN`) | sala; su borde con las paredes fija la referencia del paneo |
| Paredes (izquierda y derecha) | verde (sólido; damero de 2/0 con `-DK080_PATTERN`) | sala (cuna del tubo) |
| Techo y pared del fondo | azul (sólido; oliva con damero en la vista con paneo) | fondo de la escena (referencia central) |
| Panel (x=-100) y (x=+60) | cian (rango 8-11) | columnas verticales |
| Panel (x=-60) | amarillo (rango 4-7) | columna vertical |
| **Panel (x=+100)** | **naranja único (rango 12-15)** | marcador de movimiento: color único que sigue el paneo |
| HUD (franja inferior, 1 bpp) | blanco sobre negro | nombre+versión, FRAME/FULL/FAKE, `FULL/FAKE OFF`, FAC/VER, PAN |

La paleta está diseñada por **rangos de bits** (0-1 fondo, 2-3 paneles con las 4 entradas de cada combinación del mismo color): con el relleno por plano el panel conserva su color aunque debajo tenga bits de la sala.

## Evolución temporal

- La cámara recorre un paneo triangular entre −128 y +128 px de imagen (yaw ±29°): fase lenta (±96 px, 1 px/frame: el Full se **rebanada** en etapas —clip, sala por plano, paneles— una por update, y la pantalla sigue presentando el Full anterior; K=0) y fase rápida (borde de la trayectoria, 16 px/frame: Full completo cada frame; el Fake se rechaza por margen).
- En cada Full la escena se re-renderiza con la cámara actual; en cada Fake la ventana se desplaza 1 px dentro del framebuffer (la escena parece moverse un 1 px suave entre Fulls y varios falsos por pasada alargan la imitación). El marcador naranja permite seguir el movimiento en cualquier frame.
- El HUD se refresca cada 32 frames (~1,5 Hz) con los contadores, el estado del frame y el K vigente.

## Criterio de éxito

1. Contrato de píxel: el PNG de un frame Fake = PNG del Full anterior desplazado su offset (exactitud del mecanismo probada en aislamiento; en secuencia, costura de captura acotada — ver `VALIDATION.md` §1–§2).
2. Cadencia: display ≥25 Hz con base (Full) ≥12,5 Hz — display **cumplido** (32–36 updates/s con el rebanado); base **pendiente** (~9 Hz: el Full completo suma ~4 campos y el rebanado no reduce su trabajo, solo lo reparte; demo ABIERTA, §5, §7 y §11 de `VALIDATION.md`).
3. Sin flicker/tearing — sin veredicto por ahora: mientras el Full desborde el campo (DT-034, configurable en el rebanado de F2) puede verse una **banda horizontal rasgada** (parche de ventana aplicado a mitad de campo) o una **columna de contenido del frame anterior** en el borde de la ventana. Ambas son **defecto de presentación, no elementos de la escena**: el pase C debe marcarlas explícitamente; se cierran con la optimización/rebanado del Full, no con un parche de dibujo.
4. La visión (Ollama) describe la sala con sus paneles y confirma el desplazamiento. **Defecto conocido (DT-042)**: a pan > 0 la sala se muestra invertida (suelo arriba) desde antes de F2; el pase C debe marcarlo como FALLO, no como elemento de la escena (el eval sin contraste lo ha racionalizado como «coherente»).
