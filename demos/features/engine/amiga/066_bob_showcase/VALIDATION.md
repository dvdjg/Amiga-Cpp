# VALIDATION — 066 BOB SHOWCASE

Fecha: 2026-10-14 · Config: A500_debug · Versión en pantalla: v1.0.x (franja inferior)

## Medidas

| Configuración | fps (measure-fps) | ciclos/frame | Notas |
|---|---|---|---|
| Sync sin blits (delta) | 49.92 | 142102 (1 campo) | el CPU de la demo cabe en el campo |
| Async con blits (estado previo del turno) | 35.74 | 198466 (1.40 campos) | 19 jobs/frame |
| **Async + copper parcheado (entregado)** | **39.23** | **180 808 (1.27 campos)** | **18 jobs**: barra a copia opaca con padding (−1 job ≈ −8.2k ciclos) |

`measure-fps` funciona de nuevo: el `frames=0` que se veía tras el cambio a `takeover_system` **no era un fallo de resolución** sino el **crash real** de la demo en `init` (ver §Bugs corregidos). `run-status detail=0x13` = 19 jobs/frame (telemetría de la demo).

### Perfil por secciones (`g_eng_prof`, medido con el canal lateral)

| Sección | ciclos/frame | Lectura |
|---|---|---|
| 0 · espera de la cadena async | **701** | la cadena del frame anterior ya terminó: la espera explícita no cuesta |
| 1 · simulación + plan + lanzamiento | **163 003** | el CPU de la demo domina (86 % del frame) |
| 2 · parcheo del armado + install | **338** | el parcheo sustituyó ~10k de re-emisión |
| resto (clear, telemetría, render, VBlank) | ~25k | |

Bisección de la sección 1 (temporal): updates 37.5k + 24.9k; **lanzamiento async ~100k**. Los jobs son la moneda: **~8k ciclos por job** (medido con la fusión de la barra: −1 job = −8.2k). Palanca probada y descartada: el target/hojas **interleaved** (un blit por dibujo) exige que el BOB cubra **todos** los planos del target — ver §Experimento del doc de investigación. Palancas siguientes (bloqueadas por los contratos de la vitrina): los orbes no pueden fusionar su borrado (se solapan entre sí: el pad opaco mordería al vecino) y C/D son la exhibición de cookie-cut/save-under; llegar a 50 fps exigiría **∼5 jobs menos** (p. ej. menos objetos en movimiento o un feeder de plan incremental).

## Ronda de contratos (solapes) y sprites HW — 2026-10 segunda pasada

**Técnica vs solape** (cada zona respeta el contrato de su técnica; el sistema de coherencia
no puede verlo —es runtime— así que se corrige por diseño y se documenta):

- **Zona A (Or + borrado por caja)**: los orbes se cruzan; con borrado y dibujo intercalados
  la caja de uno **muerde los píxeles ya dibujados** de otro (parpadeo de 1 frame por cruce).
  Corregido con **dos pasadas** (borrar-todo → simular/dibujar-todo).
- **Zona B (FastCopy con padding)**: el padding se copia **opaco** y **borraría** otro objeto
  si se solaparan (el vertical cruzaba a los horizontales por construction). Corregido con
  **corredores sin cruce** (los horizontales patrullan hasta `kBx+94`; la columna del vertical
  queda libre). El solape se demuestra donde SÍ se soporta: cookie-cut (C) y save-under (D).
- **Contratos documentados**: la cabecera de la demo y el `README.md` (tabla de zonas) llevan
  una columna «Contrato» con la restricción de cada técnica (FastCopy: sin solape; borrado por
  caja: dos pasadas; cookie-cut/save-under: soportan solape).

**Sprites HW — coordenada del armado**: la pareja POS/CTL lleva el **HSTART en color-clocks**
(absoluto), no el píxel de pantalla: píxel = hpos − `DIWSTRT h` (= −0x81 con la ventana
estándar); la línea visible = VSTART − `DIWSTRT v` (= −0x2C). Verificado en emulador con el
experimento decisivo (hpos=180 → el sprite aparece en el píxel 51). El patrullaje original
(hpos 60..260) quedaba **casi todo fuera de ventana** (solo visible hpos≥0x81); ahora cada
nave patrulla su mitad (píxeles 8..288) dentro de la ventana. Diff objetivo de la banda de
naves entre frames: **2.5 %** (se mueven).

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
| naves HW (franja) | **NO** | **2.5 % ✓ — otro falso negativo con objetos pequeños** (crop: verdes/rosas en su banda; línea visible = VSTART − 0x2C) |
| degradado de fondo | NO (fijo por diseño) | 0.0 % ✓ |

- **Bordes raros**: la visión no ve ninguno, y es correcto: los "artefactos" diagonales junto a los dameros son los **iconos decorativos de flechas** del fondo (`bg_icon(1u)`, estáticos). El único residuo real (un fragmento violeta) era el borrador del caminante no dibujado (bug 3), ya resuelto.
- **Lección de procedimiento**: la visión detectó correctamente el bug del caminante (NO movimiento) pero dio un **falso negativo con la joya** (20 px). La pareja visión + **diff objetivo por zonas** (`out/tmp/zone-diff-066.mjs`) es la verificación fiable; la visión sola no basta para objetos pequeños.

## Alcance de la afirmación

Verificado: render y dinámica de las 7 zonas + sprites + degradado en A500_debug (OCS PAL 320x256, 4 planos, bitmap planar), secuencia de 6 frames, visión + diffs objetivos por zona; coherencia de setup activa (regiones, BPLCON0, orden de WAITs, combinación de bobs) — la demo llega a READY con los 4 chequeos en verde. **Sin verificar**: 50 fps (CPU-bound medido; palancas en §Perfil), release `-O2`, NTSC, y `restore_system()` en caliente (implementado; pendiente una demo que lo ejercite al terminar).
