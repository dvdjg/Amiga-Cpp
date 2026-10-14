# VALIDATION — 066 BOB SHOWCASE

Fecha: 2026-10-14 · Config: A500_debug · Versión en pantalla: v1.0.x (franja inferior)

## Medidas

| Configuración | fps (measure-fps) | ciclos/frame | Notas |
|---|---|---|---|
| Sync sin blits (delta) | 49.92 | 142102 (1 campo) | el CPU de la demo cabe en el campo |
| Async con blits (estado previo del turno) | 35.74 | 198466 (1.40 campos) | 19 jobs/frame |
| **Async + copper parcheado (entregado)** | **37.54** | **188965 (1.33 campos)** | 19 jobs; el armado de sprites se parchea (8 words) en vez de re-emitir la lista |

`measure-fps` funciona de nuevo: el `frames=0` que se veía tras el cambio a `takeover_system` **no era un fallo de resolución** sino el **crash real** de la demo en `init` (ver §Bugs corregidos). `run-status detail=0x13` = 19 jobs/frame (telemetría de la demo).

### Perfil por secciones (`g_eng_prof`, medido con el canal lateral)

| Sección | ciclos/frame | Lectura |
|---|---|---|
| 0 · espera de la cadena async | **701** | la cadena del frame anterior ya terminó: la espera explícita no cuesta |
| 1 · simulación + plan + lanzamiento | **163 003** | el CPU de la demo domina (86 % del frame) |
| 2 · parcheo del armado + install | **338** | el parcheo sustituyó ~10k de re-emisión |
| resto (clear, telemetría, render, VBlank) | ~25k | |

Bisección de la sección 1 (temporal): updates 37.5k + 24.9k; **lanzamiento async ~100k**. Con 19 jobs × 2 planos (target planar) la cadena entera (38 blits con `wait_blitter` entre planos + 19 IRQs) se ejecuta **dentro del lanzamiento**: no hay solapamiento real CPU↔Blitter. Palancas medidas para el siguiente paso: (a) menos jobs (borrados → opacos con padding: −4 jobs), (b) abaratar el coste por job de la cadena (planos por job + IRQ), (c) lanzar la cadena antes (feeder de plan incremental).

## Bugs corregidos en este turno (todos con causa raíz)

1. **Crash en `takeover_system` (PC=0xFFFFFF22)**: `OpenLibrary` guardaba la base en `g_sys.gfx` pero las macros del SDK (`LoadView`/`WaitTOF`/`OwnBlitter`) llaman por el **global `GfxBase`**, que quedaba a 0 → `JSR (a6,-0xde)` con `a6=0`. Fuente única: `GfxBase` en todo el takeover/restore. (Diagnóstico: pila + desensamblado con el MCP: `JSR.L (a6,-$00de)`.)
2. **Unidades en el mapa de regiones de la demo**: `sheet_advance` sumaba el offset en **palabras** a una dirección en **bytes** → las regiones de las hojas quedaban a mitad de camino y solapaban; **lo cazó el propio sistema de coherencia** (`0x7C001`, solape parcial). Corregido con `off * 2`.
3. **Cookie-cut planar sin dibujar (silencioso)**: la demo declaraba `layout = Interleaved` para hojas **planar**; el camino cookie-cut con `Interleaved + SeparatePlane` devuelve `false` sin dibujar (`bob.hpp:399`). Or/Opaque no usan ese campo (usan el del target) — por eso solo fallaban caminante y patrullero. Corregido a `Planar` + validador `check_bob_plan` (capa 2) que lo detecta en setup (`0x7C200`).

## Verificación por visión (Ollama `qwen3-vl:8b-instruct-q8_0`, 4 frames a 350 ms)

Prompt dirigido `out/run/vision_prompt_066_anim.txt` (animación por entidad + bordes raros). Respuesta (resumen fiel):

| entidad | ¿se mueve? (visión) | ¿real? (diff por zonas) |
|---|---|---|
| orbes cyan (zona A) | SÍ | 4.0 % px cambiados ✓ |
| deslizadores naranjas (B) | SÍ | 5.7 % ✓ |
| caminante sobre damero (C) | SÍ (tras corregir el bug 3) | 2.3 % ✓ (antes 0.0 % → bug detectado) |
| patrullero + cofre save-under (D) | SÍ; damero intacto bajo el cofre | 6.9 % ✓ |
| barras + anillo (E) | SÍ | 4.1 % ✓ |
| joya (F) | **NO** | **3.4 % ✓ — falso negativo de la visión** (objeto pequeño; el recorte lo confirma moviéndose) |
| naves HW (franja) | SÍ | 0.5 % ✓ |
| degradado de fondo | NO (fijo por diseño) | 0.0 % ✓ |

- **Bordes raros**: la visión no ve ninguno, y es correcto: los "artefactos" diagonales junto a los dameros son los **iconos decorativos de flechas** del fondo (`bg_icon(1u)`, estáticos). El único residuo real (un fragmento violeta) era el borrador del caminante no dibujado (bug 3), ya resuelto.
- **Lección de procedimiento**: la visión detectó correctamente el bug del caminante (NO movimiento) pero dio un **falso negativo con la joya** (20 px). La pareja visión + **diff objetivo por zonas** (`out/tmp/zone-diff-066.mjs`) es la verificación fiable; la visión sola no basta para objetos pequeños.

## Alcance de la afirmación

Verificado: render y dinámica de las 7 zonas + sprites + degradado en A500_debug (OCS PAL 320x256, 4 planos, bitmap planar), secuencia de 6 frames, visión + diffs objetivos por zona; coherencia de setup activa (regiones, BPLCON0, orden de WAITs, combinación de bobs) — la demo llega a READY con los 4 chequeos en verde. **Sin verificar**: 50 fps (CPU-bound medido; palancas en §Perfil), release `-O2`, NTSC, y `restore_system()` en caliente (implementado; pendiente una demo que lo ejercite al terminar).
