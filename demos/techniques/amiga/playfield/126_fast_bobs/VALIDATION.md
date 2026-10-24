# VALIDATION — 126 FAST BOBS (F5: márgenes automáticos + demo DPF)

Fecha: 2026-10-14 · Config: A500_debug

## Qué cierra

F5 de `ROADMAP_SPRITES_COMPLETO.md` §3.4: la demo DPF con BOBs rápidos por la fachada
(`scene::FastBobLayer`) y los **márgenes derivados del asset** (`graphics::fast_bob_margins`,
HOST-437): el paso máximo por eje ES la fuente de la geometría de la hoja (padding + tamaño),
no un número cableado.

## Medidas

| Configuración | fps (measure-fps) | ciclos/frame | Notas |
|---|---|---|---|
| **Entregado** | **49.97** | **141 960 (1 campo)** | DPF 3+3 por bandas + 2 actores fastcopy |
| **Revalidado (HUD)** | **49.92** | **142 101 (1 campo)** | HUD de host de una linea; 2 actores |

## Márgenes (contrato F5)

- Pasos **reales** de los actores: (3, 2) px/frame (`tri(f*3u,128)` y `tri(f*2u,128)`) — son
  los **declarados** (mínimos).
- La geometría de la hoja sale de `fast_bob_margins(16, 16, 3, 3, 2, 2, double_buffer=false)`
  → **22×20** con ancla en `(kBobPadX, kBobPadY)`; el resto (módulos, strides, copia vs
  degradación) lo decide la capa/engine.
- La espera de frame es **activa** (`set_vblank_sleep(false)`): bajo el runner, `Wait()` de
  exec borra el VERTB del engine al cambiar de tarea — con la hoja mínima el timing lo hacía
  100% reproducible (**DT-011**, resuelto; ver `212-vblank-irq-apagada-por-el-so.md`).

## Verificación por visión (Ollama `qwen3-vl:8b-instruct-q8_0`, 4 frames a 300 ms)

Prompt: `out/run/vision_prompt_126.txt` (movimiento + rastros + huecos).

| aspecto | resultado | detalle |
|---|---|---|
| movimiento de los 2 cuadrados | OK | posiciones distintas en las 4 capturas (arriba-izq → abajo-der...) |
| rastros (restos rojo/amarillo) | OK | ninguno alrededor de las posiciones previas |
| huecos en el fondo de rayas | OK | fondo uniforme, sin interrupciones |
| conteo | OK | exactamente 2 cuadrados por captura |

## Alcance

Verificado: copia rápida con padding **mínimo** sin rastros ni huecos, cadencia 1 campo,
márgenes derivados del asset en A500_debug. **Sin verificar**: release `-O2`.

## Degradación a cookie-cut (2026-10) — cubierta

La demo **dispara el camino lento** de la capa de forma determinista: bob0 acelera a 5 px/frame
(pad = 3) en la ventana [112,128) de cada 128 frames → `clear_box` + cookie-cut (hoja lenta con
máscara). Quedó cubierto al arreglar **DT-012** (el walk de la máscara se salía de su plano con
hoja planar + destino interleaved; ahora por plano — HOST-439 T4b lo fija).

| Medida | Resultado | Lectura |
|---|---|---|
| fps debug (con disparo) | **49,92 fps · 1,00 field · 1000 frames** | sin congelación ni coste extra |
| franja izquierda (x 0..15), dentro vs fuera de la ventana | **576 px no-negros / 164 filas en ambas** | sin columna de basura (el síntoma de DT-012) |
| visión (3 frames de la ventana, f117-f121) | «cuadrados completos y nítidos, sin artefactos ni zonas corruptas» ×3 | **VEREDICTO: OK** |
