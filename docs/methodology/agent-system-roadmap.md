# Sistema de agentes para el roadmap Amiga (MCP, batería, engine)

Define **roles de agente** (pueden ser chats distintos en Cursor o turnos con un solo agente), **entregables**, **supervisión** y **actualización obligatoria** de [amiga-implementation-roadmap.md](amiga-implementation-roadmap.md).

**Documento de verdad del backlog:** [amiga-implementation-roadmap.md](amiga-implementation-roadmap.md).  
**Especificación de pruebas y visión IA:** [amiga-test-battery-spec.md](amiga-test-battery-spec.md).  
**Runbook general del agente:** [agent-runbook.md](agent-runbook.md).

---

## 1. Principios de supervisión

1. **Ningún ítem se marca HECHO** sin cumplir el **criterio de cierre** de la tabla correspondiente en el roadmap.
2. **Siempre** actualizar `doc/amiga-implementation-roadmap.md` (y si aplica `amiga-test-battery-spec.md` §2) en el **mismo PR o commit** que implementa el cambio.
3. **Compilación:** cambios en `Cursor-Amiga-C` → `bash scripts/verify-build.sh` (o equivalente). Cambios en `mcp-winuae-emu` → `npm run build`.
4. **Evidencia para Fase C:** mínimo `README.md` del caso + `evidence/` con al menos una captura o texto que explique verificación MCP/manual.
5. El **Orquestador** (o tú en modo revisión) rechaza cierres que solo digan “hecho” sin filas actualizadas.

---

## 2. Roles de agente

| Rol | Responsabilidad | Repos / rutas | Fases roadmap |
|-----|-----------------|---------------|---------------|
| **G0 — Orquestador** | Elegir el siguiente ID por prioridad; validar DoD; actualizar tablas §1 y resúmenes; detectar bloqueos. | Todo el workspace, ambos repos | A–E |
| **G1 — MCP / WinUAE** | Herramientas GDB, monitor, snapshot, bitmap, ADF, relocs. | `mcp-winuae-emu/`, `WinUAE-DBG` / fork | A (A-MCP-*) |
| **G2 — Infra batería** | Plantillas, scripts `run-battery-*`, índice `tests/amiga-battery/`, launcher menú. | `Cursor-Amiga-C/tests/`, `scripts/` | B (B-TEST-*) |
| **G3 — Batería gráfica** | Implementar efectos T/C/B/S/A/M/AG; chip RAM, copper, blitter. | `app/effects/`, `tests/amiga-battery/*` | C |
| **G4 — Engine** | APIs `engine_*` alineadas a [demoscene-effects-integration.md](demoscene-effects-integration.md). | `engine/` | D |
| **G5 — QA / Verificación** | Ejecutar build, pruebas smoke MCP, revisar `evidence/`, proponer corrección de estados PARCIAL. | scripts, MCP | Todas |

**Nota Cursor:** Puedes abrir **un chat por rol** (título: `G1 MCP`, `G3 Batería`…) y pegar la **plantilla de §6**. El Orquestador puede ser tú o un agente cuyo único trabajo sea “siguiente tarea + revisión de cierre”.

---

## 3. Orden de trabajo recomendado (cola)

Desbloquear primero herramientas que reducen fricción en el resto:

```text
A-MCP-02 (snapshot) → A-MCP-03 (bitmap)  en paralelo conceptual con  B-TEST-01 (plantilla)
     ↓
A-MCP-01 (ADF matriz documentada + tests)
     ↓
B-TEST-02, B-TEST-03, B-TEST-04
     ↓
T01 → T02 → C01 → B01 → B02  (orden spec §9)
     ↓
… resto de Fase C según prioridad …
     ↓
D-ENG-* cuando haya duplicación entre efectos
```

El **Orquestador** no debe saltar a T04 antes de tener **T01** cerrado salvo decisión explícita documentada en el roadmap (nota en la fila).

---

## 4. Definition of Done (DoD) por tipo

| Tipo | DoD mínimo |
|------|------------|
| **A-MCP-*** | Código en `mcp-winuae-emu`, `npm run build` OK, README MCP actualizado, fila en §2 Fase A → **HECHO** o **PARCIAL** con subcriterios. |
| **B-TEST-*** | Archivos en repo, script ejecutable o documentado, fila Fase B actualizada. |
| **T/C/B/S/A/M/AG** | Código + integración menú o script + `evidence/` + README del caso; fila §4 → **HECHO**; opcionalmente entrada en `tests/amiga-battery/README.md`. |
| **D-ENG-*** | API en `engine.h`, uso en al menos un efecto o test; [engine-roadmap.md](engine-roadmap.md) tocado si aplica. |

---

## 5. Supervisión: checklist del Orquestador (cada entrega)

- [ ] ¿El **ID** del roadmap coincide con el trabajo del PR/commit?
- [ ] ¿Estado actualizado (**HECHO** / **PARCIAL**) con honestidad?
- [ ] ¿§1 “Resumen: qué falta” sigue siendo coherente (conteos o texto)?
- [ ] ¿Hay **compilación** verificada (log o cita de comando)?
- [ ] Para **PARCIAL**: ¿está escrito **qué falta** en la columna Notas o Criterio?
- [ ] ¿Enlaces rotos entre `amiga-test-battery-spec.md` ↔ `amiga-implementation-roadmap.md`?

---

## 6. Plantillas de prompt (copiar al iniciar un agente)

### G0 — Orquestador

```text
Eres el Orquestador del roadmap Amiga. Lee doc/amiga-implementation-roadmap.md §1 y §2–§4.
1) Propón el ÚNICO siguiente ID a implementar según la cola del doc/agent-system-roadmap.md §3.
2) En el cierre de otra sesión, verifica DoD según doc/agent-system-roadmap.md §5 y actualiza las tablas del roadmap si algo se completó.
No implementes código de producto salvo actualizaciones de documentación de seguimiento.
```

### G1 — MCP

```text
Eres el Agente MCP (G1). Objetivo: cerrar el ID A-MCP-__ del doc/amiga-implementation-roadmap.md Fase A.
Repositorio: mcp-winuae-emu (y si aplica WinUAE-DBG). Criterio de cierre en la tabla del roadmap.
Al terminar: npm run build, actualizar README del MCP, actualizar fila A-MCP-* y §1 del amiga-implementation-roadmap.md.
```

### G2 — Infra batería

```text
Eres el Agente Infra batería (G2). Objetivo: ID B-TEST-__ en Cursor-Amiga-C.
Crea plantillas/scripts según criterio del roadmap. verify-build.sh si tocas código de app.
Actualiza amiga-implementation-roadmap.md Fase B y tests/amiga-battery/README.md.
```

### G3 — Batería gráfica

```text
Eres el Agente Batería (G3). Objetivo: implementar el ID de prueba (ej. T01) según doc/amiga-test-battery-spec.md §8.
Sigue convenciones §3–§5 del spec. Entrega: código + evidence/ + README del caso.
Marca la fila en amiga-implementation-roadmap.md §4 como HECHO con ruta al código.
```

### G4 — Engine

```text
Eres el Agente Engine (G4). Objetivo: D-ENG-__ del roadmap, alineado con demoscene-effects-integration.md.
No romper demo existente; verify-build.sh obligatorio.
Actualiza amiga-implementation-roadmap.md Fase D y engine-roadmap.md si corresponde.
```

### G5 — QA

```text
Eres el Agente QA (G5). Sin implementar features nuevas salvo scripts de verificación.
Ejecuta verify-build.sh, revisa evidence/ del último ID cerrado, valida que el roadmap refleja la realidad.
Propón cambiar PARCIAL → HECHO o HECHO → PARCIAL si encuentras lagunas.
```

---

## 7. Ritmo y expectativas

| Ritmo | Acción |
|-------|--------|
| **Por tarea** | Un ID por sesión de agente cuando sea posible; evita mezclar A-MCP-02 con T07 en el mismo PR sin dependencia. |
| **Revisión** | Tras cada hito de Fase A o B, el Orquestador hace checklist §5. |
| **Mensual / hito mayor** | Releer §1 y arco §9 del spec; ajustar prioridades si el fork WinUAE bloquea una herramienta. |

---

## 8. Referencias

- Puntero corto: [agents/README.md](agents/README.md)
- [amiga-implementation-roadmap.md](amiga-implementation-roadmap.md)
- [amiga-test-battery-spec.md](amiga-test-battery-spec.md)
- [mcp-live-coding-workflow.md](mcp-live-coding-workflow.md)
- [agent-runbook.md](agent-runbook.md)
