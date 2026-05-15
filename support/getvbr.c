#include "gcc8_c_support.h"
#include <exec/execbase.h>
#include <proto/exec.h>

extern struct ExecBase *SysBase;

/* Isolated from main.c: gcc 15 ICE in dwarf2 CFI for Supervisor()+inline MOVEC snippet. */
void *GetVBR(void) {
	void *vbr = 0;
	UWORD getvbr[] = { 0x4e7a, 0x0801, 0x4e73 }; /* MOVEC.L VBR,D0 RTE */

	if (SysBase->AttnFlags & AFF_68010)
		vbr = (void *)Supervisor((ULONG (*)())getvbr);

	return vbr;
}
