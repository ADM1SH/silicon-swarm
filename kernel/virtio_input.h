// Bare-metal virtio-input (keyboard) over virtio-mmio, polling only.
// Lets keystrokes typed into the QEMU graphical window reach the game —
// the guest finally has an actual keyboard device instead of relying on
// serial stdin. Needs `-device virtio-keyboard-device` on the QEMU line.
#ifndef KERNEL_VIRTIO_INPUT_H
#define KERNEL_VIRTIO_INPUT_H

#include <stdint.h>

// Scans the virt machine's 32 virtio-mmio slots for an input device and
// brings its event queue up. Safe to call when absent (returns 0; serial
// input keeps working as the fallback).
int virtio_input_init(void);

// Non-blocking: next key press/auto-repeat mapped to the game's ASCII
// input alphabet ('w','1',' ','\r',...). 0 = none pending.
int virtio_input_poll_char(uint8_t *out);

#endif
