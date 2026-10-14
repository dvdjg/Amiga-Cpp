# 065 — MECHANISMS BRIDGE (F6)

Tutorial de la capa de mecanismos de escenario del engine (`eng/scene/rope.hpp`,
`eng/scene/mechanisms.hpp`, `eng/scene/trajectory.hpp`), con la receta de render de las
demos rápidas: **copia opaca con padding** (F5), **copperlist estática** emitida una vez y
**ejecución async** del `FramePlan` por IRQ de fin de blit.

## Qué enseña

- **Puente flexible** (`scene::Rope<s32>` + `rope_step`): 12 eslabones con extremos anclados,
  integración **verlet** y restricciones de distancia (2 pasadas); cada eslabón es un BOB
  cuadrado. El puente se **deforma** al pisarlo y bajo impulsos.
- **Jugador** (caja blanca): gravedad, avance lateral con rebote y **contacto contra la
  cuerda** (`rope_find_contact` + `rope_resolve_player`): se apoya en la superficie y empuja
  el segmento al caer (impulso repartido entre sus dos puntos).
- **Catapulta** (`scene::Catapult`): ciclo cargar → disparar; el **extremo** sale de la tabla
  de seno (`catapult_tip`) y al cruzar el ángulo de suelta nace la **bola**, que sigue una
  **trayectoria balística cocinada** (`scene::Trajectory` + follower).
- **Destructible**: la caja objetivo se rompe al impacto y deja **escombros** (4 followers).
- **Render**: los BOB llevan **padding transparente** en su hoja (= paso máximo por tick) y se
  copian **opacos** (`$F0`): la propia copia tapa la posición anterior — sin borrado, sin
  máscara, sin save-under. Fondo: `COLOR00` por bandas de Copper (lista **estática**, se
  emite una vez y no se toca).

## Receta de coste (lo que la hace rápida)

1. **Hoja de 2 planos** (`v.bitplanes = 2`): los valores 1/3 solo usan bits 0-1; declarar 4
   planos cuadruplicaba los blits en un target planar (17.6 → 24.5 fps al corregirlo).
2. **Ejecución async** (`execute_frame_plan_async` + `set_blit_service_enabled(false)`): la
   cadena avanza por IRQ mientras la CPU repinta la franja o espera el VBlank.
3. **Copper estático**: display + paleta + bandas se emiten **una vez** en `init`; el frame
   solo ejecuta el plan de blits (re-emitir la lista cada frame costaba ~10k ciclos).
4. **Franja de versión/telemetría** (norma del repo): 16 px, 4 bpp, abajo; repintada cada 32
   frames con F/U/T en k ciclos.

## Estado

Verificado por visión (puente curvo que se deforma, jugador que camina y se apoya, catapulta
que dispara, bola en vuelo, escombros) y sin artefactos; **24.5 fps** (deuda de presupuesto
documentada: ver [VALIDATION.md](VALIDATION.md) §Presupuesto — la palanca medida es el
**target interleaved**, que halva los blits por dibujo).

## Ejecutar

```bash
bash ./tools/build/build-demo.sh demos/features/engine/amiga/065_mechanisms_bridge --debug
bash ./tools/run/run-demo.sh demos/features/engine/amiga/065_mechanisms_bridge --keep-running
```
