# VALIDATION — 088_blit3d_planes (relleno por plano, paso A: colores)

## Qué se ha verificado y con qué evidencia

| Check | Evidencia | Resultado |
|---|---|---|
| Color multi-plano (magenta = planos 0+1) | Muestreo de píxeles de `out/blit3d/088exp.png` y `088iso2.png` (runs por fila): cara izquierda azul (solo plano 0) ✓, cara derecha magenta (planos 0+1) ✓, HUD magenta ✓ | **OK** |
| Relleno por plano **completo** | Muestreo por filas del cubo isométrico: la **cara superior queda sin rellenar** (solo contorno punteado rojo + un pico rojo); la cara derecha solo se rellena desde la fila en la que su borde izquierdo pasa a ser vertical | **FALLO (bug abierto)** |
| Visión canónica | `src/VISION.md` (3 frames): describe bien las caras laterales (azul/magenta) pero **confabula** la cara superior como rellena y «cambios de color» entre frames idénticos (solo cambia `F<num>`): los frames difieren únicamente en el contador | **No fiable para este caso** |

## Artefacto observado: publicación mid-frame (NO era el fill)

La **sonda estática** `089_blit3d_fill_probe` (rectángulo y rombo con aristas inclinadas, dibujados UNA vez en `init` con el mismo `fill_polygons_by_plane`) sale **perfecta**: el camino contorno XOR ONEDOT + dedupe + `FILL_XOR` descendente es correcto, también con cruces inclinados. Los artefactos de la 088 eran de **presentación**: la primera versión usaba un solo buffer y el fill escribe de abajo arriba, así que una captura podía pillar el blit a medias (filas bajas rellenas, altas no); con doble buffer, publicar a mitad de update dejaba al raster viendo un buffer en construcción. Corregido publicando al **inicio** del update (se publica el buffer completo anterior y se pinta el siguiente trasero). Conteo por frame en una secuencia de 6: 4 capturas dan `azul=rojo=magenta=900 px` (tres caras con la misma área y colores correctos); quedan capturas aisladas con el buffer en construcción — se cerrará publicando en el VBlank (o con `PresentController`) y se vigilará el residual de la familia 212.

**Evidencia de la sonda**: `out/blit3d/089probe.png` (rectángulo y rombo rellenos por completo; rectángulo = control de aristas rectas, rombo = aristas inclinadas).

## Rotación 3D (paso B, EN CURSO)

Implementada sobre la misma demo: cubo de 8 vértices, rotación `Rx·Ry·Rz` (`load_rotate`), clip de cámara y proyección de `render3d`, culling por normal 3D y submit por plano. Estado: **el culling deja ver una sola cara por frame** (conteo de colores por frame: una sola componente, p. ej. `red=4487` o `magenta=4440`, y frames vacíos) — el **signo del winding/normal está pendiente** (las listas vienen del convenio Y-arriba de la 203 y la cámara de `render3d` es Y-abajo). No verificado: siguiente paso es corregir el signo y validar con visión de secuencia (rotación real) + conteo de áreas de los 3 colores.

## Alcance y lo NO cubierto

- **Rotación 3D, culling, clipping de cámara/viewport y poliedros**: no implementados aún en esta demo (pasos siguientes del roadmap).
- **Solapes entre sólidos**: el fill por paridad no los soporta; la solución planificada es el **painter por objeto con máscaras por plano** (`ROADMAP_3D_BLITTER.md` §«Solapes entre sólidos»).
- **Visión**: el modelo canónico no es fiable en esta escena estática (confabula); hasta que haya movimiento real (rotación), la evidencia es el volcado de píxeles.
