# Estrategias para juegos 3D de alto rendimiento en A500

Objetivo de **referencia**: **25 fps** (2 campos PAL), **no un gate universal**. Cada demo puede declarar una excepción medida; 090 y 092 lo hacen. El presupuesto útil es de unos **283.000 ciclos/frame**; por encima se cae a 3 o más campos. Estas estrategias están medidas en las demos 079, 090, 091, 092, 095 y 096.

## 1. Elegir la ruta de render por contenido

| Contenido | Ruta recomendada | Evidencia |
|---|---|---|
| Alambre de un color | `Scene::collect_wire_edges` + `retro::wire_lines` | 091: **30,6 fps release**; 079: 39,7 |
| Props convexos de color plano, sombras, selección | `render_silhouette` | 096: **26,9 fps**; A/B por cara 17,7 |
| Caras con materiales o patrones | `Scene::render` por cara | 091 relleno: 8,4 fps; 092: 8,3 |
| Poliedros facetados con XOR por plano | `flat_shade_xor` (090) | 16,3 fps; se conserva como referencia |
| Mezcla con otros blits/planes | `FramePlan` | 091 plan: 24,1 fps; útil si hay más que líneas |

Reglas:

- **No usar `FramePlan` para alambre puro**: el plan y `Surface::draw_line` cuestan más que el propio trazado. `wire_lines` fija el preludio una vez y programa las líneas directamente.
- **No agrupar líneas en jobs**: el `LineBatch` medido empeora (21,2 frente a 22,0 fps). El coste está en cada línea del Blitter, no en el alta del job.
- **Silhouette no es una ruta universal**: acelera 1,5x frente a caras en objetos de un color, pero no conserva materiales ni detalle.
- **Fondo estático**: dibujar una vez por buffer los props que no cambian (096: 8 rocas) elimina su coste por frame. Limpiar solo las zonas dinámicas.

## 2. Bucle de juego con física desacoplada

Patrón recomendado:

```text
commit del buffer anterior
clear de las zonas dinamicas
sync_scene() con el estado SIMULADO ACTUAL
encolar render (Blitter asincrono)
mientras el Blitter dibuja:
    avanzar fisica fija (>=5 Hz; 2 iteraciones PBD)
wait_blitter
HUD
```

- La física se ejecuta en el **hueco del Blitter**, no antes de encolar el render. El frame muestra el estado anterior; el solver prepara el siguiente.
- Frecuencia mínima: **5 Hz**. En 092 se ejecuta una vez por render (~12,9 Hz) con 2 iteraciones PBD; cumple el mínimo y no bloquea el dibujo.
- Para muchas entidades, dividir el solver en **slices** por frame y usar un acumulador de tiempo fijo; no ejecutar todos los contactos en un frame.
- Interpolar posición/ángulo entre dos estados si la física va a menos Hz que el render. Para un personaje de 3 partes no fue necesario; para multitud sí.
- El estado del solver no debe tocar los nodos que el Blitter está leyendo. `sync_scene` copia el estado actual antes de encolar; el `step` escribe solo el estado del siguiente frame.

## 3. Presupuesto de física y fixed

- Posiciones: q0 (`eng::coord`). Ángulos y límites: q12 en turns. Inercia: q0; inversa q12.
- No usar exponente adaptativo por operación: es float software con ramas y no es determinista en coste.
- `Fixed<s32,16>` no es una alternativa directa: promueve productos a 64 bits (`__muldi3`). `MiniFloat16` cuesta ~28x por operación; reservarlo para masa/inercia o eventos raros.
- Si un cuerpo no cabe en q0, **escalar el mundo en setup o rechazarlo**; no dejar que envuelva.
- Joints baratos: bola, bisagra (`set_hinge`) y telescópico (`set_slide`). No meter contactos rígidos, motores y cinemática en el mismo solver.
- Menos iteraciones (2-3) y un solo paso por frame suelen ser suficientes para personajes/props. Medir antes de subir.

## 4. Presentación y memoria

- Doble buffer es suficiente a 25 fps; triple buffer elimina el tearing cuando el frame dura 4 campos (092). El coste es memoria Chip, no CPU.
- `commit` al inicio del update; completar las páginas antes de `mark_ready`.
- Clear por dirty rect o por objeto, no full-screen, si la escena es pequeña.
- HUD de host fuera de los bitplanes; el HUD de juego en una franja separada.
- Mantener el Blitter ocupado con rellenos/fills y solapar CPU de física mientras tanto.

## 4.1 Perfil real de 092

Perfil release por secciones de 092: **Render 421.365 ciclos (76,7 %)**, **Physics 76.422 (13,9 %)**, **Hud 21.403 (3,9 %)**, **Clear 13.751 (2,5 %)** y espera casi nula. El clear no es el cuello: probar clear por parte, un rectángulo más pequeño o un solo plano no mejoró (12,75 / 12,78 / 12,91 frente a 12,93 fps). El cuello es el relleno de tres siluetas grandes y el coste de generar sus spans/máscaras.

La ruta alternativa de contorno+area fill XOR tampoco mejoró en esta escena (12,81 fps). Activar BLTPRI (nasty) dio 12,80 frente a 12,93: no ayuda cuando el fill depende de spans CPU y esperas por objeto; BLTPRI solo evita que la CPU robe ciclos al Blitter, no acelera la preparación de la máscara. La conclusión: para 25 fps hay que reducir tamaño/número de partes o pasar a bitmaps/sprites pre-renderizados; no hay una optimización de clear que lo arregle.
## 4.1 Fill, clear y lo que no optimiza

En 092 se probaron clear por parte, clear único más pequeño y 1 plano frente a 2: 12,75 / 12,78 / 12,91 frente a 12,93 fps. El clear no era el cuello; el coste restante es el relleno/silueta de tres piezas grandes más la física. Sin física se queda en 15,36 fps, así que el contenido ya no cabe en 2 campos. Reducir planos o rectángulos no arregla un personaje sobredimensionado; hay que reducir partes o usar sprites/bitmaps.
## 5. Casos medidos y qué conservar

- **091**: alambre directo 30,6 fps release. Ruta de juego para wire.
- **096**: silueta de props 26,9 fps release; 1,52x frente a cara. Ruta de juego para flat/LOD.
- **092**: silueta 12,9 fps; física desacoplada y 2 iteraciones. Es un banco de pruebas, no una demo de juego a 25.
- **090**: 16,3 fps con XOR facetado; **se conserva como referencia** de la técnica clásica, no como objetivo de juego.
- **095**: trigger/capas 16,3 fps; validación de G6.

## 6. Checklist antes de añadir una función al API

1. ¿Tiene una ruta de render medida por debajo de 2 campos?
2. ¿Evita trabajo por frame para contenido estático?
3. ¿La física se puede ejecutar en el hueco del Blitter y a >=5 Hz?
4. ¿Los tipos fixed están en el rango correcto y el overflow se rechaza/escala?
5. ¿La función tiene una demo o test que la mida, y no solo compila?