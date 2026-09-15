/* flatshade_asm: rutinas calientes de flatshade-convex en ASM m68k (gas).
 *
 * Implementa las cuatro rutinas calientes del efecto (face/edge visibility, transform
 * de vertices y draw de aristas) y sustituye a sus equivalentes C++ cuando la demo se
 * compila con `-DK_FLATSHADE_ASM=1`. El default es la ruta C++ (`K_FLATSHADE_ASM=0`),
 * que es la que renderiza bien; la ruta asm aun no es valida: `fs_update_face_visibility`,
 * `fs_update_edge_visibility_convex` y `fs_transform_vertices` son correctas, pero
 * `fs_draw_edges` dibuja el contorno con un desfase de ~1-2 px y, con el area fill XOR
 * (paridad por scanline), el relleno se rompe en bandas/triangulos. `verify-116` da PASS
 * pese a ello: el gate valido es visual (wireframe con `-DFLATSHADE_SKIP_FILL=1` o
 * secuencia + Ollama). Ver la bitacora OPTIMIZACION_GPP_68000.md.
 *
 * Cuidado al tocar `.Lwait_blit`: espera BBUSY usando d0 como scratch, y
 * `fs_draw_edges` tiene en d0 el BLTCON0 que escribe justo despues del wait; por eso
 * el wait salva/restaura d0 (si lo pisara se programaria DMACONR como con0 y los
 * blits de linea no pintarian).
 *
 * Port fiel de `demoscene-repo-orig/effects/flatshade-convex/flatshade-convex.c`
 * sacado a rutinas .s aparte, como se hizo con `fire_loop.s` (demo 080): g++ ignora
 * los pins `register asm("aN")` del original y no consigue el codegen apretado que
 * marca el profiler (Transform 156 / Draw 130 lineas de raster). En gas se fijan los
 * registros a mano y se recupera esa velocidad. La version C++ canonica de la demo
 * (`update_face_visibility`/`update_edge_visibility_convex`/`transform_vertices`/
 * `draw_edges`) se conserva como fallback (`-DK_FLATSHADE_ASM=0`).
 *
 * Argumentos por MEMORIA en el global `extern "C" g_fs_args` (ver main.cpp):
 *   [0] = obj      (Object3D*; offsets: objdat=0, vertexGroups=4, edgeGroups=8,
 *                   faceGroups=12, objectToWorld=38, camera=86)
 *   [4] = planes   (base del buffer de bitplanes activo)
 *   [8] = invsqrt  (kInvSqrt[512], u16*)
 *   [12]= bbox     (s16[4]: bx0,bx1,by0,by1 ??? escribidos por fs_transform_vertices)
 *
 * Offsets de layout (mismos que el C original y el port object3d.hpp):
 *   Node3D: flags=0, point={2,4,6}, vertex={8,10,12}
 *   Edge  : flags=0, pad=1, point[2]={2,4}
 *   Face  : normal={0,2,4}, flags=6, material=7, count=8, indices=10
 *   Mat3x3 (objectToWorld@38): m00=38 m01=40 m02=42 x=44
 *                              m10=46 m11=48 m12=50 y=52
 *                              m20=54 m21=56 m22=58 z=60
 *   camera=86 (cx,cy,cz = 86,88,90)
 *
 * IMPORTANTE: nada de `.cfi_*` aqui (generan `.eh_frame` no vacio y el canal lateral
 * del runner desindiza las secciones; ver fire_loop.s). Sin CFI queda a 0 bytes.
 */

	.section .text.flatshade_asm,"ax",@progbits

/* Espera a que el Blitter libere (DMACONR bit 14 = BBUSY). a0 = base custom 0xdff000.
 * PRESERVA d0: `fs_draw_edges` lo usa como BLTCON0 y lo escribe justo despues del
 * wait, asi que si esta rutina lo pisara se programaria DMACONR como con0. */
.Lwait_blit:
	move.w	d0,-(sp)
.Lwait_blit_loop:
	move.w	0x002(a0),d0
	btst	#14,d0
	bne.s	.Lwait_blit_loop
	move.w	(sp)+,d0
	rts

/* ==========================================================================
 * fs_update_face_visibility
 *   Port de `UpdateFaceVisibility` (flatshade-convex.c): por cada cara calcula la
 *   luz 0..15 (dot normal??(camera-p0) normalizado con la tabla kInvSqrt, sin sqrt)
 *   y la escribe en face->flags. Caras con material<0 son de doble cara.
 * ========================================================================== */
	.globl	fs_update_face_visibility
	.type	fs_update_face_visibility, function
fs_update_face_visibility:
	movem.l	d2-d7/a2-a6,-(sp)
	lea	g_fs_args,a2			/* args */
	movea.l	0(a2),a3			/* Object3D* */
	movea.l	0(a3),a0			/* objdat */
	movea.l	12(a3),a1			/* faceGroups */
	movea.l	8(a2),a4			/* invsqrt */
	move.w	86(a3),d2			/* camera.x */
	move.w	88(a3),d3			/* camera.y */
	move.w	90(a3),d4			/* camera.z */
.Lfv_group:
	move.w	(a1)+,d5			/* f = *group++ */
	beq.w	.Lfv_done
	lea	0(a0,d5.w),a5			/* face = objdat + f */
	/* Recarga la camara CADA cara: d4 se usa como temporal en el calculo de
	 * v/mag2 y clobberaria camera.z (a partir de la 2a cara, pz saldria mal). */
	move.w	86(a3),d2			/* cx */
	move.w	88(a3),d3			/* cy */
	move.w	90(a3),d4			/* cz */
	/* primer indice de punto: face->indices[0].vertex (s16 @10) */
	move.w	10(a5),d6			/* i = indice de punto */
	lea	0(a0,d6.w),a6			/* p = point3d(objdat, i) */
	move.w	d2,d0
	sub.w	0(a6),d0			/* px = cx - p->x */
	move.w	d3,d1
	sub.w	2(a6),d1			/* py = cy - p->y */
	move.w	d4,d7
	sub.w	4(a6),d7			/* pz = cz - p->z */
	/* v = fn0*px + fn1*py + fn2*pz (muls.w 16x16->32) */
	move.w	0(a5),d6
	ext.l	d6
	move.w	d0,d5
	ext.l	d5
	muls.w	d5,d6				/* fn0*px */
	move.w	2(a5),d5
	ext.l	d5
	move.w	d1,d4
	ext.l	d4
	muls.w	d4,d5				/* fn1*py */
	add.l	d5,d6
	move.w	4(a5),d5
	ext.l	d5
	move.w	d7,d4
	ext.l	d4
	muls.w	d4,d5				/* fn2*pz */
	add.l	d5,d6				/* v (s32) */
	tst.l	d6
	bmi.w	.Lfv_back
	/* v >= 0: s = hi16(px^2+py^2+pz^2), clamp 511 */
	move.w	d0,d5
	muls.w	d0,d5
	move.w	d1,d4
	muls.w	d1,d4
	add.l	d4,d5
	move.w	d7,d4
	muls.w	d7,d4
	add.l	d4,d5				/* mag2 */
	swap	d5
	move.w	d5,d4				/* s = hi16 */
	cmpi.w	#511,d4
	bls.w	.Lfv_s_ok
	move.w	#511,d4
.Lfv_s_ok:
	/* vv = hi16(v); res = mulu16((u16)vv, invsqrt[s]) >> 16 */
	swap	d6
	move.w	d6,d5				/* vv (word bajo = hi16 de v) */
	add.w	d4,d4				/* indice*2 */
	move.w	0(a4,d4.w),d4			/* invsqrt[s] */
	mulu.w	d5,d4				/* mulu16 */
	swap	d4
	move.b	d4,6(a5)			/* face->flags = res */
	bra.w	.Lfv_group
.Lfv_back:
	/* v < 0: si material < 0 (doble cara), luz con -v; si no flags=-1 */
	move.b	7(a5),d5
	ext.w	d5
	tst.w	d5
	bpl.w	.Lfv_inv
	move.w	d0,d5
	muls.w	d0,d5
	move.w	d1,d4
	muls.w	d1,d4
	add.l	d4,d5
	move.w	d7,d4
	muls.w	d7,d4
	add.l	d4,d5
	swap	d5
	move.w	d5,d4
	cmpi.w	#511,d4
	bls.w	.Lfv_s_ok2
	move.w	#511,d4
.Lfv_s_ok2:
	neg.l	d6				/* -v */
	swap	d6
	move.w	d6,d5				/* vv = hi16(-v) */
	add.w	d4,d4
	move.w	0(a4,d4.w),d4			/* invsqrt[s] */
	mulu.w	d5,d4
	swap	d4
	move.b	d4,6(a5)			/* face->flags = res */
	bra.w	.Lfv_group
.Lfv_inv:
	move.b	#-1,6(a5)			/* face->flags = -1 (no visible) */
	bra.w	.Lfv_group
.Lfv_done:
	movem.l	(sp)+,d2-d7/a2-a6
	rts

/* ==========================================================================
 * fs_update_edge_visibility_convex
 *   Port de `UpdateEdgeVisibilityConvex`: por cada cara visible marca sus NODES
 *   (flags=1) y XOR-ea el color de luz en sus EDGES. El XOR cancela las aristas
 *   compartidas por dos caras visibles de igual luz.
 * ========================================================================== */
	.globl	fs_update_edge_visibility_convex
	.type	fs_update_edge_visibility_convex, function
fs_update_edge_visibility_convex:
	movem.l	d2-d7/a2-a6,-(sp)
	lea	g_fs_args,a2			/* args */
	movea.l	0(a2),a3			/* Object3D* */
	movea.l	0(a3),a0			/* objdat */
	movea.l	12(a3),a1			/* faceGroups */
.Lev_group:
	move.w	(a1)+,d5			/* f */
	beq.w	.Lev_done
	lea	0(a0,d5.w),a5			/* face */
	move.b	6(a5),d2			/* flags = face->flags */
	tst.b	d2
	bmi.w	.Lev_group			/* flags < 0 -> no visible */
	move.w	8(a5),d3			/* vertices = count */
	subq.w	#3,d3
	movea.l	a5,a6
	adda.w	#10,a6				/* index = face + 10 (s16*) */
	/* dos aristas/nodes fijos (como el original) */
	move.w	(a6)+,d4			/* i = *index++ */
	lea	0(a0,d4.w),a4
	suba.w	#2,a4				/* node3d = objdat + i - 2 */
	move.b	#1,(a4)				/* node->flags = 1 */
	move.w	(a6)+,d4			/* i = *index++ (edge index) */
	lea	0(a0,d4.w),a4			/* edge3d = objdat + i */
	move.b	(a4),d7
	eor.b	d2,d7
	move.b	d7,(a4)				/* edge->flags ^= flags */
	move.w	(a6)+,d4
	lea	0(a0,d4.w),a4
	suba.w	#2,a4
	move.b	#1,(a4)
	move.w	(a6)+,d4
	lea	0(a0,d4.w),a4
	move.b	(a4),d7
	eor.b	d2,d7
	move.b	d7,(a4)
.Lev_loop:
	/* do { i=*index++; node->flags=1; i=*index++; edge->flags^=flags; } while(--vertices != -1) */
	move.w	(a6)+,d4
	lea	0(a0,d4.w),a4
	suba.w	#2,a4
	move.b	#1,(a4)
	move.w	(a6)+,d4
	lea	0(a0,d4.w),a4
	move.b	(a4),d7
	eor.b	d2,d7
	move.b	d7,(a4)
	dbf	d3,.Lev_loop
	bra.w	.Lev_group
.Lev_done:
	movem.l	(sp)+,d2-d7/a2-a6
	rts

/* ==========================================================================
 * fs_transform_vertices
 *   Port de `TransformVertices` (con macros MULVERTEX1/2): transforma y proyecta
 *   (div16) los vertices con flags!=0, guarda (x,y,zp) en Node3D::vertex y
 *   actualiza la bounding-box (g_fs_args.bbox).
 * ========================================================================== */
	.globl	fs_transform_vertices
	.type	fs_transform_vertices, function
fs_transform_vertices:
	movem.l	d2-d7/a2-a6,-(sp)
	lea	-20(sp),sp			/* slots: zp/xp/yp/m0/m1 (20 bytes) */
	lea	g_fs_args,a2			/* args */
	movea.l	0(a2),a3			/* Object3D* */
	movea.l	0(a3),a0			/* objdat */
	movea.l	4(a3),a1			/* vertexGroups */
	movea.l	12(a2),a4			/* bbox */
	/* m0 = (M.x - normfx(m00*m01)) << 8 */
	move.w	44(a3),d0
	ext.l	d0
	move.w	38(a3),d1
	ext.l	d1
	move.w	40(a3),d2
	ext.l	d2
	muls.w	d2,d1				/* m00*m01 */
	moveq	#12,d2
	asr.l	d2,d1				/* normfx */
	sub.l	d1,d0
	lsl.l	#8,d0
	move.l	d0,12(sp)			/* m0 */
	/* m1 = (M.y - normfx(m10*m11)) << 8 */
	move.w	52(a3),d0
	ext.l	d0
	move.w	46(a3),d1
	ext.l	d1
	move.w	48(a3),d2
	ext.l	d2
	muls.w	d2,d1
	moveq	#12,d2
	asr.l	d2,d1
	sub.l	d1,d0
	lsl.l	#8,d0
	move.l	d0,16(sp)			/* m1 */
	/* M.z = M.z - normfx(m20*m21) (se escribe de vuelta al objeto) */
	move.w	60(a3),d0
	ext.l	d0
	move.w	54(a3),d1
	ext.l	d1
	move.w	56(a3),d2
	ext.l	d2
	muls.w	d2,d1
	moveq	#12,d2
	asr.l	d2,d1
	sub.l	d1,d0
	move.w	d0,60(a3)			/* M.z (s16) */
	/* bbox reset */
	move.w	#32767,0(a4)
	move.w	#-32768,2(a4)
	move.w	#32767,4(a4)
	move.w	#-32768,6(a4)
.Ltv_group:
	move.w	(a1)+,d1			/* i = *group++ */
	beq.w	.Ltv_group_end
	move.w	d1,d2
	subq.w	#2,d2
	lea	0(a0,d2.w),a5			/* node = objdat + i - 2 */
	tst.b	(a5)
	beq.w	.Ltv_group			/* node->flags == 0 */
	clr.w	(a5)				/* *pt++ = 0 (flags+pad) */
	move.w	2(a5),d3			/* x = point.x */
	move.w	4(a5),d4			/* y = point.y */
	move.w	6(a5),d5			/* z = point.z */
	move.w	d3,d6
	muls.w	d4,d6				/* xy = x*y */
	/* MULVERTEX1(xp, m0): t0=m00+y; t1=m01+x; t2=m02*z; xp=((t0*t1+t2-xy)>>4)+m0 */
	move.w	38(a3),d0
	add.w	d4,d0
	ext.l	d0
	move.w	40(a3),d7
	add.w	d3,d7
	ext.l	d7
	muls.w	d7,d0
	move.w	42(a3),d7
	ext.l	d7
	muls.w	d5,d7
	add.l	d7,d0
	sub.l	d6,d0
	asr.l	#4,d0
	add.l	12(sp),d0			/* + m0 */
	move.l	d0,4(sp)			/* xp */
	/* MULVERTEX1(yp, m1): t0=m10+y; t1=m11+x; t2=m12*z */
	move.w	46(a3),d0
	add.w	d4,d0
	ext.l	d0
	move.w	48(a3),d7
	add.w	d3,d7
	ext.l	d7
	muls.w	d7,d0
	move.w	50(a3),d7
	ext.l	d7
	muls.w	d5,d7
	add.l	d7,d0
	sub.l	d6,d0
	asr.l	#4,d0
	add.l	16(sp),d0			/* + m1 */
	move.l	d0,8(sp)			/* yp */
	/* MULVERTEX2(zp): t0=m20+y; t1=m21+x; t2=m22*z; t3=z; zp=normfx(t0*t1+t2-xy)+t3 */
	move.w	54(a3),d0
	add.w	d4,d0
	ext.l	d0
	move.w	56(a3),d7
	add.w	d3,d7
	ext.l	d7
	muls.w	d7,d0
	move.w	58(a3),d7
	ext.l	d7
	muls.w	d5,d7
	add.l	d7,d0
	sub.l	d6,d0
	moveq	#12,d2
	asr.l	d2,d0				/* normfx */
	move.w	60(a3),d7
	ext.l	d7
	add.l	d7,d0				/* + z */
	move.l	d0,0(sp)			/* zp */
	/* sx = div16(xp, (s16)zp) + 128 */
	move.w	2(sp),d1			/* zp como s16 (word bajo, big-endian) */
	move.l	4(sp),d0			/* xp */
	divs.w	d1,d0
	addi.w	#128,d0
	move.w	d0,8(a5)			/* vertex.x */
	/* sy = div16(yp, (s16)zp) + 128 */
	move.l	8(sp),d0			/* yp */
	divs.w	d1,d0
	addi.w	#128,d0
	move.w	d0,10(a5)			/* vertex.y */
	move.w	d1,12(a5)			/* vertex.z = (s16)zp */
	/* bbox */
	move.w	8(a5),d0			/* sx */
	cmp.w	0(a4),d0
	bge.w	.Ltv_bb0
	move.w	d0,0(a4)			/* g_bx0 = sx */
.Ltv_bb0:
	cmp.w	2(a4),d0
	ble.w	.Ltv_bb1
	move.w	d0,2(a4)			/* g_bx1 */
.Ltv_bb1:
	move.w	10(a5),d1			/* sy */
	cmp.w	4(a4),d1
	bge.w	.Ltv_bb2
	move.w	d1,4(a4)			/* g_by0 */
.Ltv_bb2:
	cmp.w	6(a4),d1
	ble.w	.Ltv_bb3
	move.w	d1,6(a4)			/* g_by1 */
.Ltv_bb3:
	bra.w	.Ltv_group
.Ltv_group_end:
	tst.w	(a1)				/* while (*group) del do/while exterior */
	bne.w	.Ltv_group
.Ltv_done:
	lea	20(sp),sp			/* liberar slots */
	movem.l	(sp)+,d2-d7/a2-a6
	rts

/* ==========================================================================
 * fs_draw_edges
 *   Port de `DrawObject` (flatshade-convex.c): dibuja las aristas visibles
 *   (edgeColor > 0) con line mode ONEDOT+EOR replicadas en cada plano con bit.
 *   Registros comunes 1x al inicio (BLTAFWM/ALWM, BLTADAT, BLTBDAT, BLTCMOD,
 *   BLTDMOD); por arista calcula Bresenham una vez y por plano programa solo
 *   BLTCON0/1, BLTCPT, BLTAPT, BLTDPT=planes, BLTBMOD, BLTAMOD, BLTSIZE
 *   (macro DRAWLINE), avanzando BLTCPT en plane_bytes entre planos.
 *   Descarta aristas horizontales (y0==y1), como el original.
 *
 * Registros (tras preparar la arista):
 *   a0=custom a4=objdat a3=edgeGroups a5=planes a6=bltcpt
 *   d0=con0 d1=con1 d2=edgeColor d4=bmod d6=amod d7=size d3=derr(apt)
 * ========================================================================== */
	.globl	fs_draw_edges
	.type	fs_draw_edges, function
fs_draw_edges:
	movem.l	d2-d7/a2-a6,-(sp)
	clr.l	-(sp)				/* (sp)  = nEdges (-> g_eng_prof.v[10]) */
	clr.l	-(sp)				/* 4(sp) = nLines (-> v[11]) */
	movea.l	#0xdff000,a0			/* custom base */
	/* Activa el DMA del Blitter, como `blitter_lines_eor_begin` (igualmente el
	 * init ya lo deja activo con el pre-clear; se reafirma aqui). */
	move.w	#0x8240,0x96(a0)		/* dmacon = setclr|master|blitter */
	bsr.w	.Lwait_blit
	move.w	#-1,0x44(a0)			/* bltafwm */
	move.w	#-1,0x46(a0)			/* bltalwm */
	move.w	#0x8000,0x74(a0)		/* bltadat */
	move.w	#0xffff,0x72(a0)		/* bltbdat (patron linea) */
	move.w	#32,0x60(a0)			/* bltcmod = WIDTH/8 */
	move.w	#32,0x66(a0)			/* bltdmod = WIDTH/8 */
	lea	g_fs_args,a1			/* args */
	movea.l	0(a1),a2			/* Object3D* */
	movea.l	0(a2),a4			/* objdat */
	movea.l	8(a2),a3			/* edgeGroups */
	movea.l	4(a1),a5			/* planes (base buffer) */
.Lde_group:
	move.w	(a3)+,d0			/* e = *group++ */
	beq.w	.Lde_done
	lea	0(a4,d0.w),a6			/* edge = objdat + e */
	move.b	(a6),d2				/* edgeColor */
	tst.b	d2
	ble.w	.Lde_group			/* edgeColor <= 0 */
	addq.l	#1,(sp)				/* ++nEdges (d5/otros se reutilizan abajo) */
	move.w	#0,(a6)				/* *edge++ = 0 (flags+pad) */
	/* x0,y0 del primer vertice */
	move.w	2(a6),d0			/* i = point[0] */
	lea	0(a4,d0.w),a1
	adda.w	#6,a1				/* VERTEX(i) = objdat + i + 6 */
	move.w	0(a1),d3			/* x0 */
	move.w	2(a1),d4			/* y0 */
	/* x1,y1 del segundo vertice */
	move.w	4(a6),d0			/* i = point[1] */
	lea	0(a4,d0.w),a1
	adda.w	#6,a1
	move.w	0(a1),d5			/* x1 */
	move.w	2(a1),d6			/* y1 */
	cmp.w	d4,d6				/* y0 == y1 ? */
	beq.w	.Lde_group
	bgt.w	.Lde_ok
	exg	d3,d5				/* swap x0,x1 */
	exg	d4,d6				/* swap y0,y1 */
.Lde_ok:
	/* dmax = |x1-x0| (d7); dmin = y1-y0 (d0) */
	move.w	d5,d7
	sub.w	d3,d7				/* dmax = x1-x0 */
	bpl.w	.Lde_abs
	neg.w	d7
.Lde_abs:
	move.w	d6,d0
	sub.w	d4,d0				/* dmin = y1-y0 */
	/* octante: decide bltcon1 y reordena dmax/dmin (d7=dmax, d0=dmin) */
	cmp.w	d0,d7				/* dmax >= dmin ? */
	blo.w	.Lde_oct_b
	/* dmax >= dmin: x0>=x1 -> AUL|SUD|LM|OD (0x17); x0<x1 -> SUD|LM|OD (0x13) */
	move.w	#0x0013,d1
	cmp.w	d3,d5
	blt.w	.Lde_oct_ok
	or.w	#0x0004,d1			/* | AUL */
	bra.w	.Lde_oct_ok
.Lde_oct_b:
	/* dmax < dmin: x0>=x1 -> SUL|LM|OD; x0<x1 -> LM|OD ; swap dmax/dmin */
	move.w	#0x0003,d1
	cmp.w	d3,d5
	blt.w	.Lde_oct_swap
	or.w	#0x0008,d1			/* | SUL */
.Lde_oct_swap:
	exg	d7,d0				/* d7<->d0: d7=dmin, d0=dmax */
.Lde_oct_ok:
	/* aqui: d7=dmax, d0=dmin, d1=con1 base. Guardar dmax/dmin en d5/d6. */
	move.w	d7,d5				/* d5 = dmax */
	move.w	d0,d6				/* d6 = dmin */
	/* bltcpt = planes + ((y0<<5)+(x0>>3)) & ~1  (y0=d4, x0=d3) */
	move.w	d4,d0
	lsl.w	#5,d0				/* y0<<5 */
	move.w	d3,d4				/* x0 */
	lsr.w	#3,d4				/* x0>>3 */
	and.w	#0xfffe,d4			/* & ~1 */
	add.w	d0,d4				/* offset */
	movea.l	a5,a6
	adda.w	d4,a6				/* bltcpt */
	/* ror = rorw(x0&15,4); con0 = ror|0x0b4a; con1 |= ror */
	move.w	d3,d0				/* x0 */
	and.w	#15,d0
	ror.w	#4,d0				/* ror */
	move.w	d0,d3				/* guardar ror (x0 ya no se usa) */
	or.w	#0x0b4a,d0			/* con0 */
	or.w	d3,d1				/* con1 |= ror */
	/* bmod = dmin<<1 ; derr = bmod-dmax ; amod = derr-dmax ; size=(dmax<<6)+66 */
	add.w	d6,d6				/* dmin<<1 */
	move.w	d6,d4				/* bmod */
	move.w	d6,d3				/* bmod (para derr) */
	sub.w	d5,d3				/* derr = bmod - dmax  (d3=derr=apt) */
	/* Sin blt_signflag: ni `blitter_line_eor_prepare` (la ruta C++ que renderiza) ni
	 * el original lo ponen (solo `blitter_line`, que es otra variante). Ponerlo
	 * desviaba bltcon1 y el contorno salia desfasado. */
	/* bltapt (BLTAPT) es el acumulador de error de 32 bits del modo linea: hay que
	 * escribir derr EXTENDIDO CON SIGNO (como `blitter_line_eor_draw`, que hace
	 * `(void*)(s32)derr`). `move.l d3` a secas dejaba la palabra alta con basura. */
	ext.l	d3
	move.w	d3,d6				/* derr */
	sub.w	d5,d6				/* amod = derr - dmax */
	move.w	d5,d7				/* dmax */
	lsl.w	#6,d7
	addi.w	#66,d7				/* size */
	/* loop de planos: d0=con0 d1=con1 d2=edgeColor d4=bmod d6=amod d7=size d3=derr a6=bltcpt
	 * (d5 = nEdges, no se toca aqui; (sp) = nLines) */
	btst	#0,d2
	beq.w	.Lde_p1
	addq.l	#1,4(sp)			/* ++nLines */
	bsr.w	.Lwait_blit
	move.w	d0,0x40(a0)			/* bltcon0 */
	move.w	d1,0x42(a0)			/* bltcon1 */
	move.l	a6,0x48(a0)			/* bltcpt */
	move.l	d3,0x50(a0)			/* bltapt = derr */
	move.l	a5,0x54(a0)			/* bltdpt = planes (base) */
	move.w	d4,0x62(a0)			/* bltbmod */
	move.w	d6,0x64(a0)			/* bltamod */
	move.w	d7,0x58(a0)			/* bltsize */
.Lde_p1:
	adda.l	#8192,a6			/* bltcpt += plane_bytes */
	btst	#1,d2
	beq.w	.Lde_p2
	addq.l	#1,4(sp)			/* ++nLines */
	bsr.w	.Lwait_blit
	move.w	d0,0x40(a0)
	move.w	d1,0x42(a0)
	move.l	a6,0x48(a0)
	move.l	d3,0x50(a0)
	move.l	a5,0x54(a0)
	move.w	d4,0x62(a0)
	move.w	d6,0x64(a0)
	move.w	d7,0x58(a0)
.Lde_p2:
	adda.l	#8192,a6
	btst	#2,d2
	beq.w	.Lde_p3
	addq.l	#1,4(sp)			/* ++nLines */
	bsr.w	.Lwait_blit
	move.w	d0,0x40(a0)
	move.w	d1,0x42(a0)
	move.l	a6,0x48(a0)
	move.l	d3,0x50(a0)
	move.l	a5,0x54(a0)
	move.w	d4,0x62(a0)
	move.w	d6,0x64(a0)
	move.w	d7,0x58(a0)
.Lde_p3:
	adda.l	#8192,a6
	btst	#3,d2
	beq.w	.Lde_next
	addq.l	#1,4(sp)			/* ++nLines */
	bsr.w	.Lwait_blit
	move.w	d0,0x40(a0)
	move.w	d1,0x42(a0)
	move.l	a6,0x48(a0)
	move.l	d3,0x50(a0)
	move.l	a5,0x54(a0)
	move.w	d4,0x62(a0)
	move.w	d6,0x64(a0)
	move.w	d7,0x58(a0)
.Lde_next:
	bra.w	.Lde_group
.Lde_done:
	/* Telemetria: v[10]=nEdges, v[11]=nLines (EngProf: magic=0, v[16]@4). Asi el
	 * canal lateral no reporta 0 al perfilar la ruta asm. d0 es libre aqui. */
	lea	g_eng_prof,a2
	move.l	(sp),d0				/* nEdges */
	move.l	d0,44(a2)			/* v[10] = nEdges */
	move.l	4(sp),d0			/* nLines */
	move.l	d0,48(a2)			/* v[11] = nLines */
	lea	8(sp),sp			/* liberar los 2 slots de contadores */
	movem.l	(sp)+,d2-d7/a2-a6
	rts
