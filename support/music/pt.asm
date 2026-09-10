        xdef    _PtInstallCIA
        xdef    _PtRemoveCIA
        xdef    _PtInit
        xdef    _PtEnd
        xdef    _PtData
        xdef    _PtEnable
        xdef    _PtE8Trigger

        section '.text',code

_PtData         set     mt_data+mt_chan1
_PtEnable       set     _mt_Enable
_PtE8Trigger    set     _mt_E8Trigger

_PtInstallCIA:
        movem.l d2-d7/a2-a6,-(sp)
        st.b    d0
        move.l  _ExcVecBase,a0
        lea     $dff000,a6
        jsr     _mt_install_cia
        movem.l (sp)+,d2-d7/a2-a6
        rts

_PtRemoveCIA:
        movem.l d2-d7/a2-a6,-(sp)
        lea     $dff000,a6
        jsr     _mt_remove_cia
        movem.l (sp)+,d2-d7/a2-a6
        rts

_PtInit:
        movem.l d2-d7/a2-a6,-(sp)
        lea     $dff000,a6
        jsr     _mt_init
        movem.l (sp)+,d2-d7/a2-a6
        rts

_PtEnd:
        movem.l d2-d7/a2-a6,-(sp)
        lea     $dff000,a6
        jsr     _mt_end
        movem.l (sp)+,d2-d7/a2-a6
        rts

        section '.text.ptplayer',code

        include 'ptplayer.i'

        xdef    _PtGetPos
        xdef    _PtGetPeriod

; Devuelve en D0 la fila actual (mt_PatternPos) y en D1 el patrón (mt_SongPos),
; para diagnosticar si la música avanza (o se queda en un tono fijo).
_PtGetPos:
        lea     mt_data(pc),a0
        move.w  mt_PatternPos(a0),d0
        moveq   #0,d1
        move.b  mt_SongPos(a0),d1
        rts

; Devuelve en D0 el período actual del canal 1 (mt_chan1+n_period), para
; diagnosticar si el tono cambia al avanzar la melodía.
_PtGetPeriod:
        lea     mt_data(pc),a0
        move.w  n_period(a0),d0
        rts
