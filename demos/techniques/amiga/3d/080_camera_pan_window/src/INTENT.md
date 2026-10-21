# INTENT — 080_camera_pan_window (paneo de cámara con frames intermedios)

## Qué hace

Una sala 3D poligonal con suelo ocre, techo verde, paredes y fondo azules, más cuatro paneles verticales de colores (uno naranja único) dentro del pasillo. El recorrido predeterminado hace un barrido yaw/Y de 120° de campo, un barrido pitch/X de 120° y una órbita combinada Lissajous 1:2 de ±40°. El framebuffer es 352×256 y el viewport 320×208 (guardas de 16 px en X y 24 filas en Y). El clipping general de cámara permite que las superficies entren y salgan del frustum. `K080_PITCH_TEST=1` y `K080_LISSAJOUS_TEST=1` fuerzan una fase para validarla aisladamente. El HUD presenta la fase y ambos ángulos en grados.

## Qué pretende demostrar

El **contrato de frames intermedios por paneo** de la técnica `docs/reference/amiga/techniques/camera-pan-fake-frames.md` (fase F1 de `ROADMAP_3D_PRESENTACION.md`):

- **Full** (render real, base objetivo ≥12,5 Hz): transforma, recorta y rellena por plano toda la sala y los paneles en el framebuffer sobredimensionado. El clipping general de cámara incluye near, laterales, techo y suelo antes de proyectar.
- **Fake** (display objetivo ≥25 Hz): no se re-renderiza nada; X parchea coarse/fine y Y desplaza el puntero por filas completas. En yaw/pitch puros, el Fake es el Full previo desplazado en su eje; en Lissajous se comprueba el desplazamiento bidimensional (ver `VALIDATION.md`).
- Los barridos triangulares recorren `−60°..+60°` en yaw y pitch y vuelven al centro antes del siguiente modo. Lissajous usa `yaw = sin(φ)` y `pitch = sin(2φ)` con amplitud aproximada de 40°; la tabla seno se lee directamente, sin cálculo trigonométrico por frame. Las velocidades limitan la deriva por Full a las guardas X/Y. La telemetría y el HUD muestran los ángulos reales y la fase.
- **Mezcla de colores con damero a nivel de píxel** (opción `-DK080_PATTERN`; `eng::graphics::fill_pattern.hpp`, HOST-474): por defecto la sala va **SÓLIDA** (colores planos, menor coste de relleno). Con el flag, los planos de la sala llevan el damero 1×1 y su inverso (`fill_dither_plane_cpu`; 2 filas por plano) y el relleno por plano los aplica por fases (`D = A & B`) tras el area fill, fundiendo dos índices base por cara. La inestabilidad que mostró la fase de patrón era la espera de VBlank de la familia 212 (resuelta con `run_frames_polling`): con el bucle por sondeo el patrón vuelve a ser estable (4/4 arranques). Los paneles no llevan patrón (sólidos) y el marcador naranja conserva su color único.

## Elementos en pantalla

| Elemento | Color | Papel |
|---|---|---|
| Suelo | ocre | sala; su límite con las paredes identifica el horizonte inferior |
| Techo | verde | superficie superior; color diferente del fondo para evitar que se fusione con él |
| Paredes laterales y pared del fondo | azul | estructura del pasillo y plano de fondo; el contraste delimita techo y suelo |
| Panel (x=-100) y (x=+60) | cian (rango 8-11) | columnas verticales |
| Panel (x=-60) | amarillo (rango 4-7) | columna vertical |
| **Panel (x=+100)** | **naranja único (rango 12-15)** | marcador de movimiento: color único que sigue el paneo |
| HUD (franja inferior, 1 bpp) | blanco sobre negro | nombre+versión, FRAME/FULL/FAKE, modo de recorrido, yaw, pitch y K |

La paleta está diseñada por **rangos de bits** (0-1 fondo, 2-3 paneles con las 4 entradas de cada combinación del mismo color): con el relleno por plano el panel conserva su color aunque debajo tenga bits de la sala.

## Evolución temporal

- La cámara recorre yaw −60°→+60°→centro, luego pitch −60°→+60°→centro y después una onda Lissajous con relación 1:2; al cerrar el ciclo vuelve a yaw.
- En cada Full la escena se recorta y re-renderiza con la cámara actual; durante el siguiente Full las Fakes desplazan la ventana dentro de la guarda X/Y. Los paneles de colores únicos muestran cómo los objetos entran y salen del frustum.
- El HUD se refresca cada 32 frames (~1,5 Hz) con los contadores, el estado del frame y el K vigente.

## Criterio de éxito

1. Contrato de ventana: los Fakes de yaw/pitch puros equivalen a un desplazamiento de un eje; Lissajous se verifica con desplazamiento X/Y y búsqueda de costura (alcance en `VALIDATION.md`).
2. Cadencia: objetivo de display ≥25 Hz y base (Full) ≥12,5 Hz. La base sigue bajo el objetivo (~8–9 Hz); el usuario acepta provisionalmente la medida actual y la optimización queda pausada. La demo no se declara cerrada por cadencia ni por ausencia de tearing.
3. Sin flicker/tearing — sin veredicto por ahora: mientras el Full desborde el campo (DT-034, configurable en el rebanado de F2) puede verse una **banda horizontal rasgada** (parche de ventana aplicado a mitad de campo) o una **columna de contenido del frame anterior** en el borde de la ventana. Ambas son **defecto de presentación, no elementos de la escena**: el pase C debe marcarlas explícitamente; se cierran con la optimización/rebanado del Full, no con un parche de dibujo.
4. Ollama y la inspección directa deben mostrar las tres fases, el techo verde separado del fondo azul y los paneles entrando/saliendo del encuadre. La evaluación de visión es auxiliar: contrastar el movimiento con diff por frame y las trayectorias de yaw/pitch en el HUD.
