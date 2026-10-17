# HOST-459: interacción y puente con la simulación (`eng::rpg`)

Test host de `engine/include/eng/rpg/{interaction,sim_bridge}.hpp` (hito M1): cerraduras, recoger/soltar y el puente con `eng::sim::Needs`.

## Qué comprueba

1. **`LockSet`**: la llave es **etiqueta + metadato de instancia** (no una clase por llave); rechaza llaves de otra puerta, objetos sin etiqueta `key` y handles caducados; abrir es idempotente y la llave no se consume.
2. **`pick_up`**: `bag_add` con **división por `stack_max`** (una pila completa entra; el resto queda en el suelo), bolsa llena devuelve el resto sin perder nada y las entradas caducadas se limpian.
3. **`drop_item`**: sale de la bolsa y entra al suelo; con el suelo lleno el objeto **no se pierde** (vuelve a la bolsa).
4. **`consume`** y **puente**: comer consume exactamente una unidad y alimenta `eng::sim::Needs` (la simulación propone, el RPG interpreta); lo que no es comida se rechaza sin efectos; al agotar la pila la instancia se destruye y sale de la bolsa; `rest_with_item` baja la fatiga.

## Salida de referencia

```
RPG interaction (cerraduras, suelo, puente sim):
OK: RPG interaction (LockSet/pick-up/drop/consume/bridge)
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/rpg/459_rpg_interaction
```
