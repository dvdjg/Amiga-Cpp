# INTENT — 001_rpg_dungeon (capa RPG del engine)

## Qué hace

Juego de rol mínimo sobre `eng::rpg` (fachada `eng/api`): una mazmorra de una sala por **casillas de 16 px** donde el jugador recorre un guion determinista (o juega con el pad): recoge mineral, **forja** una espada en el yunque (crafteo + mejora + afijo + equipo), abre el cofre (que derrama llave y pócima), **come** la pócima (necesidades de `eng::sim`), y abre la puerta con la llave. El HUD muestra nombre, versión, contador de frames, inventario, ataque derivado, hambre y el objetivo vigente.

## Qué pretende demostrar

1. El **núcleo de mundo** de `eng::rpg` funcionando en 68000: hechos de historia, instancias con generación, bolsa con apilado, equipo con etiqueta exigida, atributos derivados por el pipeline de fases y reloj de mundo — todo sin heap y con cadencia de 50 fps.
2. Los **verbos por intención** de M1 (`pick_up`/`drop`/`try_open`/`consume`) y el **puente con la simulación** (`eat_item` → `eng::sim::Needs`).
3. El **crafteo** de M2 (`craft` transaccional, `reinforce`, `apply_affix`): la espada sale con +3 base, +2 del afijo «de fuego» y +20 % por dos niveles de forja → ATAQUE 14 sobre base 5, visible en el HUD.
4. La lección de coste de OCS aprendida en el desarrollo (registrada como DT-025): los jobs del plan de Blitter sincronizan con el Blitter (miles de ciclos por job), así que la escena **no repinta rectángulos por frame**: la sala se pinta una vez en cada buffer y el resto son **parches 16×16 en ROM copiados con la racha de streaming** (`copy_now`). El HUD se compone por CPU en una tira Chip (cambios de estado) y se publica con un blit por plano cada frame.

## Elementos en pantalla

- Marco gris de muros con el hueco de la puerta a la derecha (naranja cerrada; negro al abrirse) y banda inferior negra reservada al HUD (norma del repo).
- Jugador: bloque rojo con cabeza amarilla; con espada (cian) tras forjarla.
- Yunque (gris con brillo), cofre (marrón; marrón oscuro abierto), pila de mineral (gris claro), llave (dorada) y pócima (verde) en el suelo.
- HUD por palabras (Font8): `RPG DUNGEON vMAJOR.MINOR.BUILD F <frames>`, inventario (`ORE/KEY/POT`), `ATK` derivado, `HUNGER` y `OBJ` (objetivo ligado a hechos).

## Evolución temporal (guion determinista, ~330 frames a 50 fps)

1. F 0–50: el jugador va a la pila de mineral y lo recoge al pisarla (ORE 0→3).
2. F 50–130: se acerca al yunque y dispara: forja y equipa la espada (aparece el cian; ATK 5→14; OBJ pasa a «open the door with the key»).
3. F 130–200: rodea el cofre (casilla bloqueada) y lo abre: derrama llave y pócima.
4. F 200–250: pisa el botín (KEY 0→1, POT 0→1).
5. F 250–290: lejos de todo, dispara: come (POT 1→0; HUNGER baja).
6. F 290–340: va a la puerta y la abre con la llave: el paso queda franco y OBJ = ESCAPED.
7. F 340+: patrulla en bucle para que la animación no pare.

Con pad, el guion se desactiva al primer input humano y se juega con las mismas reglas (disparo = verbo del contexto; el HUD se recompone al cambiar el estado mostrado).

