#include "game/input.h"
#include "kernel/uart.h"
#include "kernel/virtio_input.h"

input_action_t input_poll(void) {
    uint8_t c;
    // Two sources, same alphabet: the virtio keyboard (game window) and
    // the serial console (terminal) both work.
    if (!virtio_input_poll_char(&c) && !uart_getc_nonblock(&c)) {
        return INPUT_NONE;
    }
    switch (c) {
    case 'w':
    case 'W':
        return INPUT_UP;
    case 's':
    case 'S':
        return INPUT_DOWN;
    case 'a':
    case 'A':
        return INPUT_LEFT;
    case 'd':
    case 'D':
        return INPUT_RIGHT;
    case ' ':
        return INPUT_PLACE;
    case '\r':
    case '\n':
        return INPUT_START;
    case '1':
        return INPUT_TOOL_BARRICADE;
    case '2':
        return INPUT_TOOL_TURRET;
    case 'q':
    case 'Q':
        return INPUT_RAISE;
    case 'e':
    case 'E':
        return INPUT_LOWER;
    case '3':
        return INPUT_TOOL_ROAD;
    case '4':
        return INPUT_TOOL_AVENUE;
    case '5':
        return INPUT_TOOL_ZONE_R;
    case '6':
        return INPUT_TOOL_ZONE_C;
    case '7':
        return INPUT_TOOL_ZONE_I;
    case '8':
        return INPUT_TOOL_WATCHTOWER;
    case '9':
        return INPUT_TOOL_GRANARY;
    case '0':
        return INPUT_TOOL_BARRACKS;
    case 't':
    case 'T':
        return INPUT_TAX_CYCLE;
    case '-':
        return INPUT_TAX_DOWN;
    case '=':
    case '+':
        return INPUT_TAX_UP;
    case 'x':
    case 'X':
        return INPUT_DEMOLISH;
    case 'r':
    case 'R':
        return INPUT_ROTATE;
    default:
        return INPUT_NONE;
    }
}
