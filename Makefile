# KeplerOS build. Needs gcc (any x86 gcc that accepts -m32), GNU ld and,
# for `make run`, qemu-system-i386.

CC      := gcc
LD      := ld
QEMU    := qemu-system-i386

CFLAGS  := -m32 -march=i686 -std=gnu11 -O2 -g \
           -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
           -fno-asynchronous-unwind-tables -fno-tree-loop-distribute-patterns \
           -mno-sse -mno-sse2 -mno-mmx \
           -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
           -Ikernel -Idrivers -Igfx -Iui -Ifs -Iapps -Inet -Ithird_party/bearssl/inc -Ithird_party/compat
LDFLAGS := -m elf_i386 -T linker.ld -nostdlib -z noexecstack

SRCS := $(wildcard kernel/*.c drivers/*.c gfx/*.c ui/*.c fs/*.c apps/*.c net/*.c)
ASMS := $(wildcard boot/*.S assets/*.S)
OBJS := $(patsubst %.c,build/%.o,$(SRCS)) $(patsubst %.S,build/%.o,$(ASMS))

# BearSSL (TLS) is built as a library so only the parts in use get linked.
# Freestanding: no OS randomness or clock, no SIMD.
BEAR_SRCS := $(wildcard third_party/bearssl/src/*.c third_party/bearssl/src/*/*.c)
BEAR_OBJS := $(patsubst %.c,build/%.o,$(BEAR_SRCS))
BEAR_CFLAGS := -m32 -march=i686 -std=gnu11 -O2 -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
               -fno-asynchronous-unwind-tables -mno-sse -mno-sse2 -mno-mmx -w \
               -Ithird_party/compat -Ithird_party/bearssl/inc -Ithird_party/bearssl/src \
               -DBR_64=0 -DBR_INT128=0 -DBR_UMUL128=0 -DBR_RDRAND=0 -DBR_USE_URANDOM=0 -DBR_USE_WIN32_RAND=0 \
               -DBR_USE_UNIX_TIME=0 -DBR_USE_WIN32_TIME=0 -DBR_AES_X86NI=0 -DBR_SSE2=0 -DBR_POWER8=0

QEMU_FLAGS := -m 256M -vga std -serial stdio -rtc base=localtime -nic user,model=e1000

.PHONY: all run run-headless clean iso

all: build/kepler.elf

build/kepler.elf: $(OBJS) build/libbearssl.a linker.ld
	@$(LD) $(LDFLAGS) -o $@ $(OBJS) build/libbearssl.a
	@echo "built $@ ($$(du -h $@ | cut -f1))"

build/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "  CC  $<"
	@$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

build/libbearssl.a: $(BEAR_OBJS)
	@rm -f $@
	@ar rcs $@ $(BEAR_OBJS)
	@echo "  AR  $@"

build/third_party/%.o: third_party/%.c
	@mkdir -p $(dir $@)
	@$(CC) $(BEAR_CFLAGS) -MMD -MP -c $< -o $@

build/%.o: %.S
	@mkdir -p $(dir $@)
	@echo "  CC  $<"
	@$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# .incbin isn't seen by -MMD
build/assets/assets.o: assets/stockwallpaper assets/kepler-icon.jpg assets/kepler-logo.jpg assets/finder-icon.bgra $(wildcard assets/*-icon.bgra)

# ---- KeplerOS on Linux: the shell as an X11 program and window manager ----------
# (linux/build-iso.sh turns it and Ubuntu into the bootable image)

HOST_CC     := gcc
HOST_CFLAGS := -std=gnu11 -O2 -g -DKEPLER_HOSTED -fno-strict-aliasing \
               -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
               -Ikernel -Idrivers -Igfx -Iui -Ifs -Iapps -Inet -Ilinux -Ithird_party/bearssl/inc
HOST_LIBS   := -lX11 -lXext -lXcomposite -lXdamage -lXfixes -lXcursor -lpthread
HOST_SRCS   := kernel/lib.c $(wildcard gfx/*.c ui/*.c fs/*.c linux/*.c) \
               $(filter-out apps/browser.c apps/html.c,$(wildcard apps/*.c)) \
               net/http.c net/inflate.c net/entropy.c net/ca_roots.c
HOST_OBJS   := $(patsubst %.c,build/linux/%.o,$(HOST_SRCS)) build/linux/assets/assets.o
HOST_BEAR   := $(patsubst %.c,build/linux/%.o,$(BEAR_SRCS))

.PHONY: linux
linux: build/linux/kepler-shell

build/linux/kepler-shell: $(HOST_OBJS) build/linux/libbearssl.a
	@$(HOST_CC) -o $@ $(HOST_OBJS) build/linux/libbearssl.a $(HOST_LIBS)
	@echo "built $@"

build/linux/libbearssl.a: $(HOST_BEAR)
	@rm -f $@
	@ar rcs $@ $(HOST_BEAR)

build/linux/third_party/%.o: third_party/%.c
	@mkdir -p $(dir $@)
	@$(HOST_CC) -O2 -w -Ithird_party/bearssl/inc -Ithird_party/bearssl/src -MMD -MP -c $< -o $@

build/linux/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "  CC  $< (linux)"
	@$(HOST_CC) $(HOST_CFLAGS) -MMD -MP -c $< -o $@

build/linux/assets/assets.o: assets/assets.S assets/stockwallpaper assets/kepler-icon.jpg assets/kepler-logo.jpg assets/finder-icon.bgra $(wildcard assets/*-icon.bgra)
	@mkdir -p $(dir $@)
	@$(HOST_CC) -c $< -o $@

run: build/kepler.elf
	$(QEMU) -kernel $< $(QEMU_FLAGS)

# Bootable CD image (needs grub-mkrescue and xorriso)
iso: build/kepler.elf
	@mkdir -p build/iso/boot/grub
	cp build/kepler.elf build/iso/boot/kepler.elf
	printf 'set timeout=0\nset default=0\nmenuentry "KeplerOS" {\n  multiboot /boot/kepler.elf\n  boot\n}\n' > build/iso/boot/grub/grub.cfg
	grub-mkrescue -o build/kepleros.iso build/iso

clean:
	rm -rf build

-include $(OBJS:.o=.d) $(BEAR_OBJS:.o=.d) $(HOST_OBJS:.o=.d) $(HOST_BEAR:.o=.d)
