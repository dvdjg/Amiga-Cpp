# Checklist de commit / PR

Usar junto a [RENDER_CHANGE_PROTOCOL.md](RENDER_CHANGE_PROTOCOL.md). Para cambios de render, el PR no se abre si algún punto obligatorio falla.

## Resumen

- Qué cambia:
- Por qué:
- Baseline verde y comando de reproducción:

## Checklist de render

- [ ] He identificado el baseline verde y el comando exacto para reproducirlo.
- [ ] Existe una sonda mínima que **fallaba antes** del cambio.
- [ ] He escrito el invariante (espacio, unidades, layout, huella, límites, haz).
- [ ] La huella real está centralizada en el API; la demo no calcula palabras, guardas, módulos ni cajas.
- [ ] Hay test host de geometría cuando aplica.
- [ ] Hay test on-target estático, desplazado y con vecino cuando aplica.
- [ ] La secuencia frame-step no muestra objetos parciales, restos, barras ni saltos.
- [ ] Canarios y límites están limpios.
- [ ] fps/cadencia cumplen el presupuesto y son estables.
- [ ] No he usado una respuesta de IA como especificación sin verificarla con un test.
- [ ] No he portado un bloque de master sin mapearlo a un fallo reproducido y a un test.
- [ ] `bash tools/check/render-gates.sh` pasa (HOST-504 + HOST-505 + demo-docs + L1-007).
- [ ] La demo nueva/actualizada usa la capa de alto nivel (`PlaneBobLayer`, `FastBobLayer`, `BobLayer`), no construye `BlitJob` a mano.
- [ ] La demo tiene README, entrada en el catálogo de su carpeta y una secuencia de 30 frames revisada (píxeles + visión local).

## Evidencia

- Comandos ejecutados:
- Medidas (fps/campos/ciclos):
- Secuencia/capturas:
- Tests host/on-target:

## Cierre

- [ ] Documentación e índices actualizados.
- [ ] Sin cambios accidentales en master, `Amiga-Cpp-ref` ni 207/209.
- [ ] Commit atómico con el fallo/test en el mensaje.
