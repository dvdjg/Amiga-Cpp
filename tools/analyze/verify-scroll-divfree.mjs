#!/usr/bin/env node
// ---------------------------------------------------------------------------
// Host-check de la regresión 107: con la geometría NTTP (`fast_div`) el
// ScrollEngine NO debe dividir por los tiles (potencias de dos → shifts).
// Invariantes exactos en las 4 funciones scroll_*:
//   - 0 __udivsi3      (división de 32 bits sin signo: el /16 del tile era la
//                       principal; con la constante se convierte en `lsr.l #4`).
//   - 0 __modsi3/__divsi3  (módulo/división CON SIGNO: no debe haber).
//   - __umodsi3 permitido  (módulo SIN signo por display_height/planelines
//                           constantes 224/896: -Os/-O1 los deja como llamada
//                           con divisor constante; es correcto y no es el tile).
//   - scroll_right DEBE tener un shift (`lsr`) => el /16 es desplazamiento.
// Si alguien devuelve los denominadores a runtime, aparecen __udivsi3/__modsi3
// y la comprobación falla.
//
// Uso: node tools/analyze/verify-scroll-divfree.mjs <elf>
// Env: AMIGA_OBJDUMP (ruta al objdump m68k); si no se resuelve, exit 2 (skip).
// ---------------------------------------------------------------------------
import { execFileSync } from 'node:child_process';

const elf = process.argv[2];
if (!elf) { console.error('uso: verify-scroll-divfree.mjs <elf>'); process.exit(2); }
const objdump = process.env.AMIGA_OBJDUMP
  || 'C:/Users/dvdjg/.vscode/extensions/bartmanabyss.amiga-debug-1.8.1/bin/win32/opt/bin/m68k-amiga-elf-objdump.exe';
let raw;
try { raw = execFileSync(objdump, ['-d', '--no-show-raw-ins', elf], { encoding: 'utf8' }); }
catch (e) { console.error('objdump falló: ' + e.message); process.exit(2); }

const lines = raw.split('\n');
const funcs = new Map(); // name -> { udiv, smod, has_lsr }
let cur = null;
for (let i = 0; i < lines.length; ++i) {
  const open = lines[i].match(/^([0-9a-f]+) <(_ZN3eng5field12ScrollEngine[^>]*scroll_(right|left|down|up))[^>]*>:/);
  if (open) { cur = open[2]; funcs.set(cur, { udiv: 0, smod: 0, has_lsr: false }); continue; }
  if (cur) {
    // Nueva función (etiqueta sin "+offset"): cerrar el cuerpo anterior.
    if (/^[0-9a-f]+ <([^+>]+)>:/.test(lines[i]) && !/sobre_overlap/.test(lines[i])) {
      const newName = lines[i].match(/^[0-9a-f]+ <([^+>]+)>:/)[1];
      if (newName !== cur) { cur = null; continue; }
    }
    if (/__(udivsi3)/.test(lines[i])) funcs.get(cur).udiv++;
    if (/__(modsi3|divsi3)/.test(lines[i])) funcs.get(cur).smod++;
    if (/\blsr\./.test(lines[i])) funcs.get(cur).has_lsr = true;
  }
}
if (funcs.size === 0) { console.error('No se encontraron ScrollEngine::scroll_* en ' + elf); process.exit(1); }

let ok = true;
for (const [name, f] of funcs) {
  const which = /right/.test(name) ? 'right' : /left/.test(name) ? 'left' : /down/.test(name) ? 'down' : 'up';
  console.log(`  scroll_${which}: __udivsi3=${f.udiv} __mod/divsi3=${f.smod} has_lsr=${f.has_lsr}`);
  if (f.udiv !== 0 || f.smod !== 0) {
    console.error(`FALLO: scroll_${which} usa división/módulo por denominador no constante (¿se perdió fast_div/NTTP?).`);
    ok = false;
  }
}
const right = [...funcs.entries()].find(([n]) => /scroll_right/.test(n))?.[1];
if (right && !right.has_lsr) {
  console.error('FALLO: scroll_right sin shift de tile (¿el /16 dejó de ser desplazamiento?).');
  ok = false;
}
console.log(ok ? 'OK scroll div-free: tiles por shift, sin división/​módulo con denominador runtime.' : 'ERROR: scroll con división no constante.');
process.exit(ok ? 0 : 1);