# 066: bugs reales del camino de blits encontrados con el showcase (caché, recursión async, render async abierto)

Estado: **2 bugs corregidos + 1 abierto**. Este doc sustituye a la hipótesis inicial "el SO mata la
IRQ BLIT" (era incorrecta: el handler de nivel 3 del engine **no encadena** la ROM —
`amiga.cpp:288-306` sustituye el vector entero; el doc 212 describe otro camino).

## Medidas de referencia (build 066, `measure-fps`)

| Configuración | fps | ciclos/frame |
|---|---|---|
| Sync (`execute_frame_plan`) con todos los blits | 34.0 | 208667 |
| **Sin blits** (resto igual) | **49.92** | **142102 (= 1 campo exacto)** |
| **Async (`execute_frame_plan_async`) con todos los blits** | **49.92** | **142102** |

Conclusión de coste: **todo el déficit del sync son los lanzamientos de blit** (66.5k ciclos de
pared sobre el campo); el CPU de la demo + la espera de VBlank no añaden nada (sin blits el frame es
exactamente 1 campo). El "88% en ROM" que se citó antes salió de un parseo del perfil que contaba
marcadores no-PC (valores 1, 4, 7…) como muestras de ROM: **dato inválido, retirado**.

## Bug 1 (corregido): la caché de registros comunes del Blitter cruzaba jobs/frames

`m_blt_common_valid` sobrevivía entre jobs individuales. Los servidores del SO (y cualquier otro
código) pueden programar el Blitter en cualquier hueco; un job "igual" al cacheado se saltaba las
escrituras y bliteaba con registros ajenos → **planos perdidos** (objetos a medio dibujar, colores
imposibles). El camino sync lo tapaba a veces (las rachas batch invalidan), pero es un bug real.
Corregido: `submit_blit_job` invalida la caché en cada job (`amiga_blitter.cpp`). Las rachas batch
siguen fijando su estado explícitamente.

## Bug 2 (corregido): recursión del feeder async por el servicio del `wait_blitter`

`wait_blitter()` llama al **servicio de blit** registrado mientras espera. Con el camino async, ese
servicio ES el feeder de la cadena (`frame_plan_async_step`); un job multi-plano hace
`wait_blitter()` entre planos → el feeder se re-entraba a sí mismo y corrompía la cadena (render
roto y reinicios). Corregido con un guard anti-recursión (`g_in_blit_service` en
`amiga_internal.hpp`; el feeder y el lanzamiento inicial lo activan alrededor del submit).

## Bug 3 (ABIERTO): con el async, ciertos jobs aún renderizan mal

Con async + los dos fixes: **49.92 fps** pero el render sigue corrupto en un subconjunto de jobs:
los **opacos** (deslizadores con padding, cofre save-under, joya) salen correctos; los **Or ($FC)**,
**erase box** y **cookie-cut con máscara** salen mal (aros/aspas, planos perdidos). Con sync el
mismo plan renderiza bien. No hay rachas batch en este plan (los estados difieren por el shift), así
que sync y async ejecutan los mismos jobs individuales: la diferencia está en la **ejecución
encadenada por ISR** de esos tipos. Sospechas a investigar (en orden):
1. El ISR programa el siguiente job tras la IRQ de fin de blit; verificar con watchpoints si la
   IRQ BLIT llega **antes** del fin real del blit en algún caso (el engine limpia `INTREQ` BLIT en
   `level3_dispatch`; si el bit se limpia con el blit aún drenando los últimos words, el siguiente
   job pisa registros en vuelo).
2. El `wait_blitter()` con BLTPRI dentro del ISR (poll puro tras el guard) y su interacción con el
   IRQ VERTB (latido) que preempta la cadena.
3. Comparar registro a registro (BLTCON/MOD/punteros) el job N en sync vs async con watch de
   Copper/CPU (memwatch `reportonly`) — el sync ya está validado contra HOST-439.

## Recomendación

- Hasta cerrar el bug 3: la 066 usa **sync** (34 fps, render correcto) o **async** (49.92 fps,
  render con defectos conocidos). El fix correcto es el 3; no maquillar con workarounds.
- El setup de control de la demo incluye `set_vblank_sleep(false)` + `Forbid()` (setup clásico de
  demo; `Permit()` al salir). El takeover documentado está en
  `docs/engine/architecture/HARDWARE_AND_ROM_KERNEL_POLICY.md` §«Toma de control del display».
