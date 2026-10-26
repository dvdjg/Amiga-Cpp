# Contrato del cook-plan y `TileLayerStack`

Este documento fija la frontera entre lo que se **cuece** en host y lo que se
compone en runtime. Es la referencia para `tools/tiled/validate-cook-plan.mjs`,
`compose-map.mjs`, `cook-map.mjs` y `eng::playfield::TileLayerStack`.

## 1. Principio

La base cocida es un **prefijo opaco**. Una capa viva no puede tener capas
cocidas por encima: si el agua está viva, cualquier capa que se dibuje encima
(Road, Shadows, Object Layer 1...) debe seguir siendo overlay/occluder. Si se
funde en la base, taparía o sería tapada en el orden equivocado.

La regla se valida antes de cocinar:

```text
baked_through = última capa del prefijo cocido (inclusive)
capas <= baked_through : baked | base
capas >  baked_through : overlay | animated | occluder | parallax | skip
```

`base` es una capa opaca que se puede tratar como prefijo; no es una capa viva.

## 2. Modos de capa

| Modo | Runtime | Uso |
|---|---|---|
| `baked` | ya está en el bitmap base | estático, sin transparencia fuera de la base |
| `base` | copia opaca en la banda entrante | base viva opaca; no se parchea por frame |
| `overlay` | celda enmascarada encima de la base | transparencia estática o animada |
| `animated` | overlay con frame actual de `TileAnimator` | agua, flores, fuego |

| `live` | toda la capa se compone en runtime; las celdas estáticas se emiten como grupos de 1 frame | capas vivas completas, prototipos |
| `occluder` | solo tras actores y en dirty rects | copas, puentes, aleros |
| `parallax` | campo separado/DPF | no es una capa libre más |
| `skip`/`collision` | no se dibuja | colisión o datos auxiliares |

Una capa `live` estática no replica la **opacidad de capa** de Tiled: si la capa usa blending global (p. ej. sombras), se resuelve con un bitplane/índice de sombra o dithering; no se asume alpha blending general. La opción de cocinado debe declarar `shadow_mode: bitplane|dither|baked`. Por eso el prefijo cocido sigue siendo la opción segura para capas superiores con opacidad.



El cooker no acepta un tile transparente “por convención”: una capa `overlay`,
`animated` u `occluder` necesita datos de máscara. La base opaca no lee máscara.

## 3. JSON mínimo

```json
{
  "render": {
    "schema": 1,
    "baked_through": "RockSlopes_Auto",
    "modes": {
      "Ground": "baked",
      "Flowers": "baked",
      "Road": "baked",
      "Shadows": "baked",
      "Object Shadows": "baked",
      "RockSlopes_Auto": "baked",
      "Water": "animated",
      "Object Layer 1": "animated"
    },
    "opaque": ["Ground", "Road", "Shadows"],
    "occluders": []
  },
  "layers": {
    "Ground": "baked",
    "Flowers": "baked",
    "Road": "baked",
    "Shadows": "baked",
    "RockSlopes_Auto": "baked",
    "Water": "animated"
  },
  "objects": { "Object Layer 1": "animated" },
  "animations": {
    "mode": "live",
    "layers": ["Water"],
    "objects": ["Object Layer 1"],
    "max_visible_cells": 64
  },
  "animation_budget": {
    "policy": "preserve50",
    "target_fps": 50,
    "degrade_order": ["Water", "Flowers", "Object Layer 1"]
  }
}
```

- `render.modes` es la fuente de verdad si existe; `layers`/`objects` se
  mantienen por compatibilidad con el cooker actual.
- `baked_through` es inclusivo. Si no se declara, el validador lo infiere y
  avisa de capas cocidas por encima de una viva.
- `opaque` es una pista: permite copia directa en una capa viva. Si el arte
  tiene transparencia, el cooker debe rechazarla o el runtime debe usar máscara.
- `occluders` debe coincidir con las capas en modo `occluder`.
- `animations.mode: "patch"` es **legacy** (base con primer frame); se acepta con
  aviso para no romper assets antiguos, pero no es el camino recomendado.

## 4. Fallback y presupuesto

El objetivo por defecto es **preservar 50 fps**. El desarrollador puede elegir
`preserve50`, `target` o `off`.

Cuando el frame se acerca al presupuesto:

1. Se mantiene el scroll y el dibujado de la base.
2. Se congela el reloj de animación de las capas de menor prioridad, empezando
   por `Water`, después `Flowers` y por último `Object Layer 1` (fuego).
3. **No se borra el dibujado**: la celda conserva el último frame válido. El
   siguiente frame que entre por scroll se pinta con ese frame, no con un hueco.
4. Si el presupuesto sigue sin llegar, se reduce el número de celdas vivas
   actualizadas por frame o se acepta 2 campos (25 fps) según la política.

La prioridad por defecto es:

```text
fondo/agua < decoración/flores < foreground/fuego
```

El fuego se degrada último porque es el elemento con más lectura de primer plano.
La política es opcional en el cook-plan y activa por defecto si no se especifica.

### Base de respaldo

La base no debe contener el primer frame de una capa viva si el frame animado
tiene transparencia: revelaría el frame viejo. El flujo correcto es:

- base cocida **sin** las celdas vivas (debajo queda hierba/suelo);
- al entrar una celda, se dibuja su frame actual (o congelado) sobre esa base;
- si la animación se congela, se sigue mostrando el último frame, no un hueco.

Así el fallback no necesita un segundo bitmap completo: la base limpia + un frame
válido por celda viva es suficiente. Si el desarrollador quiere una variante
totalmente estática, cuece otra selección con `baked_through` al final y sin
`animations`.

## 5. Orden y occluders

```text
base cocida (opaca)
  -> overlays vivos, de abajo arriba
    -> actores
      -> occluders (solo dirty rects)
        -> HUD
```

Un `Occluder` no se pinta en el pase normal. Se redibuja después de los actores
solo si su celda intersecta un rectángulo sucio. Un occluder estático puede
estar también en la base para el caso normal; el parche de occlusión es la
copia de primer plano que se usa cuando un actor pasa por detrás.

## 6. Optimización de dibujo

- Base: escritura directa, sin leer destino, sin máscara, sin save-under.
- Overlay transparente: cookie-cut solo si hace falta conservar el fondo.
- Overlay opaco (`opaque: true`): copia directa, sin máscara.
- Actor: save-under del engine, no del tile.
- Occluder: solo dirty rects, nunca la capa completa por frame.
- El scroll compone la **banda entrante**, no la pantalla completa.

## 7. Presupuesto

El presupuesto se declara, no se estima:

- coste de base por banda: `blits = celdas_banda`;
- coste de overlay: `celdas_banda * planos` si es enmascarado, `celdas_banda` si
  es opaco;
- coste de animación: `celdas_visibles_que_cambian * planos`;
- coste de occluder: solo celdas que intersectan dirty rects.

`TileLayerStack` expone modos y prioridad; el contador de trabajos y el reloj de
frame son la señal para congelar capas.
