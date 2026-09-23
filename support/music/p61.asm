;============================================================================
; p61.asm — "The Player" 6.1A (P6112), playroutine OFICIAL de Photon/Scoopex.
;
; Fuente vendorizada: `support/music/p61/P6112-Play.i` (P6112-Play_hr.i de
; Photon/Scoopex, V1.hr "EFx fix"; MIT/dominio publico segun su readme de
; Aminet `mus/misc/P6112`). Sustituye al wrapper casero anterior.
;
; Config: modo **VBlank** (no tenemos la CIA libre tras el takeover): `P61mode=2`
; → `p61cia=0`, `lev6=1`. `p61system=1` (exec valido), `p61exec=0` (no depender
; de ExecBase->VBlankFrequency), `channels=4`. Se conserva el vector por
; `P61_Music` cada frame (lo llama el engine en `update_music`).
;
; Referencia de uso: `hukkax/amiga bears/player.asm` y `Player61A.guide`.
;============================================================================

; --- Opciones (VBlank) ------------------------------------------------------
usecode		= -1		;versión completa (todas las features; Player61A.guide §8)
P61mode		= 2		;2=VBLANK
split4		= 0		;incompatible con F03/F02/F01
splitchans	= 1
visuctrs	= 0		;contadores de visualizador (los usa el engine via _P61_visuctr)
asmonereport	= 0
p61system	= 0
p61exec		= 0
p61fade		= 0
channels	= 4
playflag	= 0
p61bigjtab	= 0
opt020		= 0
p61jump		= 1
C		= 0
clraudxdat	= 0
optjmp		= 1
oscillo		= 0
use1Fx		= 0
quietstart	= 0

	ifeq P61mode-2
p61cia		= 0
lev6		= 1
noshorts	= 0
dupedec		= 0
suppF01		= 0
	endc

; --- Interfaz con el engine (nombres `_P61_*` que espera `music_player.hpp`) --
	XDEF	_P61_Init, _P61_Music, _P61_SetPosition, _P61_Osc, _P61_End
	XDEF	_P61_ControlBlock, _P61_visuctr, _P61_temp

	SECTION "P61Code",CODE

; El engine llama a `_P61_Init(module, samples, buffer)` con A0/A1/A2 y lee D0
; como retorno. La fuente oficial ensambla `P61_Init` como punto de entrada (no
; `_`); este envoltorio fija A6=$dff000 antes, como hace el original.
_P61_Init
	movem.l	d2-d7/a2-a6,-(sp)
	moveq.l	#0,d0			;autodetect timers (CIA desactivada en VBlank)
	bsr	P61_Init
	movem.l	(sp)+,d2-d7/a2-a6
	rts

_P61_Music
	movem.l	d2-d7/a2-a6,-(sp)
	lea	$dff000,a6
	bsr	P61_Music
	movem.l	(sp)+,d2-d7/a2-a6
	moveq.l	#0,d0
	rts

_P61_SetPosition
	movem.l	a3/a6,-(sp)
	lea	$dff000,a6
	bsr	P61_SetPosition
	movem.l	(sp)+,a3/a6
	rts

_P61_Osc
	rts				;no se usa en la demo

_P61_End
	movem.l	a2-a6,-(sp)
	lea	$dff000,a6
	bsr	P61_End
	movem.l	(sp)+,a2-a6
	rts

	INCLUDE	"p61/P6112-Play.i"

; --- Alias de compatibilidad con `music_player.hpp` --------------------------
; El `.hpp` referencia `_P61_ControlBlock` (struct en `P61_motuuli`) y `_P61_temp`.
; Se exportan apuntando a los símbolos reales del `.i` (sin tocar el header).
; `_P61_visuctr` solo existe si `visuctrs=1`; el header no lo usa, no se exporta.
	XDEF	_P61_ControlBlock, _P61_temp
_P61_ControlBlock	EQU	P61_motuuli
_P61_temp		EQU	P61_temp0
