# HOST-497: SpriteBank/GameView y `set_position`

Valida la capa de presentación de sprites:

- `SpriteBank<4>` liga una DATA Chip prestada, devuelve la rebanada de cada slot y publica los `SpriteConfig` en `SpriteManager`.
- `SpriteGameView`/`GameView` aplica el banco a una escena y solicita la invalidación de su copperlist.
- `SpriteDirector::set_position` mueve un actor simple en coordenadas de pantalla y actualiza el HSTART interno sin exponer `ActorDesc`; un id inválido se rechaza y `clear` vacía el director.
- `SpritePatchTable`/`emit_placements_patchable`: captura los `MoveSlot` de POS/CTL, parchea en una lista simulada y rechaza el parche si la DATA o la disposición cambian.

```bash
bash tools/run-host-tests.sh tests/host/scene/497_sprite_bank
```

Ejecuta además un fake scene sin hardware: el banco no toca registros, solo configura el `SpriteManager`.
