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
    INPUT_DEMOLISH,       // 'x' (v2)
    INPUT_ROTATE,         // 'r' -- rotate the view 90 degrees (v3)
    INPUT_TOOL_AVENUE,     // '4' (v4)
    INPUT_TOOL_ZONE_R,     // '5' (v4)
    INPUT_TOOL_ZONE_C,     // '6' (v4)
    INPUT_TOOL_ZONE_I,     // '7' (v4)
    INPUT_TOOL_WATCHTOWER, // '8' (v4)
    INPUT_TOOL_GRANARY,    // '9' (v4)
    INPUT_TOOL_BARRACKS,   // '0' (v4)
    INPUT_TAX_CYCLE,       // 't' -- select which tax slider (R/C/I) (v4)
    INPUT_TAX_DOWN,        // '-' (v4)
    INPUT_TAX_UP,          // '=' (v4)
    INPUT_UP_FAST,         // shift+WASD -- 5-tile cursor jumps (v5)
    INPUT_DOWN_FAST,
    INPUT_LEFT_FAST,
    INPUT_RIGHT_FAST,
    INPUT_SPEED,           // 'f' -- cycle sim speed 1x/2x/4x (v5)
} input_action_t;

// Non-blocking. Returns INPUT_NONE if no byte was pending or the byte read
// didn't map to a known action.
input_action_t input_poll(void);

#endif
