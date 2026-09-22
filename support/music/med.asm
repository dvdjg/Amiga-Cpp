;============================================================================
; med.asm — envoltura del playroutine MED/OctaMED (KONEY) para el engine.
;
; Ensambla el playroutine (MOT) de KONEY/OctaMED-R y el módulo (INCBIN) en una
; sección ChipData, y exporta `_startmusic`/`_endmusic`, llamables desde la capa
; C++ `eng::audio::OctaMedPlayer` con `jsr _startmusic` / `jsr _endmusic`.
; `_chipzero` es la palabra de silencio que el playroutine usa como "sample" cero.
;
; Ver docs/engine/architecture/MUSIC_PLAYER.md y
; docs/guides/roadmap/ROADMAP_AUDIO.md (A1).
;============================================================================
	INCLUDE "octamed/med_feature_control.i"
	INCLUDE "octamed/MED_PlayRoutine.i"

	SECTION "ChipData",DATA_C
MED_MODULE:
	INCBIN "../../assets/amiga/audio/octamed_test.med"

	XDEF	_chipzero
_chipzero:
	DC.L	0

	END
