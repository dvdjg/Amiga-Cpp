# VALIDATION — 065 MECHANISMS BRIDGE

Fecha: 2026-10-14 · Config: A500_debug · Versión en pantalla: v1.0.x (franja inferior)

## Medidas

| Configuración | fps (measure-fps) | ciclos/frame | Notas |
|---|---|---|---|
| Estado previo (sync, hojas de 4 planos, copper re-emitido) | 17.62 | 402 555 (2.84 campos) | 84 blits/frame en sync |
| Async + 2 planos + copper estático + franja | 24.49 | 289 703 (2.04 campos) | 18 jobs |
| **Entregado** (+ contratos de solape: brazo/jugador/bola) | **22.36** | **317 192 (2.24 campos)** | 18 jobs + cajas de borrado y máscaras |

`detail=0x13` (telemetría de la demo) = 19 jobs/frame. Franja: U294/T295 k ciclos.

### Contratos de solape (corregidos en la pasada de contratos)

- **Brazo de la catapulta**: 4 segmentos a ~6 px con hojas de 22 px → con copia **opaca** el
  padding de cada uno borraba al anterior (solo se veía el último: "blobs"). Ahora los
  segmentos del brazo son **cookie-cut con máscara** (`m_plank_m`) y se ensamblan en una barra
  continua; la estela la limpia la **caja AABB del brazo anterior** en la pasada de borrado
  (antes de ningún dibujo).
- **Jugador**: camina SOBRE el puente; su padding opaco mordía la cuerda bajo sus pies cada
  frame. Ahora es **cookie-cut** (`m_player_m`) + caja de borrado de su posición anterior.
- **Bola y escombros**: vuelan sobre la cuerda (pads de 14 px); ahora **cookie-cut**
  (`m_ball_m`) + cajas de borrado. La cuerda (y el resto) se redibuja **después** de la
  pasada de borrado, así el frame es correcto en todos los cruces.
- **Eslabones del puente, base y objetivo**: siguen **fastcopy** — su geometría no solapa
  contenido ajeno (contenido 6 px vs separación 15 px), que es el contrato del FastCopy.

## Presupuesto — DEUDA DOCUMENTADA (excepción)

La demo queda **por debajo del objetivo de 50 fps** (24.5). Diagnóstico medido:

- El frame está dominado por la **cadena de blits** (36 blits/frame; cada job planar de 2
  planos programa y espera entre planos) + simulación; el resto es VBlank/render.
- **Palanca medida (siguiente paso)**: pasar el bitmap y las hojas a **interleaved**
  (planos por fila) — cada dibujo pasa de 2 blits a **1** (mitad de cadena), como documenta
  la 066. Alternativa: arte de 1 plano (los valores 1/3 → 1) — mitad otra vez, a cambio del
  acento cálido.
- Se aplicó ya: hojas de 2 planos (17.6→24.5), ejecución async, copperlist estática y
  parcheo (la re-emisión por frame costaba ~10k), franja de versión.

## Verificación por visión (Ollama `qwen3-vl:8b-instruct-q8_0`, 4 frames a 400 ms)

Prompt: `out/run/vision_prompt_065.txt`. Respuesta (resumen fiel):

| elemento | visible | se mueve | artefactos |
|---|---|---|---|
| puente de eslabones (catenaria) | SÍ | SÍ (se deforma) | NO |
| jugador (caja blanca) | SÍ | SÍ (sobre el puente) | NO |
| catapulta (brazo cálido) | SÍ | SÍ (cambia el ángulo) | NO |
| bola en vuelo / escombros | SÍ | SÍ | NO |

Franja legible en las capturas ("065 MECHANISMS BRIDGE", versión, F/U/T).

## Alcance de la afirmación

Verificado: mecánicas F6 (puente/jugador/catapulta/bola/destructible) y su dinámica en
A500_debug, secuencia de 4 frames, visión dirigida; sin artefactos observados. **Sin
verificar**: 50 fps (deuda arriba), release `-O2`, NTSC, y el rebote/impulso en casos límite
(los cubre HOST-438 por unidad, no esta secuencia).

## Puerta y bloque solido (2026-10-23)

Anadidos scene::Door (12x24, sube 24 px con el ciclo) y scene::SolidBox (16x10) al camino del jugador; el objetivo usa scene::Destructible. La demo pasa a un_frames_polling (la guarda 0xE212 rechaza un_frames bajo SO sin takeover) y mide **16,46 fps / 3,038 campos / 431.076 ciclos** en A500_debug (stable: true, detail=0x15): la puerta/bloque anaden 3 jobs y el sondeo cuesta mas que el bucle anterior; queda como deuda de presupuesto ya documentada. Secuencia de 6 frames y VISION.md regenerada (Ollama; puerta/bloque y objetos en movimiento descritos sin artefactos).
