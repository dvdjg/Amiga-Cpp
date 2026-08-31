import { readFileSync } from 'fs';
const { sideChannelCommand } = await import('file:///C:/Users/dvdjg/Documents/programa/AI/Amiga/mcp-winuae-emu/dist/side-channel.js');
const base = 0xc0cb88;
const map = readFileSync('out/demos/107_xlimited_corkscrew/107_xlimited_corkscrew.map', 'utf8');
function sym(name) {
  for (const line of map.split('\n')) {
    const m = line.match(new RegExp(`^\\s*0x([0-9a-fA-F]+)\\s+${name}\\s*$`));
    if (m) return parseInt(m[1], 16);
  }
  return null;
}
for (const name of ['g_eng_frame_telemetry', 'g_eng_run_status', 'g_dbg_copper', 'g_dbg_hud_base']) {
  const link = sym(name);
  const addr = base + (link - 0x400);
  const r = await sideChannelCommand(`mem 0x${addr.toString(16)} 20`, 2346, 4000);
  console.log(name, 'link 0x' + link.toString(16), 'runtime 0x' + addr.toString(16), '=', r.reply?.data || r.raw);
}
process.exit(0);
