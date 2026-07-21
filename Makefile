TARGET   := aarch64-none-elf
CC       := clang
LD       := ld.lld
OBJCOPY  := llvm-objcopy

BUILD    := build
ELF      := $(BUILD)/silicon_swarm.elf
IMG      := $(BUILD)/silicon_swarm.img

CFLAGS   := -target $(TARGET) -ffreestanding -nostdlib -mcpu=cortex-a72 \
            -std=c11 -Wall -Wextra -O2 -g -I. -MMD -MP
ASFLAGS  := -target $(TARGET) -ffreestanding -nostdlib -mcpu=cortex-a72 -g

QEMU     := qemu-system-aarch64
QFLAGS   := -M virt,gic-version=2 -cpu host -accel hvf -m 512

S_SRCS   := boot/start.S boot/vectors.S boot/mmu.S engine/blit_neon.S
C_SRCS   := kernel/kmain.c kernel/uart.c kernel/exceptions.c kernel/gic.c kernel/timer.c kernel/framebuffer.c kernel/alloc.c kernel/perf.c game/input.c game/build_phase.c game/siege_phase.c game/city.c engine/entity_soa.c engine/flowfield.c engine/spatial_hash.c engine/terrain.c
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
	$(QEMU) $(QFLAGS) -device ramfb -display cocoa -serial stdio -kernel $(IMG)

debug: $(IMG)
	$(QEMU) $(QFLAGS) -nographic -kernel $(IMG) -s -S

dumpdtb:
	$(QEMU) -M virt,dumpdtb=virt.dtb -cpu host
	dtc -I dtb -O dts virt.dtb -o virt.dts

# Host-side unit tests for the hardware-independent engine/ modules (README
# "Testing strategy") -- plain host clang, no -target/-ffreestanding, no QEMU.
test-host:
	@mkdir -p $(BUILD)
	$(CC) -std=c11 -Wall -Wextra -I. -o $(BUILD)/test_entity_soa tests/test_entity_soa.c engine/entity_soa.c
	$(CC) -std=c11 -Wall -Wextra -I. -o $(BUILD)/test_alloc tests/test_alloc.c kernel/alloc.c
	$(CC) -std=c11 -Wall -Wextra -I. -o $(BUILD)/test_flowfield tests/test_flowfield.c engine/flowfield.c kernel/alloc.c
	$(CC) -std=c11 -Wall -Wextra -I. -o $(BUILD)/test_spatial_hash tests/test_spatial_hash.c engine/spatial_hash.c engine/entity_soa.c kernel/alloc.c
	$(CC) -std=c11 -Wall -Wextra -I. -o $(BUILD)/test_build_phase tests/test_build_phase.c game/build_phase.c engine/flowfield.c kernel/alloc.c
	$(CC) -std=c11 -Wall -Wextra -I. -o $(BUILD)/test_terrain tests/test_terrain.c engine/terrain.c
	$(CC) -std=c11 -Wall -Wextra -I. -o $(BUILD)/test_city tests/test_city.c game/city.c engine/terrain.c
	$(BUILD)/test_entity_soa
	$(BUILD)/test_alloc
	$(BUILD)/test_flowfield
	$(BUILD)/test_spatial_hash
	$(BUILD)/test_build_phase
	$(BUILD)/test_terrain
	$(BUILD)/test_city

clean:
	rm -rf $(BUILD) virt.dtb virt.dts

# -MMD -MP (above) emits a .d file per object recording the headers it
# included; pulling those in is what makes `make build` actually notice a
# header-only change like a struct or #define edit, instead of silently
# relinking stale objects.
-include $(OBJS:.o=.d)
