# VALIDATION — 065 MECHANISMS BRIDGE

Fecha: 2026-10-14 · Config: A500_debug · Versión en pantalla: v1.0.x (franja inferior)

## Medidas

| Configuración | fps (measure-fps) | ciclos/frame | Notas |
|---|---|---|---|
| Estado previo (sync, hojas de 4 planos, copper re-emitido) | 17.62 | 402 555 (2.84 campos) | 84 blits/frame en sync |
| **Entregado** (async, 2 planos, copper estático, franja) | **24.49** | **289 703 (2.04 campos)** | 18 jobs (2 planos/job) = 36 blits |

`detail=0x12` (telemetría de la demo) = 18 jobs/frame. Franja: U275/T275 k ciclos (coherente
con la medida).

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
