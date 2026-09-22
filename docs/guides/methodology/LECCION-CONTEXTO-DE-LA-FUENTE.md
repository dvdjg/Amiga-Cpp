# Lección: no obcecarse con código que falla sin el contexto de la fuente

**Regla**: cuando un sistema de terceros (un reproductor, un driver, una librería) se comporta
distinto bajo nuestro entorno, **el trabajo empieza por conseguir el contexto de la fuente** —
el repositorio de confianza o la referencia escrita—, no por parchear el código a ciegas.

## El caso (OctaMED, fase A1, 2026-09)

Integramos el playroutine OctaMED (repo de KONEY) y no arrancaba. Durante **~10 turnos** probamos y
descartamos cosas contra el binario: `ADKCON`, `INTENA` del takeover, `VBLANK`/`CIAB`, el bucle
`_Wait1line`, contar registros write-only, un probe GDB a medias, un fix de `reloci`… **cada intento
seguía sin funcionar y abría la siguiente hipótesis**. El coste fue alto y el resultado, nulo.

Lo que **sí** funcionó fue leer **su documentación** (`README.md`, `modplayer.txt`,
`OCTAMED_example3.s`, `PhotonsMiniWrapper1.04.s`): ahí estaba el entorno que el playroutine espera
(el wrapper de Photon, `AUDDEV=0`, «DONT RESET [INTENA] FOR MED PLAYER»). Y el propio autor del repo
avisa: la versión *«I managed to make this work with Photon's mini Wrapper»*.

## Por qué es un antipatrón

- **Síntoma ≠ causa**: probar variantes contra un binario opaco produce falsos positivos y falsos
  negativos (p. ej. el `--warp` aceleraba la emulación y parecía que «solo sonaban dos notas»).
- **Radio de explosión**: cada parche a ciegas toca código que no entendemos y puede romper lo que
  ya funcionaba (el `INTENA` del takeover es compartido por todas las demos).
- **Coste de contexto**: se acumulan intentos sin modelo mental; el siguiente intento parte de una
  base peor que la anterior.
- **Tiempo del usuario**: el objetivo (que *suene*) puede estar ya cumplido mientras se persigue un
  modo de *diagnóstico* que no aporta valor.

## Orden correcto

1. **Documentación de la fuente primero** (`§1.7`): repo de confianza, README, ejemplos de uso,
   notas de release, changelog. Si no existe en el repo, **traerla antes de programar**.
2. **Línea base que funcione**: compilar/ejecutar **su** ejemplo tal cual (sin nuestro entorno) y
   confirmar que funciona. Si su ejemplo funciona y el nuestro no, la diferencia es **nuestro
   entorno**, no su código.
3. **Aislar la diferencia** entre la línea base y nuestra integración, con evidencia (no con
   hipótesis encadenadas).
4. Solo **entonces** tocar código; y si un intento no verifica, **revertirlo** (no dejar hardware
   vendado a medias).

## Señal de parada

Si tras **2–3 intentos** medidos no hay una hipótesis confirmada por la fuente, **parar**: o se
consigue el contexto, o se documenta el bloqueo y se sigue con otra cosa. «Probar otra variante» sin
modelo no es depuración, es apuesta.

## Corolario

Un bloqueo **bien documentado** (qué se probó, con qué evidencia, qué falta) vale más que un parche
a medias que parece avanzar. El estado del repo debe quedar **operativo**: lo que funciona, funciona;
lo que no, se declara como tal.

## Referencias

- `AGENTS.md` §1.5 (evidencia) y §1.7 (contexto técnico antes de implementar).
- Caso completo: [`octamed-startmusic-hang.md`](../../debugging/investigaciones/octamed-startmusic-hang.md).
