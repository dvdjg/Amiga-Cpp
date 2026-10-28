# Contrato del cook-plan y `TileLayerStack`

Este documento fija la frontera entre lo que se **cuece** en host y lo que se compone en runtime. Es la referencia para `tools/tiled/validate-cook-plan.mjs`, `compose-map.mjs`, `cook-map.mjs` y `eng::playfield::TileLayerStack`.

## 1. Principio

La base cocida es un **prefijo opaco**. Una capa viva no puede tener capas cocidas por encima: si el agua está viva, cualquier capa que se dibuje encima (Road, Shadows, Object Layer 1...) debe seguir siendo overlay/occluder. Si se funde en la base, taparía o sería tapada en el orden equivocado.

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

Una capa `live` estática no replica la **opacidad de capa** de Tiled: si la capa usa blending global (p. ej. sombras), se resuelve con un bitplane/índice de sombra o dithering; no se asume alpha blending general. La opción de cocinado debe declarar `shadow_mode: baked|dither`. `baked` compone la sombra contra el fondo con alpha al hornear; `dither` aproxima la cobertura con una matriz ordenada 2x2 a partir del alfa de origen (alfa 64 = 1 de cada 4 píxeles, 255 = opaco) y usa `render.shadow_color` (por defecto negro) como índice. `bitplane` se rechaza: el perfil EHB usa el sexto plano para half-brights y no queda un plano dedicado a sombra. Por eso el prefijo cocido sigue siendo la opción segura para capas superiores con opacidad.



El cooker no acepta un tile transparente “por convención”: una capa `overlay`, `animated` u `occluder` necesita datos de máscara. La base opaca no lee máscara.

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
    "max_visible_cells": 64,
    "fusion": { "max_cells": 8, "max_duplicate_bytes": 0 }
  },
  "animation_budget": {
    "policy": "preserve50",
    "target_fps": 50,
    "degrade_order": ["Water", "Flowers", "Object Layer 1"]
  }
}
```

- `render.modes` es la fuente de verdad si existe; `layers`/`objects` se mantienen por compatibilidad con el cooker actual.
- `baked_through` es inclusivo. Si no se declara, el validador lo infiere y avisa de capas cocidas por encima de una viva.
- `opaque` es una pista: permite copia directa en una capa viva. Si el arte tiene transparencia, el cooker debe rechazarla o el runtime debe usar máscara.
- `occluders` debe coincidir con las capas en modo `occluder`.
- `animations.mode: "patch"` es **legacy** (base con primer frame); se acepta con aviso para no romper assets antiguos, pero no es el camino recomendado.
- `animations.fusion` controla la fusión automática de clusters: `max_cells` limita la caja (8) y `max_duplicate_bytes` el banco extra si un gid se repite (0 = solo gids únicos). El coste que se compara es el banco duplicado frente a los jobs ahorrados (`2 × (celdas - 1)` por cambio); el informe `animations.json` lo registra por grupo.

## 4. Fallback y presupuesto

El objetivo por defecto es **preservar 50 fps**. El desarrollador puede elegir `preserve50`, `target` o `off`.

Cuando el frame se acerca al presupuesto:

1. Se mantiene el scroll y el dibujado de la base.
2. Se congela el reloj de animación de las capas de menor prioridad, empezando por `Water`, después `Flowers` y por último `Object Layer 1` (fuego).
3. **No se borra el dibujado**: la celda conserva el último frame válido. El siguiente frame que entre por scroll se pinta con ese frame, no con un hueco.
4. Si el presupuesto sigue sin llegar, se reduce el número de celdas vivas actualizadas por frame o se acepta 2 campos (25 fps) según la política.

La prioridad por defecto es:

```text
fondo/agua < decoración/flores < foreground/fuego
```

El fuego se degrada último porque es el elemento con más lectura de primer plano. La política es opcional en el cook-plan y activa por defecto si no se especifica.

### Base de respaldo

La base no debe contener el primer frame de una capa viva si el frame animado tiene transparencia: revelaría el frame viejo. El flujo correcto es:

- base cocida **sin** las celdas vivas (debajo queda hierba/suelo);
- al entrar una celda, se dibuja su frame actual (o congelado) sobre esa base;
- si la animación se congela, se sigue mostrando el último frame, no un hueco.

Así el fallback no necesita un segundo bitmap completo: la base limpia + un frame válido por celda viva es suficiente. Si el desarrollador quiere una variante totalmente estática, cuece otra selección con `baked_through` al final y sin `animations`.

### 4.1 Solape con sprites estáticos

Una celda animada puede quedar debajo de un objeto estático posterior (por ejemplo, la copa de un árbol sobre el agua). El runtime pinta el overlay animado **después** del bake, así que ese objeto horneado desaparecería. El cooker resuelve el solape en dos pasos:

- `animations` no emite como vivas las celdas cubiertas por un objeto estático de una capa/objeto posterior (los objetos con animación propia no cuentan como estáticos). La consulta es por bounding box del objeto; las capas de tiles no suprimen celdas completas porque pueden tener transparencia.
- `render.bake_animated_first_frame: true` incluye el primer frame de las capas animadas en la base. Las celdas suprimidas conservan así agua/flores + objeto, y el parche no puede borrar el objeto.

Es una regla **conservadora**: no redibuja el sprite superpuesto en cada frame, sino que deja de animar la celda que quedaría tapada. Si se necesita animar debajo del sprite, el camino correcto es emitirlo como objeto `foreground`/`occluder` y dibujarlo después del parche (208); el cooker no inventa esa separación.

## 4.2 Máscara solo si hace falta

El cooker inspecciona todas las máscaras de un grupo: TileAnimationGroupView.flags & kTileAnimationFlagMasked solo se activa si algún frame tiene un bit transparente. Un grupo opaco usa CopyRect, no cookie-cut, y el parche no captura/restaura save-under para sus celdas. En Beginning Fields, 8 de 59 grupos (27 celdas) son opacos; el resto (flores, agua y fogata) conserva transparencia y necesita cookie-cut + save-under. La decisión se registra en nimations.json (lags) y se prueba en HOST-500.

## 5. Objetos en una sola capa

Tiled solo tiene `Object Layer 1`; no hace falta separar capas para cocer casas y mantener props en primer plano. Lo recomendable es **no tocar el TMX original** y trabajar con una **copia de cocina** (`*.cook.tmx`) donde cada objeto declare su rol mediante clase/propiedades:

```xml
<object id="12" class="Building" ...>
  <properties>
    <property name="cook" value="baked"/>
  </properties>
</object>

<object id="37" class="Prop" ...>
  <properties>
    <property name="cook" value="foreground"/>
    <property name="sort_y" value="base"/>
  </properties>
</object>

<object id="50" class="Tree" ...>
  <properties>
    <property name="cook" value="foreground"/>
    <property name="sort_y" value="base"/>
    <property name="occluder" value="canopy"/>
  </properties>
</object>
```

- `cook=baked`: el objeto se funde en la base, como una casa de fondo.
- `cook=foreground`: el objeto no se hornea; se emite como `MapObject`/sprite (o grupo de tiles) y participa en el orden por Y.
- `sort_y=base`: la clave es `object.y + object.height` (el pie del objeto). El actor usa la misma clave (`foot_y`). Los objetos con clave menor se dibujan antes del actor; los mayores, después.
- `occluder=canopy`: separa la parte que debe tapar al actor (copa, tejado) y mantén el tronco/base en el fondo o en el objeto ordenado. Sin esta separación, un árbol grande no puede ser a la vez fondo y primer plano.
- Si una casa se puede rodear y el personaje puede pasar por detrás, **no se hornea entera**: se cuece la fachada de fondo y se deja tejado/pared delantera como `foreground`/`occluder`. Si es un decorado no atravesable, `baked` es correcto.

El cook-plan puede llevar reglas de respaldo para una copia aún sin propiedades:

```json
"objects": {
  "Object Layer 1": {
    "default": "foreground",
    "sort_y": "base",
    "rules": [
      { "gid": [477, 478, 479, 480], "mode": "baked" },
      { "class": "Building", "mode": "baked" }
    ]
  }
}
```

Las reglas por `gid` son una muleta para el fixture; para producción, clases y propiedades en la copia de cocina son la fuente de verdad. Los nombres de los objetos están vacíos, así que no son una clave fiable.

`map-objects.mjs` resuelve cada objeto así: propiedades del objeto (`cook`, `sort_y`, `occluder`) → reglas de `objects.<capa>.rules` en orden (gana la primera coincidencia por `id`/`gid`/`class`/`type`/`name`) → `default`. Los modos son `baked`, `foreground`, `occluder`, `collision` y `skip`; `sort_y` es `base` (pie = `y + h`) o `top`; un `foreground` con `occluder=canopy` pasa a `occluder` y se dibuja después de los actores. El cooker emite `objects.h` con `MapObjectView` (posición de mundo, `foot_y`, `sort_key`, imagen y rol) ordenado por profundidad, `object_sheet.bin` (planos con guarda por fila) y `object_masks.bin` (1bpp). El runtime consume `eng::map::MapObjectsView` y `upper_bound_foreground(foot_y)` para intercalar props y actores; los objetos escalados, rotados o sin `gid` se rechazan en host. La demo 208 es el consumidor de referencia. Los objetos con animación de Tiled conservan sus frames contiguos en el mismo sheet y una tabla `MapObjectAnimationView` por objeto; `map_object_sprite` + `MapSpriteCache` los dibujan en píxeles arbitrarios, sin exigir alineación a tile.

## 6. Orden y occluders

```text
base cocida (opaca)
  -> overlays vivos, de abajo arriba
    -> actores
      -> occluders (solo dirty rects)
        -> HUD
```

Un `Occluder` no se pinta en el pase normal. Se redibuja después de los actores solo si su celda intersecta un rectángulo sucio. Un occluder estático puede estar también en la base para el caso normal; el parche de occlusión es la copia de primer plano que se usa cuando un actor pasa por detrás.

## 7. Optimización de dibujo

- Base: escritura directa, sin leer destino, sin máscara, sin save-under.
- Overlay transparente: cookie-cut solo si hace falta conservar el fondo.
- Overlay opaco (`opaque: true`): copia directa, sin máscara.
- Actor: save-under del engine, no del tile.
- Objeto compuesto: `cook-animations` fusiona automáticamente clusters rectangulares de celdas animadas con la misma cadencia en un grupo de un sprite (un blit y un save-under para toda la caja); es una regla general, no una excepción por objeto. Si un gid se repite, solo se fusiona dentro del presupuesto `animations.fusion.max_duplicate_bytes` para no duplicar el banco sin control.
- Occluder: solo dirty rects, nunca la capa completa por frame.
- El scroll compone la **banda entrante**, no la pantalla completa.

## 8. Presupuesto

El presupuesto se declara, no se estima:

- coste de base por banda: `blits = celdas_banda`;
- coste de overlay: `celdas_banda * planos` si es enmascarado, `celdas_banda` si es opaco;
- coste de animación: `celdas_visibles_que_cambian * planos`;
- coste de occluder: solo celdas que intersectan dirty rects.

`TileLayerStack` expone modos y prioridad; el contador de trabajos y el reloj de frame son la señal para congelar capas.
