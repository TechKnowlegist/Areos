# apps/GNUmakefile - builds the AREOS apps into .arx files.
#
# To add an app: write apps/yourapp.c, then add "yourapp" to one of the
# two lists below. A bigger app can be a folder instead (apps/yourapp/
# full of .c files): add it to FOLDER_APPS as well.
#   ELF_APPS:    .arx with ELF inside (the standard format)
#   NATIVE_APPS: .arx in the native AREOS format (packed by tools/arxpack)

ELF_APPS    := hello oops starfield desktop
NATIVE_APPS := countdown
FOLDER_APPS := desktop

CC      := cc
HOST_CC := cc

# Programs run with no C library and no operating system extras, so tell
# the compiler not to assume any. Floating point stays off for now.
CFLAGS := -O2 -pipe -Wall -Wextra -std=gnu11 -ffreestanding -fno-builtin \
          -fno-stack-protector -fno-stack-check -fno-PIC -fno-pie \
          -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -mno-80387 \
          -fcf-protection=none -fno-stack-clash-protection \
          -ffunction-sections -fdata-sections -I include
LDFLAGS := -nostdlib -static -no-pie -Wl,-T,lib/app.ld -Wl,--build-id=none -Wl,--gc-sections \
           -Wl,-z,max-page-size=0x1000 -Wl,-z,noexecstack

LIB := bin/start.o bin/memory.o bin/font.o bin/gui.o

# Keep in-between files (.o, .elf) so rebuilds are quick.
.SECONDARY:
ARXPACK := ../tools/bin/arxpack

.PHONY: all
all: $(addprefix bin/,$(addsuffix .arx,$(ELF_APPS) $(NATIVE_APPS)))

bin/%.o: lib/%.c include/areos.h include/gui.h include/font.h
	mkdir -p bin
	$(CC) $(CFLAGS) -c $< -o $@

bin/%.app.o: %.c include/areos.h include/gui.h
	mkdir -p bin
	$(CC) $(CFLAGS) -c $< -o $@

bin/%.elf: bin/%.app.o $(LIB) lib/app.ld
	$(CC) $(CFLAGS) $(LDFLAGS) $< $(LIB) -o $@

# Folder apps: every .c file in apps/name/ is built and linked together.
define FOLDER_APP_RULES
bin/$(1).elf: $$(patsubst $(1)/%.c,bin/$(1)/%.o,$$(wildcard $(1)/*.c)) $$(LIB) lib/app.ld
	$$(CC) $$(CFLAGS) $$(LDFLAGS) $$(filter %.o,$$^) -o $$@

bin/$(1)/%.o: $(1)/%.c $$(wildcard $(1)/*.h) include/areos.h include/gui.h
	mkdir -p bin/$(1)
	$$(CC) $$(CFLAGS) -I $(1) -c $$< -o $$@
endef
$(foreach app,$(FOLDER_APPS),$(eval $(call FOLDER_APP_RULES,$(app))))

# ELF-inside apps: the ELF file simply gets the .arx name.
$(addprefix bin/,$(addsuffix .arx,$(ELF_APPS))): bin/%.arx: bin/%.elf
	cp $< $@

# Native apps: repacked by arxpack.
$(addprefix bin/,$(addsuffix .arx,$(NATIVE_APPS))): bin/%.arx: bin/%.elf $(ARXPACK)
	$(ARXPACK) $< $@

$(ARXPACK): ../tools/arxpack.c
	mkdir -p ../tools/bin
	$(HOST_CC) -O2 -Wall -Wextra $< -o $@

.PHONY: clean
clean:
	rm -rf bin ../tools/bin
