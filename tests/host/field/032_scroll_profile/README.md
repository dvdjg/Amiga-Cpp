# Test HOST-032: perfil de scroll estático (SpeedPolicy)

Respalda `eng::field::ScrollProfile` (`engine/include/eng/field/scroll_profile.hpp`): la selección
**compile-time** del comportamiento de scroll que el desarrollador elige con un solo tipo
(`ScrollProgressive`, `ScrollFast1/2/4` o un perfil a medida).

Se comprueban: relleno por frame (`fill_tiles`/`prefill`), guarda de lookahead (`guard_px`), paso
máximo por tamaño de tile (`max_step_px`), el default que reproduce el comportamiento clásico
(sin override), la escala con `tile=32`, un perfil a medida con dirección laceda y
`StripPrerender`. El invariante `guarda >= relleno + 1` lo verifica el `static_assert` del perfil
en compilación.

```bash
bash tools/run-host-tests.sh tests/host/field/032_scroll_profile
```

Contexto: `docs/engine/architecture/FAST_SCROLL.md`.
