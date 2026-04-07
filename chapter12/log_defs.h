#define L_BOOT  LOG_TYPE_INFO
#define L_BASE  LOG_TYPE_INFO

#pragma once
#include <stdint.h>

#define LOG_N_LEVELS 4

enum log_type {
	LOG_TYPE_NONE = 0,
	LOG_TYPE_ALLOC,
	LOG_TYPE_FREE,
	LOG_TYPE_READ,
	LOG_TYPE_WRITE,
	LOG_TYPE_ERROR,
	LOG_TYPE_INFO,
	LOG_TYPE_DEBUG,
	LOG_TYPE_MAX
};

#define L_CTX_SWITCH      LOG_TYPE_DEBUG
#define L_CTX_START       LOG_TYPE_INFO
#define L_USER_SLEEP_START LOG_TYPE_DEBUG
#define L_FREQ            LOG_TYPE_INFO
#define L_NORM            LOG_TYPE_INFO

struct log_entry {
	enum log_type type;
	int arity;
	uint64_t ts[LOG_N_LEVELS];
	uint64_t self;
	uint64_t payload[3];
};

struct log_header {
	unsigned sizes[LOG_N_LEVELS];
	unsigned ts[LOG_N_LEVELS];
};

#pragma once

#define L_NORM 0
#define L_BASE 1
#define L_FREQ 2
#define L_DIE  3
#define L_FLAT_INIT 10
#define L_FLAT_CREATE 11
#define L_FLAT_SIZE 12
#define L_FLAT_READ 13
#define L_FLAT_WRITE 14
#define L_FLAT_DELETE 15
#define L_RAMDISK_READ 20
#define L_RAMDISK_WRITE 21