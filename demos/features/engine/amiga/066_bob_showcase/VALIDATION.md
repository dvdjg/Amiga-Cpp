# VALIDATION — 066 BOB SHOWCASE

Fecha: 2026-10-14 · Config: A500_debug · Versión: v1.0.11 (franja en pantalla)

## Medidas

| Configuración | fps (measure-fps) | ciclos/frame | Notas |
|---|---|---|---|
| Sync sin blits (delta) | 49.92 | 142102 (1 campo) | el CPU de la demo cabe en el campo |
| Sync con blits | ~34 | 208667 | camino validado, render correcto |
| **Async con blits (entregado)** | ~39-44 | 183832 (T161 en franja) | cadena async correcta (L=1 launch/frame, S=17 submits/frame) |

`measure-fps` con el build actual puede devolver `frames=0` por un fallo de resolución del
run-status (mapa/secciones) — pendiente; la franja de la demo da la telemetría fiable
(F/U/T/L/S en k ciclos).

## Verificación por visión (Ollama `qwen3-vl:8b-instruct-q8_0`, secuencia de 3 frames a 600 ms)

Prompt: `out/run/vision_prompt_066.txt` (los 7 puntos de la intención). Respuesta (resumen
fiel):

- 1) degradado por bandas azul/gris: **SÍ** · 2) seis zonas punteadas: **SÍ** ·
  3) orbes cyan rebotando (Or): **SÍ** · 4) barras naranjas deslizándose (fast+padding):
  **SÍ** · 5) dameros con figuras cyan cookie-cut + rombo arcoíris (save-under): **SÍ** ·
  6) barras naranjas/blancas y anillos magenta (bob multicolor): **SÍ** · 7) franja inferior
  con naves y balas (sprites HW): **SÍ**.
- Movimiento: cambios de posición en todas las zonas entre frames (diferencias de píxeles
  medidas: ~7.1-7.7k px). Sin artefactos ("la animación parece fluida y bien ejecutada").
- La franja de versión/telemetría se lee y cambia entre frames (F/U/T/L/S).

## Alcance de la afirmación

Verificado: render y dinámica de los 7 puntos en A500_debug (OCS PAL, 320x256, 4 planos),
secuencia de 3 frames, visión + diffs objetivos. **Sin verificar**: 50 fps (ver
`docs/debugging/investigaciones/066-async-blit-irq-bajo-el-so.md` §Medidas: falta reducir
lanzamientos por job), release `-O2`, NTSC, y `restore_system()` en caliente (implementado;
pendiente una demo que lo ejercite al terminar).

## Cobertura de la matriz de BOBs (contra HOST-439)

Or ($FC), erase box, Opaque con padding (FastCopy), cookie-cut planar con máscara separada,
save-under (`bob_save_box`/`restore`), bob multicolor (4 planos), sprites HW por
`SpriteManager::arm_object` (par adjunto). El par interleaved está cubierto por HOST-439 y no
se usa en esta demo (target planar).
