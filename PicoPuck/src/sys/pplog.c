// pplog.c — see pplog.h.
//
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sys/pplog.h"

#include <string.h>
#include "pico/sync.h"

#define PPLOG_CAP 1024

static char s_buf[PPLOG_CAP];
static uint16_t s_len; // valid bytes at the front of s_buf (oldest→newest)
static spin_lock_t *s_lock;

static spin_lock_t *lock(void)
{
	// Producers run in the BTstack/async context, the snapshot in the TinyUSB
	// task — different call sites, so serialise with a spin lock (claimed lazily
	// to avoid an init-order dependency).
	if (!s_lock)
		s_lock = spin_lock_instance(spin_lock_claim_unused(true));
	return s_lock;
}

void pplog(const char *s)
{
	if (!s || !*s)
		return;
	uint16_t n = (uint16_t)strlen(s);
	uint32_t sv = spin_lock_blocking(lock());
	if (n >= PPLOG_CAP) {
		memcpy(s_buf, s + (n - PPLOG_CAP), PPLOG_CAP);
		s_len = PPLOG_CAP;
	} else {
		if ((uint32_t)s_len + n > PPLOG_CAP) {
			uint16_t drop = (uint16_t)(s_len + n - PPLOG_CAP);
			memmove(s_buf, s_buf + drop, (size_t)(s_len - drop));
			s_len = (uint16_t)(s_len - drop);
		}
		memcpy(s_buf + s_len, s, n);
		s_len = (uint16_t)(s_len + n);
	}
	spin_unlock(lock(), sv);
}

uint16_t pplog_snapshot(uint8_t *out, uint16_t max)
{
	uint32_t sv = spin_lock_blocking(lock());
	uint16_t n = s_len;
	uint16_t off = 0;
	if (n > max) {
		off = (uint16_t)(n - max); // keep the most-recent `max` bytes
		n = max;
	}
	memcpy(out, s_buf + off, n);
	spin_unlock(lock(), sv);
	return n;
}

// ---- characteristic dump (append-only, cleared per discovery) --------------
#define PPCHARS_CAP 512
static char s_chars[PPCHARS_CAP];
static uint16_t s_chars_len;

void pplog_chars_reset(void)
{
	uint32_t sv = spin_lock_blocking(lock());
	s_chars_len = 0;
	spin_unlock(lock(), sv);
}

void pplog_chars(const char *s)
{
	if (!s || !*s)
		return;
	uint16_t n = (uint16_t)strlen(s);
	uint32_t sv = spin_lock_blocking(lock());
	if ((uint32_t)s_chars_len + n <= PPCHARS_CAP) {
		memcpy(s_chars + s_chars_len, s, n);
		s_chars_len = (uint16_t)(s_chars_len + n);
	}
	spin_unlock(lock(), sv);
}

uint16_t pplog_chars_snapshot(uint8_t *out, uint16_t max)
{
	uint32_t sv = spin_lock_blocking(lock());
	uint16_t n = s_chars_len > max ? max : s_chars_len;
	memcpy(out, s_chars, n);
	spin_unlock(lock(), sv);
	return n;
}
