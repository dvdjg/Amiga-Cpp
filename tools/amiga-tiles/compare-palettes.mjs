// ---------------------------------------------------------------------------
// compare-palettes.mjs — comparación PÍXEL A PÍXEL de algoritmos de paleta.
// Imagina que cada reconstruct_<algo>.png se compara contra una referencia
// (original truecolor reescalado, "--emit-source --dither none") y se miden:
//   PSNR      calidad media del cuantizador
//   p50..max  error cromático por percentiles (sqrt de dist² RGB)
//   jump>60   píxeles con error >60 (los "saltos" visibles: planos desiguales,
//             p. ej. el torso de los "culturistas")
//   nearPairs pares de colores de la paleta a dist <=12 ("casi idénticos")
//
// Uso (desde la raíz del repo, para que pngjs resuelva):
//   node tools/amiga-tiles/compare-palettes.mjs out/tile-demos/08_foto_real_ehb_32c
//
// Espera la estructura:
//   <base>/<imagen>/{64,32}c_floyd/<algoritmo>/{reconstruct_*.png,palette_*.json}
// y la referencia en %TEMP%\at_ref\<imagen>\ref.png.
// ---------------------------------------------------------------------------
import fs from 'node:fs';
import path from 'node:path';
import { PNG } from 'pngjs';

const base = process.argv[2];
if (!base) { console.error('uso: node compare-palettes.mjs <baseDir>'); process.exit(1); }
const imgs = fs.readdirSync(base).filter((d) => fs.statSync(path.join(base, d)).isDirectory());

function readRGB(p){ const d = PNG.sync.read(fs.readFileSync(p)); const w=d.width,h=d.height; const out=new Uint8Array(w*h*3); for(let i=0;i<w*h;i++){ out[i*3]=d.data[i*4];out[i*3+1]=d.data[i*4+1];out[i*3+2]=d.data[i*4+2]; } return {w,h,px:out}; }
function errDist(a,b,o){ const dr=a[o]-b[o],dg=a[o+1]-b[o+1],db=a[o+2]-b[o+2]; return dr*dr+dg*dg+db*db; }

for (const img of imgs) {
  const refP = path.join(process.env.TEMP,'at_ref',img,'ref.png');
  if(!fs.existsSync(refP)) continue;
  const ref = readRGB(refP);
  const n = ref.w*ref.h;
  const rows=[];
  for (const sub of ['64c_floyd','32c_floyd']) {
    const dir=path.join(base,img,sub);
    if(!fs.existsSync(dir)) continue;
    const palDirs = fs.readdirSync(dir).filter(x=>fs.statSync(path.join(dir,x)).isDirectory());
    for (const alg of palDirs) {
      const rec = path.join(dir,alg,fs.readdirSync(path.join(dir,alg)).find(f=>f.startsWith('reconstruct_')&&f.endsWith('.png')));
      if(!rec) continue;
      const r = readRGB(rec);
      const errs = new Float64Array(n);
      let tot=0;
      for(let i=0;i<n;i++){ errs[i]=errDist(ref.px,r.px,i*3); tot+=errs[i]; }
      const mse=tot/n; const psnr=10*Math.log10(255*255*3/(mse+1e-9));
      const s=Array.from(errs).sort((a,b)=>a-b);
      const pct=(q)=>{const m=Math.min(n-1,Math.round(q*n));return Math.round(Math.sqrt(s[m]));};
      const max=Math.round(Math.sqrt(s[n-1]));
      const jump=errs.filter(e=>e>60*60).length;
      let pal=[];
      const palF=fs.readdirSync(path.join(dir,alg)).find(f=>f.startsWith('palette_')&&f.endsWith('.json'));
      if(palF) { try { pal=JSON.parse(fs.readFileSync(path.join(dir,alg,palF),'utf8')).palette||[]; } catch {} }
      let nearPairs=0;
      for(let a=0;a<pal.length;a++)for(let b=a+1;b<pal.length;b++){const dx=pal[a][0]-pal[b][0],dy=pal[a][1]-pal[b][1],dz=pal[a][2]-pal[b][2]; if(dx*dx+dy*dy+dz*dz<=12*12) nearPairs++;}
      rows.push([img,sub,alg,psnr,pct(0.5),pct(0.95),pct(0.99),max,jump,nearPairs]);
    }
  }
  console.log(`\n### ${img} (${ref.w}x${ref.h})`);
  console.log('img        depth  alg         PSNR   p50  p95  p99  max  jump>60  nearPairs');
  for(const r of rows.sort((a,b)=>a[1].localeCompare(b[1])||b[3]-a[3])){
    console.log(`${r[0].padEnd(9)} ${r[1].padEnd(8)} ${r[2].padEnd(10)} ${r[3].toFixed(1).padStart(6)}  ${String(r[4]).padStart(3)} ${String(r[5]).padStart(3)} ${String(r[6]).padStart(3)} ${String(r[7]).padStart(3)}  ${String(r[8]).padStart(6)}  ${String(r[9])}`);
  }
  console.log('  (jump>60 = píxeles con error>60, los "saltos" visibles; nearPairs = pares de colores casi-idénticos)');
}
