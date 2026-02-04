#include "syslib.h"
#include "blockpixel.h"
#include <stdint.h>

#define WIDTH 39
#define HEIGHT 22

// Used codex to parse through relevant files and functiosn and suggest
// what kind of code to write

//states of stopwatch
enum sw_state { SW_STOPPED, SW_RUNNING };

//important stopwatch fields to track
struct stopwatch {
    enum sw_state state;
    int has_focus;   //1 means focus 0 is not focus
    uint64_t start_time_ns;  // only valid when running
    uint64_t elapsed_ns;   //accumulates when stopped
};

// block pixel for rendering
static struct bp bp;
static uint8_t bp_buffer[WIDTH * HEIGHT];

//initializes stopwatch
static void sw_init(struct stopwatch *sw, uint64_t now) {
    sw->state = SW_STOPPED;
    sw->has_focus = 0;
    sw->start_time_ns = now;
    sw->elapsed_ns = 0;
}

//toggles state of stopwatch between running and stopped
static void sw_toggle(struct stopwatch *sw, uint64_t now) {
    if (sw->state == SW_STOPPED) {
        sw->state = SW_RUNNING;
        sw->start_time_ns = now;
    } else {
        sw->elapsed_ns += (now - sw->start_time_ns);
        sw->state = SW_STOPPED;
    }
}

static void sw_reset(struct stopwatch *sw, uint64_t now) {
    sw->elapsed_ns = 0;
    if (sw->state == SW_RUNNING)
        sw->start_time_ns = now;
}

static uint64_t sw_current_elapsed(const struct stopwatch *sw, uint64_t now) {
    if (sw->state == SW_RUNNING)
        return sw->elapsed_ns + (now - sw->start_time_ns);
    return sw->elapsed_ns;
}

static void clear_screen(uint8_t color) {
    for (int y = 0; y < HEIGHT; y++)
        for (int x = 0; x < WIDTH; x++)
            bp_put(&bp, x, y, color, BP_LAZY);
}

// draws rectangle at specified position with given color
static void draw_rect(int x0, int y0, int w, int h, uint8_t color) {
    for (int y = y0; y < y0 + h; y++)
        for (int x = x0; x < x0 + w; x++)
            bp_put(&bp, x, y, color, BP_LAZY);
}

//digit table for 3x5 representation
static const uint8_t digit3x5[10][5] = {
    {0x7,0x5,0x5,0x5,0x7}, // 0
    {0x2,0x6,0x2,0x2,0x7}, // 1
    {0x7,0x1,0x7,0x4,0x7}, // 2
    {0x7,0x1,0x7,0x1,0x7}, // 3
    {0x5,0x5,0x7,0x1,0x1}, // 4
    {0x7,0x4,0x7,0x1,0x7}, // 5
    {0x7,0x4,0x7,0x5,0x7}, // 6
    {0x7,0x1,0x1,0x1,0x1}, // 7
    {0x7,0x5,0x7,0x5,0x7}, // 8
    {0x7,0x5,0x7,0x1,0x7}, // 9
};

// uses block pixel to draw a single digit at (x,y) with given color
static void draw_digit(int x, int y, int d, uint8_t color) {
    for (int row = 0; row < 5; row++) {
        uint8_t bits = digit3x5[d][row];
        for (int col = 0; col < 3; col++) {
            if (bits & (1 << (2 - col)))
                bp_put(&bp, x + col, y + row, color, BP_LAZY);
        }
    }
}

// uses block pixel to draw colon
static void draw_colon(int x, int y, uint8_t color) {
    bp_put(&bp, x, y + 1, color, BP_LAZY);
    bp_put(&bp, x, y + 3, color, BP_LAZY);
}

// renders color and design of stopwatch
static void sw_render(struct stopwatch *sw, uint64_t now) {
    uint64_t elapsed = sw_current_elapsed(sw, now);
    uint64_t ms = elapsed / 1000000ULL;
    int minutes = (ms / 60000) % 60;
    int seconds = (ms / 1000) % 60;
    int tenths  = (ms / 100) % 10;

    uint8_t bg = sw->has_focus ? ANSI_BLUE : ANSI_BLACK;
    uint8_t fg = (sw->state == SW_RUNNING) ? ANSI_GREEN : ANSI_YELLOW;

    clear_screen(bg);
    draw_rect(1, 1, WIDTH - 2, HEIGHT - 2, ANSI_BLACK);

    // MM:SS.t layout
    int y = 8;
    draw_digit(5,  y, minutes / 10, fg);
    draw_digit(9,  y, minutes % 10, fg);
    draw_colon(13, y, ANSI_WHITE);
    draw_digit(15, y, seconds / 10, fg);
    draw_digit(19, y, seconds % 10, fg);
    bp_put(&bp, 23, y + 4, ANSI_WHITE, BP_LAZY); // dot
    draw_digit(25, y, tenths, ANSI_RED);

    // Running/stopped indicator bar
    draw_rect(2, 2, WIDTH - 4, 1, sw->state == SW_RUNNING ? ANSI_GREEN : ANSI_RED);

    bp_flush(&bp);
}

// handles different events like focus change and key presses
static void handle_event(struct stopwatch *sw, int ev, uint64_t now) {
    if (ev == USER_GET_GOT_FOCUS) {
        sw->has_focus = 1;
        return;
    }
    if (ev == USER_GET_LOST_FOCUS) {
        sw->has_focus = 0;
        return;
    }
    if (ev == 's') {
        sw_toggle(sw, now);
        return;
    }
    if (ev == 'r') {
        sw_reset(sw, now);
        return;
    }
    if (ev == 'q') {
        user_exit();
    }
}

int main(void) {
    // initialize block pixel and stopwatch
    struct stopwatch sw;
    bp_init(&bp, 0, 0, WIDTH, HEIGHT, bp_buffer);

    uint64_t now = user_gettime();
    sw_init(&sw, now);
    

    sw_render(&sw, now);

    // loop that handles events and updates display
    for (;;) {
        int ev;
        if (sw.state == SW_RUNNING) {
            ev = user_get(0);
            now = user_gettime();
            if (ev != USER_GET_NO_INPUT)
                handle_event(&sw, ev, now);
            sw_render(&sw, now);
            user_delay(10);
        } else {
            ev = user_get(1);
            now = user_gettime();
            handle_event(&sw, ev, now);
            sw_render(&sw, now);
        }
    }
}


