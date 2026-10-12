# apps/GNUmakefile - builds the AREOS apps into .arx files.
#
# To add an app: write apps/yourapp.c, then add "yourapp" to one of the
# two lists below. A bigger app can be a folder instead (apps/yourapp/
# full of .c files): add it to FOLDER_APPS as well.
#   ELF_APPS:    .arx with ELF inside (the standard format)
#   NATIVE_APPS: .arx in the native AREOS format (packed by tools/arxpack)

ELF_APPS    := hello oops starfield desktop jstest
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

.DEFAULT_GOAL := all

# BearSSL (bearssl.org): the TLS code the browser uses for https. Built into
# a library; only the parts the browser actually uses end up in the app.
BEARSSL_SRCS := $(wildcard bearssl/src/*/*.c) $(wildcard bearssl/src/*.c)
BEARSSL_OBJS := $(patsubst bearssl/src/%.c,bin/bearssl/%.o,$(BEARSSL_SRCS))
BEARSSL_LIB  := bin/libbearssl.a

bin/bearssl/%.o: bearssl/src/%.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -w -I bearssl/inc -I bearssl/src -c $< -o $@

$(BEARSSL_LIB): $(BEARSSL_OBJS)
	rm -f $@
	ar rcs $@ $^

# QuickJS (bellard.org/quickjs): the JavaScript engine the browser uses. It
# needs floating point, so it is built without the -mno-sse flags, along with
# a small C library (js/libc) and AREOS's glue (js/jshost.c).
JSFLAGS := -O2 -pipe -std=gnu11 -ffreestanding -fno-builtin -fno-stack-protector \
           -fno-stack-check -fno-PIC -fno-pie -mno-red-zone -fcf-protection=none \
           -fno-stack-clash-protection -fno-math-errno -ffunction-sections -fdata-sections \
           -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -w -DAREOS -D_GNU_SOURCE \
           -DCONFIG_VERSION='"2025-04-26"' -I js/include -I js -I js/libc -I include
JS_SRCS := $(wildcard js/*.c) $(wildcard js/libc/*.c)
JS_OBJS := $(patsubst js/%.c,bin/js/%.o,$(JS_SRCS)) bin/js/libc/setjmp.o bin/js/jsprelude.o
JS_LIB  := bin/libquickjs.a

bin/js/%.o: js/%.c $(wildcard js/*.h) $(wildcard js/libc/*.h) $(wildcard js/include/*.h) include/areos.h
	mkdir -p $(dir $@)
	$(CC) $(JSFLAGS) -c $< -o $@

bin/js/libc/setjmp.o: js/libc/setjmp.S
	mkdir -p $(dir $@)
	$(CC) -c $< -o $@

# The page-environment script, joined from its parts and built into the program.
bin/js/prelude.js: $(wildcard js/prelude/*.js) js/prelude/build.sh
	mkdir -p bin/js
	sh js/prelude/build.sh $(abspath $@)

bin/js/jsprelude.o: js/jsprelude.S bin/js/prelude.js
	mkdir -p bin/js
	$(CC) -c $< -o $@

$(JS_LIB): $(JS_OBJS)
	rm -f $@
	ar rcs $@ $^

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
	$(CC) $(CFLAGS) -I . -c $< -o $@

bin/%.elf: bin/%.app.o $(LIB) lib/app.ld
	$(CC) $(CFLAGS) $(LDFLAGS) $< $(LIB) -o $@

# jstest uses the JavaScript engine.
bin/jstest.elf: bin/jstest.app.o $(LIB) $(JS_LIB) lib/app.ld
	$(CC) $(CFLAGS) $(LDFLAGS) $< $(LIB) $(JS_LIB) -lgcc -o $@

# Folder apps: every .c file in apps/name/ is built and linked together.
define FOLDER_APP_RULES
bin/$(1).elf: $$(patsubst $(1)/%.c,bin/$(1)/%.o,$$(wildcard $(1)/*.c)) $$(EXTRA_OBJS_$(1)) $$(LIB) $$(BEARSSL_LIB) $$(JS_LIB) lib/app.ld
	$$(CC) $$(CFLAGS) $$(LDFLAGS) $$(filter %.o,$$^) $$(BEARSSL_LIB) $$(JS_LIB) -lgcc -o $$@

bin/$(1)/%.o: $(1)/%.c $$(wildcard $(1)/*.h) $$(wildcard $(1)/*.inc) include/areos.h include/gui.h
	mkdir -p bin/$(1)
	$$(CC) $$(CFLAGS) -I $(1) -I bearssl/inc -I js -c $$< -o $$@
endef
# The desktop's web engine: a few files need floating point (TrueType fonts,
# the newer CSS color spaces), so they are built like QuickJS, without the
# -mno-sse flags. The fonts themselves are built in as data (fonts.S).
EXTRA_OBJS_desktop := bin/desktop/fonts.o
FLOAT_OBJS_desktop := bin/desktop/font_ttf.o bin/desktop/web_color_f.o bin/desktop/svg.o
$(FLOAT_OBJS_desktop): bin/desktop/%.o: desktop/%.c $(wildcard desktop/*.h) include/areos.h
	mkdir -p bin/desktop
	$(CC) $(JSFLAGS) -I desktop -c $< -o $@
bin/desktop/fonts.o: desktop/fonts.S $(wildcard desktop/fonts/*.ttf)
	mkdir -p bin/desktop
	$(CC) -c $< -o $@

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
