# Consulta (castellano): prioridad de `BPLCON2` con un solo playfield

> Versión en castellano para el registro; la consulta se envía a Grok en inglés
> (`consulta-bplcon2-single-playfield-priority-en.md`), como exige `AGENTS.md` §1.3.
> Relacionado: `docs/reference/amiga/techniques/sprite-layer.md` §5,
> `docs/reference/emulators/winuae/sprite-color-priority.md`, demo
> `demos/techniques/amiga/sprites/214_attached_object`.

## Contexto

Demo para Amiga OCS/A500. Pantalla de **un solo playfield** de 4 planos (320×256,
`BPLCON0=$4200`) y 8 sprites hardware como objetos (un par *attached* de 15 colores y seis
sprites de 3 colores). La copperlist escribía `BPLCON2 = $0000` al inicio de cada field. Con ese
valor, los sprites eran **invisibles allí donde el playfield tenía índice de color distinto de 0**
(solo se veían en el borde y sobre zonas transparentes del bitmap). Con `BPLCON2 = $0024` todos
los sprites aparecen delante del playfield, que es lo buscado.

En el AHRM 3.ª encontramos:

- Table 7-1: bits 2-0 = `PF1P2-PF1P0`; bits 5-3 = `PF2P2-PF2P0`.
- Table 7-2: `000 -> PF1 SP01 SP23 SP45 SP67 … 100 -> SP01 SP23 SP45 SP67 PF1`.
- Nota tras Table 7-2: *«PF2P2-PF2P0, bits 5-3, son los bits de prioridad para playfields
  normales (no duales)»*.
- El ejemplo del manual: `MOVE.W #$0024,BPLCON2 ;Sprites have priority over playfields`.

## Preguntas

1. Con **un solo playfield** (no DPF), ¿qué campo coloca el playfield en la cadena de
   prioridad frente a los sprites: `PF1P` (bits 2-0) o `PF2P` (bits 5-3)? La nota de Table 7-2
   dice que `PF2P` es el de los playfields normales, pero la tabla y el registro se describen
   en términos de `PF1P`. ¿Se ignora `PF1P` en modo no dual?
2. Valor de **reset** de `BPLCON2` en OCS (A500) y ECS: ¿es `$0024`?
3. ¿Es correcto que `BPLCON2=$0000` sitúe el playfield **delante de los cuatro grupos de
   sprites** (sprites visibles solo sobre color 0 y borde)? Lo observamos así; queremos
   confirmación documentada y de hardware real.
4. ¿Afecta la peculiaridad OCS/ECS de `plf2pri >= 5` con el plano 5 a una pantalla normal de
   4 planos?
5. Valor canónico recomendado para «un playfield; todos los sprites delante»: ¿`$0024`, o
   `$0004` (`PF1P=100`, `PF2P=000`) está mejor definido en single-playfield? ¿Qué usarían los
   juegos/demos reales y por qué?

## Qué usamos ahora

`BPLCON2 = $0024` una vez por field (defecto del engine en
`copper::Scheduler::emit_planes_display`), citando AHRM Table 7-2 y el ejemplo `$24`. Regresión
de las demos afectadas en verde. Queremos confirmar que es la opción canónica/portable y corregir
la referencia con la regla exacta.

## Respuesta (Grok, 2026-10)

1. **En single-playfield el campo que manda es `PF2P` (bits 5-3)**, como dice la nota del AHRM
   tras Table 7-2; `PF1P` no controla ese playfield.
2. **`BPLCON2` no tiene valor de reset documentado** (indefinido/residual de Denise): hay que
   escribirlo siempre de forma explícita.
3. **`$0000` = playfield delante de todos los sprites** (solo se ven sobre índice 0 y borde);
   comportamiento real de OCS.
4. La peculiaridad `PFxP>=5` exige **≥5 planos**: no aplica a `BPLCON0=$4200` (4 planos).
5. **Canónico: `$0024`** (ejemplo del propio manual); `$0020` basta en single-playfield puro,
   pero `$24` es portable y compatible con DPF. **Mantener `$0024`.**

Conclusión: el fix del engine es la solución correcta y documentada; la referencia se actualizó
con el matiz de `PF2P` (`sprite-layer.md` §5, `winuae/sprite-color-priority.md`).
