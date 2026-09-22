;============================================================================
; med.asm — envoltura del playroutine MED/OctaMED (KONEY) para el engine.
;
; Ensambla el playroutine (MOT) de KONEY/OctaMED-R y el módulo (INCBIN) en una
; sección ChipData, y exporta `_startmusic`/`_endmusic`, llamables desde la capa
; C++ `eng::audio::OctaMedPlayer` con `jsr _startmusic` / `jsr _endmusic`.
; `_chipzero` es la palabra de silencio que el playroutine usa como "sample" cero.
;
; El módulo se elige con `-DMED_MODULE_FILE="..."` (por defecto, el de prueba):
;   EXTRA_DEFINES='-DENG_AUDIO_OCTAMED -DMED_MODULE_FILE="mammagamma.med"'
; Debe existir en assets/amiga/audio/. Ver docs/engine/architecture/MUSIC_PLAYER.md
; y docs/guides/roadmap/ROADMAP_AUDIO.md (A1).
;============================================================================
	INCLUDE "octamed/med_feature_control.i"
	INCLUDE "octamed/MED_PlayRoutine.i"

; Modulo incrustado. VASM no permite pasar el NOMBRE del fichero por `-D` (no hay
; sustitucion de texto), asi que el modulo se elige con `-DMED_MODULE=<n>` y aqui se
; INCBINa el que toque (anadir una rama por modulo disponible en assets/amiga/audio/).
	IFND	MED_MODULE_NUM
MED_MODULE_NUM	EQU	0
	ENDIF

	SECTION "ChipData",DATA_C
MED_MODULE:
	IF	MED_MODULE_NUM = 1
	INCBIN	"../../assets/amiga/audio/mammagamma.med"
	ELSEIF	MED_MODULE_NUM = 2
	INCBIN	"../../assets/amiga/audio/mammagamma_SPD.med"
	ELSEIF	MED_MODULE_NUM = 3
	INCBIN	"../../assets/amiga/audio/playroutine_test.med"
	ELSE
	INCBIN	"../../assets/amiga/audio/octamed_test.med"
	ENDIF

	XDEF	_chipzero
_chipzero:
	DC.L	0

	END
