#ifndef GAME_INPUT_H
#define GAME_INPUT_H

// UART key polling -> game action mapping (Phase 6, extended in Phase 11
// for build-phase tool selection/placement). Discrete keypresses only for
// v1, no key-repeat/hold semantics.
typedef enum {
    INPUT_NONE = 0,
    INPUT_UP,
    INPUT_DOWN,
    INPUT_LEFT,
    INPUT_RIGHT,
    INPUT_PLACE,          // space -- place the selected tool at the cursor
    INPUT_START,          // enter/return -- start the siege
    INPUT_TOOL_BARRICADE, // '1'
    INPUT_TOOL_TURRET,    // '2'
    INPUT_RAISE,          // 'q' -- raise terrain under the cursor (v2)
    INPUT_LOWER,          // 'e' -- lower terrain under the cursor (v2)
    INPUT_TOOL_ROAD,      // '3' (v2)
    INPUT_TOOL_HOUSE,     // '4' (v2)
    INPUT_DEMOLISH,       // 'x' (v2)
    INPUT_ROTATE,         // 'r' -- rotate the view 90 degrees (v3)
} input_action_t;

// Non-blocking. Returns INPUT_NONE if no byte was pending or the byte read
// didn't map to a known action.
input_action_t input_poll(void);

#endif
