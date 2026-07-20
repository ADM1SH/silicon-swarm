TARGET   := aarch64-none-elf
CC       := clang
LD       := ld.lld
OBJCOPY  := llvm-objcopy

BUILD    := build
ELF      := $(BUILD)/silicon_swarm.elf
IMG      := $(BUILD)/silicon_swarm.img

CFLAGS   := -target $(TARGET) -ffreestanding -nostdlib -mcpu=cortex-a72 \
            -std=c11 -Wall -Wextra -O2 -g -I.
ASFLAGS  := -target $(TARGET) -ffreestanding -nostdlib -mcpu=cortex-a72 -g

QEMU     := qemu-system-aarch64
QFLAGS   := -M virt,gic-version=2 -cpu host -accel hvf -m 512

S_SRCS   := boot/start.S boot/vectors.S boot/mmu.S
C_SRCS   := kernel/kmain.c kernel/uart.c kernel/exceptions.c kernel/gic.c kernel/timer.c kernel/framebuffer.c game/input.c
OBJS     := $(patsubst %.S,$(BUILD)/%.o,$(S_SRCS)) \
            $(patsubst %.c,$(BUILD)/%.o,$(C_SRCS))

.PHONY: build run run-gfx debug dumpdtb clean test-host

build: $(IMG)

$(BUILD)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(ELF): $(OBJS) linker.ld
	$(LD) -T linker.ld -o $(ELF) $(OBJS)

$(IMG): $(ELF)
	$(OBJCOPY) -O binary $(ELF) $(IMG)

run: $(IMG)
	$(QEMU) $(QFLAGS) -nographic -kernel $(IMG)

run-gfx: $(IMG)
	$(QEMU) $(QFLAGS) -device ramfb -display cocoa -kernel $(IMG)

debug: $(IMG)
	$(QEMU) $(QFLAGS) -nographic -kernel $(IMG) -s -S

dumpdtb:
	$(QEMU) -M virt,dumpdtb=virt.dtb -cpu host
	dtc -I dtb -O dts virt.dtb -o virt.dts

clean:
	rm -rf $(BUILD) virt.dtb virt.dts
