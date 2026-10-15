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
           -Ikernel -Idrivers -Igfx -Iui -Ifs -Iapps
LDFLAGS := -m elf_i386 -T linker.ld -nostdlib -z noexecstack

SRCS := $(wildcard kernel/*.c drivers/*.c gfx/*.c ui/*.c fs/*.c apps/*.c)
ASMS := $(wildcard boot/*.S assets/*.S)
OBJS := $(patsubst %.c,build/%.o,$(SRCS)) $(patsubst %.S,build/%.o,$(ASMS))

QEMU_FLAGS := -m 256M -vga std -serial stdio -rtc base=localtime

.PHONY: all run run-headless clean iso

all: build/kepler.elf

build/kepler.elf: $(OBJS) linker.ld
	@$(LD) $(LDFLAGS) -o $@ $(OBJS)
	@echo "built $@ ($$(du -h $@ | cut -f1))"

build/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "  CC  $<"
	@$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

build/%.o: %.S
	@mkdir -p $(dir $@)
	@echo "  CC  $<"
	@$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# .incbin isn't seen by -MMD
build/assets/assets.o: assets/stockwallpaper

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

-include $(OBJS:.o=.d)
