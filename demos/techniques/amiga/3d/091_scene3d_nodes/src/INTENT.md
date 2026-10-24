# Intención de la demo 091 — Scene3D retenida con jerarquía

## Qué hace

Demuestra la **fachada de juego 3D** (`eng::scene3d`, G0–G3 de `ROADMAP_3D_GAME_API.md`) con una escena mínima pero real: un cubo raíz grande en el centro y un cubo hijo pequeño que cuelga de él. El cubo raíz gira en Y/Z; el hijo gira sobre sí mismo y **hereda** la transformación del padre. La demo es el tutorial de cómo un juego describe nodos, transformaciones y materiales sin tocar bitplanes, Blitter, máscaras ni registros. La variante por defecto es **alambre** (`K091_WIRE=1`) para aislar la escena/jerarquía del coste del relleno de caras; `K091_WIRE=0` usa `Scene::render` con materiales de `ModelTable` y el mismo `PolygonFillService` por cara.

## Qué pretende demostrar

Que la fachada `Scene<MaxNodes, MaxVertices>` resuelve el ciclo de vida y la jerarquía de una escena 3D de juego: handles generacionales (`spawn`/`remove`), transform local con rotación Q4.12, composición padre→hijo con `set_parent`, `update_transforms` que solo recompone nodos sucios, cámara `Camera3D<eng::coord>` y render por `render_wire` o por materiales de `ModelTable`. El display y el relleno reutilizan la infraestructura ya validada de las demos 3D (`composition::Scene` + `PolygonFillService`); la lógica de la demo no programa registros ni toca planos.

## Elementos en pantalla

| Elemento | Color | Papel |
|---|---|---|
| Fondo | negro (COLOR00 = 0x000) | lienzo uniforme; el alambre blanco se lee sin halos |
| Cubo raíz | blanco (COLOR15) | sólido de 28 unidades; gira en Y/Z y define la transformación de la rama |
| Cubo hijo | blanco (COLOR15) | sólido de 16 unidades ligado al raíz; gira sobre sí mismo y orbita con el padre |
| HUD de host | blanco sobre negro | nombre + número + versión + frames + fps + telemetría `E24 J96 OK` |
| Rótulo de franja | blanco | nombre de la demo en la franja inferior del bitmap (refuerzo) |

## Evolución temporal

Ambos cubos rotan de forma continua: la fase avanza 12 unidades por frame (módulo 4096) y alimenta `math3d::load_rotate` con ángulos distintos para el raíz y para el hijo. La posición del hijo en pantalla cambia porque la rotación del padre arrastra su transform local; en algunas poses se solapa parcialmente con el raíz. El doble buffer publica al inicio de cada `update` la página completa anterior, de modo que el raster nunca ve el clear ni la página a medio pintar. La secuencia de validación (4 frames separados) muestra poses distintas y el contador de frames del HUD.
