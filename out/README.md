# `out/` — reglas canónicas de salida

`out/` es el directorio raíz de **todo lo generado** por herramientas, builds,
capturas, informes y assets producidos por pipelines. Es gitignored: se puede
borrar y regenerar con total seguridad.

La especificación completa de organización del repo está en
`docs/STRUCTURE.md` (§6.1, §11). Este README es el recordatorio rápido que
deben respetar las herramientas y sus invocaciones.

## Estructura canónica

```
out/
├── demos/<demo>/<CONFIG_ID>/        → builds (elf/exe/map/listing) — p. ej. out/demos/107_xlimited_corkscrew/A500_debug/
├── run/<demo>[/<CONFIG_ID>]/        → capturas del runner (screenshot.png, sequence/, run-report.json)
├── assets/<pipeline>/…              → assets generados (ehb/, demo202/, tile-demos/, …)
├── profile/<nombre>/                → profiling (bin + frames + profile-summary.json + informes)
├── regression/<YYYYmmdd-HHMMSS>/    → informes de regresión completa
├── framescope/<demo>/               → informes FrameScope
├── analysis/<demo>/                 → asserts/pixel-assert/overlays
├── debug-current/                   → build de depuración interactiva (F5)
├── playground/<experimento>/        → salidas de playground y medidas de calidad
└── tmp/                             → salidas temporales/ad-hoc efímeras
```

## Reglas obligatorias

1. **Siempre `--out` con defecto canónico.** Cualquier tool que genere archivos
   admite `--out <ruta>`, y su defecto debe ser uno de los subdirectorios
   canónicos de aquí (nunca la raíz `out/` ni una ruta ad-hoc).
2. **Nada suelto en la raíz de `out/`.** Prohibido `out/mi_experimento/` sin más:
   va a `out/playground/<experimento>/` o `out/tmp/`.
3. **Nombres auto-descriptivos.** Los ficheros de salida deben expresar qué son y
   con qué parámetros se produjeron (p. ej. `demo201_reconstruct_32c_kmeans_floyd_720x416.png`).
4. **Determinista.** Mismo comando → misma estructura de salida, tanto si lo
   invoca un humano como una IA.
5. **Intermedios de compilación en `obj/`**, no en `out/` (`obj/demos/<demo>/<CONFIG_ID>/`).
6. **Regenerable.** Si un pipeline regenera una carpeta, debe poder borrarse sin
   romper nada; los resultados canónicos congelados viven en `artifacts/`, no aquí.

## Antes / después

Esta carpeta solía acumular directorios ad-hoc (`auto_green`, `det-r1`,
`exp_*`, `slash_out_test`, `vision_verde`, `perc_compare*`, `demo202` suelto,
`out/out`, …). Desde la reorganización, todo lo ad-hoc se reubica bajo
`assets/<pipeline>/`, `playground/<experimento>/` o `tmp/`. Si encuentras una
salida que no encaja en esa regla, muévela al lugar correcto y corrige la ruta
en la tool que la genera.