#include "thread.h"
#include "syslib.h"
#include "blockpixel.h"
#include <stdint.h>

// Print score as text on the top row
void print_score(int x, int score) {
	if (score == 0) {
		user_put(x, 0, CELL('0', 7, 0));
		return;
	}
	while (score != 0) {
		user_put(x, 0, CELL('0' + score % 10, 7, 0));
		score /= 10;
		x--;
	}
}

#define SCREEN_W 39
#define SCREEN_H 20
#define BUCKET_W 7
#define MAX_DOTS 16

// Game area starts at row 1 (row 0 is reserved for the score HUD)
#define GAME_Y_OFFSET 1
#define GAME_H (SCREEN_H - GAME_Y_OFFSET)

enum DotType { DOT_GREEN, DOT_GOLD, DOT_RED };

typedef struct {
	int x, y;
	enum DotType type;
	int active;
} Dot;

static int bucket_x = SCREEN_W / 2 - BUCKET_W / 2;
static int score = 0;
static int game_over = 0;
static Dot dots[MAX_DOTS];
static struct sema *sem_state;
static struct bp bp;
static uint8_t bp_buffer[SCREEN_W * GAME_H];

void draw_bucket() {
	for (int i = 0; i < BUCKET_W; i++) {
		bp_put(&bp, bucket_x + i, GAME_H - 2, 7, BP_LAZY);
	}
}

void draw_dots() {
	for (int i = 0; i < MAX_DOTS; i++) {
		if (!dots[i].active) continue;

		uint8_t color = 2;
		if (dots[i].type == DOT_GOLD) color = 6;
		else if (dots[i].type == DOT_RED) color = 4;

		bp_put(&bp, dots[i].x, dots[i].y, color, BP_LAZY);
	}
}

void draw_score() {
	// Clear the entire score row first to prevent leftover digits/colors
	for (int i = 0; i < SCREEN_W; i++) {
		user_put(i, 0, CELL(' ', 7, 0));
	}
	const char *label = "SCORE:";
	for (int i = 0; label[i] != '\0'; i++) {
		user_put(i, 0, CELL(label[i], 7, 0));
	}
	print_score(10, score);
}

void clear_screen() {
	for (int x = 0; x < SCREEN_W; x++) {
		for (int y = 0; y < GAME_H; y++) {
			bp_put(&bp, x, y, 0, BP_LAZY);
		}
	}
}

void bucket_thread(void *arg) {
	(void)arg;
	while (!game_over) {
		int c = thread_get();

		sema_dec(sem_state);
		if (c == 'a' && bucket_x > 0) bucket_x--;
		if (c == 'd' && bucket_x < SCREEN_W - BUCKET_W) bucket_x++;
		sema_inc(sem_state);

		thread_sleep(user_gettime() + 30000000ULL);
	}
	thread_exit();
}

void falling_thread(void *arg) {
	(void)arg;
	int tick = 0;

	while (!game_over) {
		sema_dec(sem_state);

		// Move balls more slowly: only every 3 ticks
		if (tick % 3 == 0) {
			for (int i = 0; i < MAX_DOTS; i++) {
				if (!dots[i].active) continue;

				dots[i].y++;

				// Catch at bucket row (GAME_H - 2)
				if (dots[i].y == GAME_H - 2 &&
				    dots[i].x >= bucket_x &&
				    dots[i].x < bucket_x + BUCKET_W) {
					if (dots[i].type == DOT_GREEN) score += 1;
					else if (dots[i].type == DOT_GOLD) score += 10;
					else if (dots[i].type == DOT_RED) game_over = 1;
					dots[i].active = 0;
				} else if (dots[i].y >= GAME_H - 1) {
					dots[i].active = 0;
				}
			}
		}

		// Spawn new balls
		if (tick % 30 == 0) {
			for (int i = 0; i < MAX_DOTS; i++) {
				if (!dots[i].active) {
					dots[i].x = (int)((user_gettime() * 17ULL + i * 13ULL) % SCREEN_W);
					dots[i].y = 1;

					int r = (int)((user_gettime() * 23ULL + i * 7ULL) % 100);
					if (r < 80) dots[i].type = DOT_GREEN;
					else if (r < 95) dots[i].type = DOT_RED;
					else dots[i].type = DOT_GOLD;

					dots[i].active = 1;
					break;
				}
			}
		}

		sema_inc(sem_state);
		tick++;
		thread_sleep(user_gettime() + 40000000ULL);
	}
	thread_exit();
}

void background_thread(void *arg) {
	(void)arg;
	int phase = 0;

	while (!game_over) {
		sema_dec(sem_state);
		// Start from row 1 to leave a clean gap under the score HUD
		for (int x = 0; x < SCREEN_W; x++) {
			bp_put(&bp, x, 1, phase % 8, BP_LAZY);
		}
		sema_inc(sem_state);

		phase++;
		thread_sleep(user_gettime() + 50000000ULL);
	}
	thread_exit();
}

void main(void) {
	thread_init();

	sem_state = sema_create(1);

	// bp buffer covers only the game area: rows 1..(SCREEN_H-1) on screen
	bp_init(&bp, 0, GAME_Y_OFFSET, SCREEN_W, GAME_H, bp_buffer);
	clear_screen();

	// Draw the score HUD once before threads start so row 0 is never blank
	draw_score();

	thread_create(bucket_thread, 0, 4096);
	thread_create(falling_thread, 0, 4096);
	thread_create(background_thread, 0, 4096);

	while (!game_over) {
		sema_dec(sem_state);
		clear_screen();
		draw_dots();
		draw_bucket();
		bp_flush(&bp);
		// draw_score uses user_put directly to row 0 — bp_flush never touches it
		draw_score();
		sema_inc(sem_state);

		thread_sleep(user_gettime() + 20000000ULL);
	}

	sema_dec(sem_state);
	clear_screen();
	bp_flush(&bp);
	draw_score();

	const char *msg = "GAME OVER";
	int msglen = 9;
	int x0 = (SCREEN_W - msglen) / 2;
	// Center in the game area (offset by GAME_Y_OFFSET)
	int y0 = GAME_Y_OFFSET + GAME_H / 2;
	for (int i = 0; i < msglen; i++) {
		user_put(x0 + i, y0, CELL(msg[i], 4, 0));
	}
	sema_inc(sem_state);

	thread_sleep(user_gettime() + 200000000ULL);
	thread_exit();
}