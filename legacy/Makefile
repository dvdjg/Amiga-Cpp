# to generate assembler listing with LTO, add to LDFLAGS: -Wa,-adhln=$@.listing,--listing-rhs-width=200
# for better annotations add -dA -dP
# to generate assembler source with LTO, add to LDFLAGS: -save-temps=cwd

ifdef OS
	WINDOWS = 1
	SHELL = cmd.exe
	MKDIR_P = @if not exist obj mkdir obj & if not exist out mkdir out
else
	MKDIR_P = @mkdir -p obj out
endif

subdirs := $(wildcard */)
VPATH = $(subdirs)
cpp_sources := $(wildcard *.cpp) $(wildcard $(addsuffix *.cpp,$(subdirs)))
cpp_objects := $(addprefix obj/,$(patsubst %.cpp,%.o,$(notdir $(cpp_sources))))
c_sources := $(filter-out support/getvbr.c,$(wildcard *.c) $(wildcard $(addsuffix *.c,$(subdirs))))
c_objects := $(addprefix obj/,$(patsubst %.c,%.o,$(notdir $(c_sources))))
s_sources := support/gcc8_a_support.s support/depacker_doynax.s
s_objects := $(addprefix obj/,$(patsubst %.s,%.o,$(notdir $(s_sources))))
vasm_sources := $(wildcard *.asm) $(wildcard $(addsuffix *.asm, $(subdirs)))
vasm_objects := $(addprefix obj/, $(patsubst %.asm,%.o,$(notdir $(vasm_sources))))
objects := $(cpp_objects) $(c_objects) obj/getvbr.o $(s_objects) $(vasm_objects)

# https://stackoverflow.com/questions/4036191/sources-from-subdirectories-in-makefile/4038459
# http://www.microhowto.info/howto/automatically_generate_makefile_dependencies.html

program = out/a
OUT = $(program)

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

# DWARF (-g) for GDB / Amiga Debug; GetVBR lives in support/getvbr.c (compiled without -g)
CCFLAGS   = -g -MP -MMD -m68000 -Ofast -nostdlib -Wextra -Wno-unused-function -Wno-volatile-register-var -fomit-frame-pointer -fno-tree-loop-distribution -flto -fwhole-program -fno-exceptions -ffunction-sections -fdata-sections
CPPFLAGS  = $(CCFLAGS) -fno-rtti -fcoroutines -fno-use-cxa-atexit
ASFLAGS   = -mcpu=68000 -g --register-prefix-optional -I$(SDKDIR)
# -Ttext=0x400 matches WinUAE-DBG gdbserver ELF_TEXT_BASE for breakpoint relocation
LDFLAGS   = -Wl,--emit-relocs,--gc-sections,-Ttext=0x400,-Map=$(OUT).map

GETVBR_FLAGS = -MP -MMD -m68000 -nostdlib -Wextra -Wno-unused-function -Wno-volatile-register-var -fno-exceptions -ffunction-sections -fdata-sections

# Debug-friendly build (breakpoints + step): make debug
# -O1: gcc 15 ICE at -O0 on inline-asm (p61Init, doynaxdepack, GetVBR); -O1 keeps DWARF + debuggable C source
debug: CCFLAGS := -g -MP -MMD -m68000 -O1 -nostdlib -Wextra -Wno-unused-function -Wno-volatile-register-var -fomit-frame-pointer -fno-exceptions -ffunction-sections -fdata-sections
debug: CPPFLAGS := $(CCFLAGS) -fno-rtti -fcoroutines -fno-use-cxa-atexit
debug: GETVBR_FLAGS := -MP -MMD -m68000 -O1 -nostdlib -Wextra -Wno-unused-function -Wno-volatile-register-var -fno-exceptions -ffunction-sections -fdata-sections
debug: LDFLAGS := -Wl,--emit-relocs,--gc-sections,-Ttext=0x400,-Map=$(OUT).map
debug: clean all
VASMFLAGS = -m68000 -Felf -opt-fconst -nowarn=62 -dwarf=3 -quiet -x -I. -I$(SDKDIR)

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
	@del /q obj\* out\*
else
	@$(RM) obj/* out/*
endif

-include $(objects:.o=.d)

$(cpp_objects) : obj/%.o : %.cpp
	$(info Compiling $<)
	@$(CC) $(CPPFLAGS) -c -o $@ $(CURDIR)/$<

# getvbr.c: no DWARF (gcc 15 ICE); must be listed before the generic %.c rule
obj/getvbr.o: support/getvbr.c
	$(info Compiling $< (no debug info))
	@$(CC) $(GETVBR_FLAGS) -g0 -c -o $@ $(CURDIR)/$<

$(c_objects) : obj/%.o : %.c
	$(info Compiling $<)
	@$(CC) $(CCFLAGS) -c -o $@ $(CURDIR)/$<

$(s_objects): obj/%.o : %.s
	$(info Assembling $<)
	@$(AS) $(ASFLAGS) --MD $(@D)/$*.d -o $@ $(CURDIR)/$<

$(vasm_objects): obj/%.o : %.asm
	$(info Assembling $<)
	@$(VASM) $(VASMFLAGS) -dependall=make -depfile $(@D)/$*.d -o $@ $(CURDIR)/$<
