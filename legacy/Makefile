# Makefile del proyecto C legado (demo Amiga en C con musica ProTracker).
# Vive en legacy/ y reutiliza el soporte común de la raíz (../support/).
# NO usar para el engine C++ (engine/, demos/): ese flujo usa tools/build/build-demo.ps1.
# Para generar el listing con LTO, añadir a LDFLAGS: -Wa,-adhln=$@.listing,--listing-rhs-width=200
# Para mejores anotaciones, añadir -dA -dP.

ifdef OS
	WINDOWS = 1
	SHELL = cmd.exe
	MKDIR_P = @if not exist obj mkdir obj & if not exist out mkdir out
else
	MKDIR_P = @mkdir -p obj out
endif

program = out/a
OUT = $(program)

# Objetivo por defecto explícito: las asignaciones target-specific de `debug`
# aparecen antes que `all` y, sin esto, GNU make tomaría `debug` como objetivo
# por defecto (limpiaría y compilaría en -O1 en cada `make`).
.DEFAULT_GOAL := all

ifdef AMIGA_BIN_PATH
	CC = $(AMIGA_BIN_PATH)\opt\bin\m68k-amiga-elf-gcc.exe
	AS = $(AMIGA_BIN_PATH)\opt\bin\m68k-amiga-elf-as.exe
	VASM = $(AMIGA_BIN_PATH)\vasmm68k_mot.exe
	ELF2HUNK = $(AMIGA_BIN_PATH)\elf2hunk.exe
	OBJDUMP = $(AMIGA_BIN_PATH)\opt\bin\m68k-amiga-elf-objdump.exe
	SDKDIR = $(AMIGA_BIN_PATH)\opt\m68k-amiga-elf\sys-include
else
	CC = m68k-amiga-elf-gcc
	AS = m68k-amiga-elf-as
	VASM = vasmm68k_mot
	ELF2HUNK = elf2hunk
	OBJDUMP = m68k-amiga-elf-objdump
ifdef WINDOWS
	SDKDIR = $(abspath $(dir $(shell where $(CC)))..\m68k-amiga-elf\sys-include)
else
	SDKDIR = $(abspath $(dir $(shell which $(CC)))../m68k-amiga-elf/sys-include)
endif
endif

# Soporte común en la raíz del repo.
SUPPORT = ../support

# -I.. para resolver #include "support/gcc8_c_support.h" desde legacy/.
# DWARF (-g) para GDB / Amiga Debug; GetVBR vive en $(SUPPORT)/getvbr.c (compilado sin -g).
# Sin -fwhole-program: con LTO elimina símbolos globales que solo se escriben
# (p. ej. SysBase, usado por getvbr.o, que se compila sin LTO) y rompe el enlazado.
CCFLAGS   = -g -MP -MMD -m68000 -Ofast -nostdlib -Wextra -Wno-unused-function -Wno-volatile-register-var -fomit-frame-pointer -fno-tree-loop-distribution -flto -fno-exceptions -ffunction-sections -fdata-sections -I..
ASFLAGS   = -mcpu=68000 -g --register-prefix-optional -I$(SDKDIR)
# -Ttext=0x400 coincide con el ELF_TEXT_BASE del gdbserver de WinUAE-DBG para la relocalización de breakpoints.
LDFLAGS   = -Wl,--emit-relocs,--gc-sections,-Ttext=0x400,-Map=$(OUT).map

GETVBR_FLAGS = -MP -MMD -m68000 -nostdlib -Wextra -Wno-unused-function -Wno-volatile-register-var -fno-exceptions -ffunction-sections -fdata-sections

# Build amigable para depuración (breakpoints + step): make debug
# -O1: gcc 15 ICE a -O0 en inline-asm (p61Init, doynaxdepack, GetVBR); -O1 conserva DWARF + código C depurable.
debug: CCFLAGS := -g -MP -MMD -m68000 -O1 -nostdlib -Wextra -Wno-unused-function -Wno-volatile-register-var -fomit-frame-pointer -fno-exceptions -ffunction-sections -fdata-sections -I..
debug: GETVBR_FLAGS := -MP -MMD -m68000 -O1 -nostdlib -Wextra -Wno-unused-function -Wno-volatile-register-var -fno-exceptions -ffunction-sections -fdata-sections
debug: LDFLAGS := -Wl,--emit-relocs,--gc-sections,-Ttext=0x400,-Map=$(OUT).map
debug: clean all
VASMFLAGS = -m68000 -Felf -opt-fconst -nowarn=62 -dwarf=3 -quiet -x -I. -I$(SDKDIR)

objects := obj/main.o obj/gcc8_c_support.o obj/getvbr.o obj/gcc8_a_support.o obj/depacker_doynax.o obj/depacker_doynax_vasm.o

all: $(OUT).exe

$(objects): | obj

obj:
	$(MKDIR_P)

$(OUT).exe: $(OUT).elf
	$(info Elf2Hunk $(program).exe)
	@$(ELF2HUNK) $(OUT).elf $(OUT).exe

$(OUT).elf: $(objects)
	$(info Linking $(program).elf)
	@$(CC) $(CCFLAGS) $(LDFLAGS) $(objects) -o $@
	@$(OBJDUMP) --disassemble --no-show-raw-ins --visualize-jumps -S $@ >$(OUT).s

clean:
	$(info Cleaning...)
ifdef WINDOWS
	@if exist obj del /q obj\* & if exist out del /q out\* & cd .
else
	@$(RM) obj/* out/*
endif

-include $(objects:.o=.d)

obj/main.o: main.c
	$(info Compiling $<)
	@$(CC) $(CCFLAGS) -c -o $@ $(CURDIR)/$<

obj/gcc8_c_support.o: $(SUPPORT)/gcc8_c_support.c
	$(info Compiling $<)
	@$(CC) $(CCFLAGS) -c -o $@ $(CURDIR)/$<

# getvbr.c: sin DWARF (gcc 15 ICE).
obj/getvbr.o: $(SUPPORT)/getvbr.c
	$(info Compiling $< (no debug info))
	@$(CC) $(GETVBR_FLAGS) -g0 -c -o $@ $(CURDIR)/$<

obj/gcc8_a_support.o: $(SUPPORT)/gcc8_a_support.s
	$(info Assembling $<)
	@$(AS) $(ASFLAGS) --MD $(basename $@).d -o $@ $(CURDIR)/$<

obj/depacker_doynax.o: $(SUPPORT)/depacker_doynax.s
	$(info Assembling $<)
	@$(AS) $(ASFLAGS) --MD $(basename $@).d -o $@ $(CURDIR)/$<

obj/depacker_doynax_vasm.o: $(SUPPORT)/depacker_doynax_vasm.asm
	$(info Assembling $<)
	@$(VASM) $(VASMFLAGS) -dependall=make -depfile $(basename $@).d -o $@ $(CURDIR)/$<
