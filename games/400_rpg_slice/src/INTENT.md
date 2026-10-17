# INTENT — 400_rpg_slice (vertical slice RPG: aldea, bosque y mazmorra)

## Qué hace

Juego de rol mínimo sobre la capa `eng::rpg` (fachada `eng/api`): **tres salas** de 20×14 casillas de 16 px (aldea, bosque y torreón subterráneo) que el jugador recorre con un **guion determinista** o con el pad. La crónica encadena: hablar con la anciana (te encarga cazar lobos y te da espada y pocimas), cazar tres **lobos territoriales** en el bosque (dejan pieles), **vender** las pieles al mercader y **comprar la llave del torreón**, abrir la puerta, entrar en la mazmorra y **abrir el cofre de la reliquia** (+50 monedas) hasta `OBJ:VICTORY`. En el camino hay pocimas que curan, un reloj de mundo, hambre vía `eng::sim` y quests declarativas que comparten hechos (`hunt_done`) sin conocerse entre sí.

## Qué pretende demostrar

1. La **capa RPG completa en un juego real** (68000, A500): hechos persistentes, objetos tipo+instancia, bolsa/equipo, atributos derivados, combate por fases, trueque, cerraduras con etiqueta y quests declarativas sobre hechos — todo sin heap y con presupuesto de un frame.
2. Que la **doble ruta del render** es viable a coste bajo: la **sala se cocina una vez por sala en el setup** (tiles 16×16 desde una **tabla ROM** por plano) y se **publica solo al cambiar**; los actores son **parches 16×16 en streaming** (`copy_now`) y las estelas se **borran restaurando la casilla desde la imagen**; el HUD vive dentro de la imagen y se republica **solo la banda** (16 filas) cuando cambia.
3. El **control de coste** aprendido en la familia RPG (DT-025 y demo 001): nada de repintar la sala por frame; cocciones fuera de la ruta de juego; HUD por palabras y por bandas.
4. La **secuencia de historia** como contrato verificable: la crónica se completa sola (guion) hasta `OBJ:VICTORY` y el HUD refleja hechos, oro, inventario y objetivo vigente.

## Elementos en pantalla

- **Sala** cocinada: piedra (gris), hierba (verde), árboles y agua (bosque), sendero (marrón), muros y suelo de mazmorra; decorado horneado por sala (anciana, mercader, cama; cofre cerrado en el torreón).
- **Actores** (parches): jugador (rojo con cabeza amarilla; espada dorada al equiparla), lobos (oscuros con ojos blancos), pocimas (verdes), pieles (amarillas), llave (dorada), reliquia (cian con base dorada), cofre abierto y puerta cerrada del torreón.
- **HUD** (dos líneas, Font8, banda inferior de 16 filas): `RPG SLICE vMAJOR.MINOR.BUILD F <frames> HP.. ATK..` y `G<oro> PELT.. POT.. OBJ:<objetivo>`.

## Evolución temporal (guion determinista, ~1.300 frames a ~46 fps)

1. **F 0–25**: la aldea; el jugador camina a la anciana y dispara: encargo (hecho `talked`), espada equipada (ATK 4→7) y dos pocimas al suelo.
2. **F 25–60**: recoge las pocimas (`POT 2`) bordeando a la anciana.
3. **F 60–190**: cruza la aldea hacia el este y entra en el bosque (puerta).
4. **F 190–420**: caza los tres lobos (dos golpes con enfriamiento 18 por lobo; tres amagos por lobo en el guion), recoge las tres pieles y la pocima del sendero (`PELT 3`, `POT 3`).
5. **F 420–560**: vuelve a la aldea, vende las pieles (`G 30`), compra la llave (`G 10`, llave en bolsa) y vuelve al este.
6. **F 560–650**: abre la puerta del torreón (llave con etiqueta) y entra en la mazmorra.
7. **F 650–820**: rodea los muros hasta el cofre, lo abre (`G 60`, reliquia + pocima al suelo) y recoge la reliquia: `OBJ:VICTORY` (quests cerradas).
8. **F 820+**: patrulla entre dos casillas para que la animación no pare.
