#ifndef GAME_INPUT_H
#define GAME_INPUT_H

// UART key polling -> game action mapping (Phase 6). Discrete keypresses
// only for v1, no key-repeat/hold semantics.
typedef enum {
    INPUT_NONE = 0,
    INPUT_UP,
    INPUT_DOWN,
    INPUT_LEFT,
    INPUT_RIGHT,
    INPUT_CONFIRM,
} input_action_t;

// Non-blocking. Returns INPUT_NONE if no byte was pending or the byte read
// didn't map to a known action.
input_action_t input_poll(void);

#endif
