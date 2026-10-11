# speech/GNUmakefile - builds the speech engine (Flite + a small C library)
# into bin/libspeech.a, which the kernel links in.
#
# It's built apart from the rest of the kernel because it needs floating
# point maths, which the kernel itself turns off.
#
#   make             build bin/libspeech.a
#   make host-test   build bin/host_test, which runs the engine on Linux
#                    and saves a .wav: bin/host_test out.wav "Hello there"

.SUFFIXES:
.DELETE_ON_ERROR:

CC := cc
OPT := -O2

HEADERS := ../kernel/freestanding-c-hdrs/include

override INCLUDES := -nostdinc -isystem $(HEADERS) -I libc/include -I libc -I . \
    -I flite/include -I flite/lang/usenglish -I flite/lang/cmulex \
    -I flite/src/cg -I flite/src/regex -I flite/src/speech

override COMMON := $(OPT) -g -pipe -std=gnu11 -ffreestanding -fno-builtin -fno-common \
    -fno-stack-protector -fno-stack-check -fno-PIC -m64 -march=x86-64 -mno-red-zone \
    -ffunction-sections -fdata-sections

# Flite is someone else's code: build it quietly, as is.
override FLITE_WARNINGS := -w
override OUR_WARNINGS := -Wall -Wextra

override KERNEL_FLAGS := $(COMMON) -mcmodel=kernel
override HOST_FLAGS := $(COMMON)

override FLITE_SRC := $(shell find flite/src flite/lang -name '*.c' | LC_ALL=C sort)
override OUR_SRC := $(wildcard libc/*.c) engine.c
override ASM_SRC := libc/setjmp.S

override KOBJ := $(addprefix obj/kernel/,$(FLITE_SRC:.c=.o) $(OUR_SRC:.c=.o) $(ASM_SRC:.S=.o))
override HOBJ := $(addprefix obj/host/,$(FLITE_SRC:.c=.o) $(OUR_SRC:.c=.o) $(ASM_SRC:.S=.o) test/host_test.o)

.PHONY: all
all: bin/libspeech.a

bin/libspeech.a: $(KOBJ)
	@mkdir -p bin
	@rm -f $@
	@echo "Packing the speech engine into $@"
	@ar rcs $@ $(KOBJ)

obj/kernel/flite/%.o: flite/%.c GNUmakefile
	mkdir -p "$(dir $@)"
	$(CC) $(KERNEL_FLAGS) $(FLITE_WARNINGS) $(INCLUDES) -c $< -o $@

obj/kernel/%.o: %.c GNUmakefile
	mkdir -p "$(dir $@)"
	$(CC) $(KERNEL_FLAGS) $(OUR_WARNINGS) $(INCLUDES) -c $< -o $@

obj/kernel/%.o: %.S GNUmakefile
	mkdir -p "$(dir $@)"
	$(CC) $(KERNEL_FLAGS) -c $< -o $@

.PHONY: host-test
host-test: bin/host_test

bin/host_test: $(HOBJ)
	mkdir -p bin
	$(CC) -static -nostdlib -no-pie $(HOBJ) -o $@

obj/host/flite/%.o: flite/%.c GNUmakefile
	mkdir -p "$(dir $@)"
	$(CC) $(HOST_FLAGS) $(FLITE_WARNINGS) $(INCLUDES) -c $< -o $@

obj/host/%.o: %.c GNUmakefile
	mkdir -p "$(dir $@)"
	$(CC) $(HOST_FLAGS) $(OUR_WARNINGS) $(INCLUDES) -c $< -o $@

obj/host/%.o: %.S GNUmakefile
	mkdir -p "$(dir $@)"
	$(CC) $(HOST_FLAGS) -c $< -o $@

.PHONY: clean
clean:
	rm -rf obj bin
