#include "thread.h"
#include "syslib.h"
#include "blockpixel.h"
#include <stdint.h>

// Print score at top row, similar to pong
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
#define BUCKET_H 1
#define MAX_DOTS 16

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
static uint8_t bp_buffer[SCREEN_W * SCREEN_H];

void draw_bucket() {
	for (int i = 0; i < BUCKET_W; i++) {
		bp_put(&bp, bucket_x + i, SCREEN_H - 2, 7, BP_LAZY); // white
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
	print_score(2, score);
}

void clear_screen() {
	for (int x = 0; x < SCREEN_W; x++)
		for (int y = 0; y < SCREEN_H; y++)
			bp_put(&bp, x, y, 0, BP_LAZY);
}

void bucket_thread(void *arg) {
	while (!game_over) {
		int c = thread_get();
		sema_dec(sem_state);
		if (c == 'a' && bucket_x > 0) bucket_x--;
		if (c == 'd' && bucket_x < SCREEN_W - BUCKET_W) bucket_x++;
		sema_inc(sem_state);
		thread_sleep(user_gettime() + 30000000ULL); // 30ms
	}
	thread_exit();
}

void falling_thread(void *arg) {
	int tick = 0;
	while (!game_over) {
		sema_dec(sem_state);
		// Move dots
		for (int i = 0; i < MAX_DOTS; i++) {
			if (!dots[i].active) continue;
			dots[i].y++;
			// Check collision with bucket
			if (dots[i].y == SCREEN_H - 2 && dots[i].x >= bucket_x && dots[i].x < bucket_x + BUCKET_W) {
				if (dots[i].type == DOT_GREEN) score++;
				else if (dots[i].type == DOT_GOLD) score += 10;
				else if (dots[i].type == DOT_RED) game_over = 1;
				dots[i].active = 0;
			} else if (dots[i].y >= SCREEN_H - 1) {
				dots[i].active = 0;
			}
		}
		// Spawn new dots
		if (tick % 20 == 0) {
			for (int i = 0; i < MAX_DOTS; i++) {
				if (!dots[i].active) {
					dots[i].x = (user_gettime() * 17 + i * 13) % SCREEN_W;
					dots[i].y = 2;
					int r = (user_gettime() * 23 + i * 7) % 100;
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
		thread_sleep(user_gettime() + 20000000ULL); // 20ms
	}
	thread_exit();
}

void background_thread(void *arg) {
	int phase = 0;
	while (!game_over) {
		// Animate background by changing color of top row
		for (int x = 0; x < SCREEN_W; x++)
			bp_put(&bp, x, 0, phase % 8, BP_LAZY);
		phase++;
		thread_sleep(user_gettime() + 50000000ULL); // 50ms
	}
	thread_exit();
}

void main(void) {
	thread_init();

	sem_state = sema_create(1);
	bp_init(&bp, 0, 0, SCREEN_W, SCREEN_H, bp_buffer);
	clear_screen();
	thread_create(bucket_thread, 0, 4096);
	thread_create(falling_thread, 0, 4096);
	thread_create(background_thread, 0, 4096);
	while (!game_over) {
		sema_dec(sem_state);
		clear_screen();
		draw_score();
		draw_dots();
		draw_bucket();
		bp_flush(&bp);
		sema_inc(sem_state);
		thread_sleep(user_gettime() + 20000000ULL); // 20ms
	}
	print_score(SCREEN_W / 2 - 5, score);
	// Print GAME OVER in center
	const char *msg = "GAME OVER";
	int msglen = 9;
	int x0 = (SCREEN_W - msglen) / 2;
	int y0 = SCREEN_H / 2;
	for (int i = 0; i < msglen; i++) {
		user_put(x0 + i, y0, CELL(msg[i], 4, 0));
	}
	thread_sleep(user_gettime() + 200000000ULL); // 200ms
	thread_exit();

}
