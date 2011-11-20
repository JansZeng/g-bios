#pragma once

#ifdef __arm__
#include <arm/types.h>
#else
#include <arm/types.h> // fixme
// #error "arch not supported yet"
#endif

#define MKFOURCC(a, b, c, d)    (((a) << 24) | (b) << 16 | ((c) << 8) | (d))

#define __PACKED__        __attribute__((packed))
#define __WEAK__          __attribute__((weak))

// #define PRINT_ARG_FORMAT __attribute__((format (printf, 1, 2)))

#define DECLARE_REBOOT(func) \
	void reset(void) __attribute__((alias(#func)))

#define NULL  ((void *)0)

#define ARRAY_ELEM_NUM(arr) (sizeof(arr) / sizeof((arr)[0]))

#define container_of(ptr, type, member) \
	(type *)((char *)ptr - (char *)(&((type *)0)->member))

#define min_t(type, x, y) \
		({ type __x = (x); type __y = (y); __x < __y ? __x: __y; })

#define max_t(type, x, y) \
		({ type __x = (x); type __y = (y); __x > __y ? __x: __y; })

#define min(x, y) ((x) < (y) ? (x) : (y))
#define max(x, y) ((x) > (y) ? (x) : (y))

// fixme
#define SWAP(a,b) \
		do { \
			typeof(a) __temp; \
			__temp = (a); \
			(a) = (b); \
			(b) = __temp; \
		} while(0)

#define KB(n) ((n) << 10)
#define MB(n) ((n) << 20)
#define GB(n) ((n) << 30)
