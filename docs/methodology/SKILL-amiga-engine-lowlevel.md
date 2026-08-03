---
name: amiga-engine-lowlevel
description: "Build, repair, verify, and extend low-level Amiga engine work in the Cursor-Amiga-C project. Use when working in Cursor-Amiga-C on: (1) engine functional tests, battery cases, or evidence pipelines, (2) hardware techniques involving bitplanes, copper, blitter, sprites, VBL, loader paths, or WinUAE validation, (3) investigation of new Amiga game techniques before extraction into engine APIs, or (4) future game feature development that must be grounded in proven battery cases and real hardware-style verification."
---

# Amiga Engine Lowlevel

Use this skill only for the `Cursor-Amiga-C` workspace or a direct fork that preserves the same documentation structure. If the repo does not contain `doc/engine-docs-index.md`, `doc/amiga-test-battery-spec.md`, and `tests/amiga-battery/`, stop and say the skill is project-specific.

## Core workflow

1. Read only the docs needed for the task.
   For document selection, use [references/cursor-amiga-c-doc-map.md](references/cursor-amiga-c-doc-map.md).
2. Treat every non-trivial Amiga task as phased low-level work, not as generic feature coding.
3. Before implementing, model the scene as persistent machine state, not as a stateless draw call.
4. Before implementing, build a dependency inventory:
   - what the demo needs technically,
   - which pieces already exist in `engine/`,
   - which pieces already exist as proven battery cases,
   - which exact part is the new hypothesis under test.
5. Before implementing, fill a short contract using the repo template:
   `doc/amiga-lowlevel-technique-contract-template.md`
6. Reuse proven battery cases and existing `engine_*` APIs before writing fresh hardware setup.
7. Implement one phase at a time and verify it before moving on.
8. If something fails, capture autopsy evidence before redesigning.
9. Update the roadmap or case docs in the same change when the task changes real project status.

## Non-negotiable rules

- Default to `A500 + Kickstart 1.3` unless a later machine is explicitly required.
- Do not mix `dos_hunk_exe` assumptions with `metal/direct` assumptions.
- Do not treat a composed Amiga scene as "basic" if it combines multiple subsystems.
- Do not close tests because the code "looks right"; close them only with build plus evidence.
- Do not rewrite multiple low-level subsystems at once after a failure; find the first broken invariant.
- Do not create new battery cases without placing them in the existing battery structure and documentation flow.
- Do not open a new low-level case before listing every required technical element and deciding whether it already exists in `engine/` or in a proven case.
- Do not rewrite base display setup if a validated case already provides the required display contract.
- Do not treat graphics as retained-mode UI; assume the machine has persistent state and each visible result must be maintained explicitly.
- Do not add HUD, debug text, scroll, overlays, BOBs, copper effects, or mixed playfield/sprite scenes without explaining ownership of pixels or registers first.
- Do not assume a visual element persists correctly unless its redraw, restore, or independent overlay policy has been defined.
- Do not assume all sprites are equivalent: hardware sprites, CPU sprites, and blitter BOBs have different ownership, lifetime, and update costs.
- Do not request copper effects casually; copper work must be described in terms of scanline timing and dynamic register changes during display fetch.
- Do not design engine APIs as if one implementation fits every rendering context; assume bitplanes, masks, stride, clipping, and ownership can change the correct implementation.
- Do not hide low-level costs behind a single convenience API if the underlying contexts have materially different behavior.

## API layering rule

When proposing or editing engine APIs, assume the engine often needs two layers:

1. Low-level explicit primitives
   These expose the real parameters that affect implementation strategy:
   bitplane count, mask mode, stride/modulo, clipping, buffer ownership, sprite class, source format, destination surface, and timing constraints.
2. Retained or scene-level helpers
   These simplify normal game code by managing scene objects, dirty regions, restore/redraw, ordering, and per-frame maintenance on top of the low-level layer.

Prefer this shape:

- low-level primitive families for predictable control and specialization
- retained wrappers for ease of use

Avoid this shape:

- one "do everything" function that hides too many context-dependent costs

## Parametric implementation rule

For CPU sprites, BOBs, blits, tile copies, and similar routines, explicitly ask:

1. Does bitplane count change the implementation?
2. Does mask mode change the implementation?
3. Does destination ownership or restore policy change the implementation?
4. Is there a generic path plus specialized fast paths?
5. Should this be one API with multiple specializations, or multiple explicit primitives?

If fixed parameters are known at compile time, prefer designs that allow specialization rather than forcing one generic runtime path.

## Specialization guidance

If the task touches performance-critical low-level code, consider a split between:

- generic fallback implementation
- specialized variants for common fixed cases

Examples:

- `1bpl`, `2bpl`, `4bpl` CPU blits
- masked vs unmasked BOB copies
- clipped vs unclipped hot paths
- constant-width or constant-stride sprite routines

The skill should favor interfaces that keep this specialization possible without duplicating the whole scene system.

## Mandatory mental model for every task

For every non-trivial request, explicitly reason about these before coding:

1. What state already exists on the machine and who owns it?
2. Which surface or resource each feature lives on:
   bitplanes, overlay plane, sprite DMA, blitter-managed buffer, copper list, or CPU-side shadow data.
3. Whether the feature is screen-anchored, world-anchored, or tied to another moving viewport.
4. What must happen once at init, once per frame, and during scanout.
5. What must be restored, redrawn, double-buffered, or left untouched.
6. Which resources are consumed asynchronously by DMA or copper and therefore cannot be treated like normal memory.

If any of these are ambiguous and the choice changes architecture, ask the user before implementing.

## Dependency inventory rule

Before coding, explicitly list:

1. every technical element the task requires,
2. whether each one is already provided by the engine,
3. whether each one is already proven by a battery case,
4. whether it must be implemented new in this task.

Recommended table:

- element needed
- provided by engine?
- provided by proven case?
- new work?
- chosen reference

If a required element is already proven, prefer inheriting its contract rather than rebuilding it.

This is especially important for:

- display base setup,
- interleaved bitplane layout,
- `BPL1MOD/BPL2MOD`,
- copper scaffolding,
- sprite DMA stream format,
- scroll/view setup,
- DOS loader paths.

If the task's new hypothesis is "CPU sprite masked", then display setup should usually come from an existing proven display case and not be redesigned.

## Frame-loop rule

Unless the task is a one-shot static vector or a truly fixed display, assume the solution needs at least one per-frame update loop.

That loop is responsible for some combination of:

- persistence and redraw policy
- scroll adaptation
- sprite/BOB position updates
- repopulating DMA-visible buffers
- HUD or overlay maintenance
- palette or copper control updates that are CPU-driven
- synchronization with VBL / `WaitTOF`

Never assume a HUD, debug overlay, or moving object will stay correct without a defined per-frame maintenance policy.

## Surface ownership and redraw policy

Before implementation, choose and state one of these models for every visible element:

- fixed screen overlay
- world-space element that moves with scroll
- save-under / restore-under on shared bitplanes
- redraw-every-frame region
- dedicated plane or buffer
- hardware sprite stream
- CPU sprite or blitter BOB
- copper-driven visual change

State who owns the pixels or registers, who restores prior content, and what happens when the viewport scrolls or the content changes.

## CPU, DMA, and copper responsibilities

Every task should separate responsibility across:

- CPU init
- CPU per-frame/VBL work
- copper work during scanout
- DMA-owned resources

If the task involves the copper, describe which registers change dynamically, at what scanline scope, and whether the values are static or rebuilt per frame.

## Dynamic copper rule

Treat dynamic copper work as a scene-state problem, not as a one-off visual trick.

Before implementing dynamic copper behavior, explicitly answer:

1. Which scene variables drive the copper changes?
2. Which registers are patched by CPU each frame?
3. Which registers are changed by the copper during scanout?
4. Is the list static, patched in place, or double-buffered?
5. What guarantees prevent patching the active list at the wrong time?
6. Which parts are scene-dependent and which are fixed display scaffolding?

If these answers are not explicit, do not implement the copper effect yet.

## Engine API design questions

When the task proposes new engine APIs, answer these before settling the interface:

1. Is this low-level primitive, retained helper, or both?
2. Which parameters materially change implementation strategy?
3. Should those parameters stay explicit in the API?
4. Is there a generic fallback plus specialized hot paths?
5. Can the design evolve toward compile-time specialization without breaking the API shape?

## Choose the right lane

### Fix or close an existing engine test

Read:

- `doc/engine-unified-test-roadmap.md`
- `doc/engine-test-battery-matrix.md`
- `doc/amiga-implementation-roadmap.md`
- the target case `README.md`, `docs/technique.md`, and `evidence/README.md`

Then:

1. Identify whether the case is blocked by build, runtime, validation, or documentation honesty.
2. Keep the current artifact path unless there is a documented reason to change it.
3. Explicitly check whether the failure is in the case logic or in surface ownership / frame maintenance assumptions.
4. Verify with the strongest available evidence:
   build, runtime-state, evidence-log, custom regs, screenshot, vision, postmortem, sequence if motion matters.
5. Mark the case `HECHO` or `PARCIAL` honestly in project docs if the task changes closure status.

### Create a new battery or engine functional test

Read:

- `doc/amiga-test-battery-spec.md`
- `doc/engine-test-battery-matrix.md`
- `doc/amiga-implementation-roadmap.md`
- `tests/amiga-battery/README.md`
- the nearest stable reference cases

Then:

1. Choose the nearest proven case and derive from it.
2. Build the dependency inventory before writing code.
3. Define the machine, artifact, and evidence path in the contract first.
4. Define surface ownership, viewport anchoring, and per-frame maintenance policy before drawing anything.
5. Keep the first phase visually and technically narrow.
6. Add runtime markers and evidence expectations early.
7. Ensure the test proves a reusable engine capability, not just a one-off demo trick.

### Investigate a new Amiga technique

Read:

- `doc/techniques/README.md`
- the relevant file under `doc/techniques/`
- `doc/reference/amiga-authoritative-sources.md`
- `doc/amiga-hardware-manual-index.md`
- existing related battery cases

Then:

1. Treat the task as research first, implementation second.
2. State what is known, what is inferred, and what still needs proof.
3. Explain the timing model: CPU, VBL, copper, DMA, and any scanline-local effects.
4. Open with the smallest demonstrable vector.
5. Land the technique in battery form before promoting it to `engine/`.

### Develop a future game feature on the engine

Read:

- `doc/engine-architecture.md`
- `doc/engine-new-project-guide.md`
- `doc/engine-roadmap.md`
- the battery cases that already prove the required techniques

Then:

1. Decompose the feature into proven and unproven hardware capabilities.
2. For each visible element, define whether it belongs to world space, screen space, sprite DMA, blitter output, or copper state.
3. If a required capability is not yet proven in battery, stop feature expansion and create the proof case first.
4. Build game code on top of reusable engine APIs whenever possible.

## Validation ladder

Use as many of these as the task supports, in this order:

1. Build success
2. Runtime markers: `runtime-state`, `evidence-log`
3. Custom register checks
4. Memory or pointer checks from `.map` / ELF symbols
5. Internal emulator screenshot
6. LM Studio vision artifacts
7. Postmortem for crashes or suspicious stops
8. Sequence evidence for movement, temporal effects, or command contracts

If the image and the markers disagree, trust the deeper instrumentation first and explain the conflict.

## Common invariant questions

When debugging, answer these before changing architecture:

1. Did the binary reach its intended entry or first stage marker?
2. Is the base display mode actually active?
3. Is the active copper list the one we think it is?
4. Are DMA pointers valid and in the correct memory region?
5. Are we reading a DMA-consumed pointer and mistaking that for corruption?
6. Is the failure in loader, runtime, display setup, animation update, or validation tooling?

When the output looks visually wrong but the markers are healthy, ask next:

7. Did we accidentally reopen a solved display-base problem instead of only testing the intended new hypothesis?
8. Are `BPL1MOD/BPL2MOD` and interleaved layout inherited correctly from the proven reference case?
9. Are we validating a new primitive on top of a display contract that was quietly reimplemented incorrectly?

## Required questions for HUD, overlays, scroll, and mixed scenes

If the task includes a HUD, debug text, overlay, scroll, or mixed sprite/playfield composition, answer these explicitly:

1. Is the element anchored to the screen or to the world?
2. If it lives on shared bitplanes, who restores what was there before?
3. If the viewport scrolls, does the element move, redraw, or stay on a separate surface?
4. Is the element updated every frame, only on change, or by a dedicated overlay mechanism?
5. Would a hardware sprite, CPU sprite, blitter BOB, dedicated plane, or copper band be the more appropriate implementation?

If the answer is not obvious, pause and ask the user.

## Documentation obligations

When the task materially changes project truth, update the matching docs in the same pass:

- `doc/amiga-implementation-roadmap.md`
- `doc/engine-test-battery-matrix.md`
- case `README.md` / `docs/technique.md` / `evidence/README.md`
- `doc/engine-docs-index.md` if a new core document was added

## Use the repo prompt and contract

For any significant low-level task in this repo, use these repo docs as first-class operating instructions:

- `doc/amiga-lowlevel-agent-prompt.md`
- `doc/amiga-lowlevel-technique-contract-template.md`
- `doc/amiga-display-setup-checklist.md`

They define how to ask, phase, and validate Amiga work in this project.
