// usb_mount.cpp -- OpenPuck's platform hooks for the SHARED dynamic-mount policy
// (src/common/usb_mount.c). The policy (map/debounce/add-now-remove-lazy) is
// shared; here we supply the RF-specific connected signal (a slot counts as
// connected while it has replied within CONN_UP_MS) and the clock. The actual
// re-enumerate (usbReenumerate) lives in OpenPuck.ino.
#include "usb_mount.h"
#include "bonds.h" // g_slot / g_connReplyMs
#include <Arduino.h>

// More lenient than the 300 ms link-up check so a brief RF gap doesn't unmount a
// slot; the shared debounce adds further hysteresis.
#define CONN_UP_MS 1200u

extern "C" uint8_t usb_mount_connected_mask(void)
{
	unsigned long now = millis();
	uint8_t m = 0;
	for (int s = 0; s < NSLOT; s++)
		if (g_slot[s].used && g_connReplyMs[s] != 0 &&
		    (now - g_connReplyMs[s]) < CONN_UP_MS)
			m |= (uint8_t)(1u << s);
	return m;
}

extern "C" uint32_t usb_mount_last_input_ms(void)
{
	unsigned long last = 0;
	for (int s = 0; s < NSLOT; s++)
		if (g_slot[s].used && g_connReplyMs[s] > last)
			last = g_connReplyMs[s];
	return (uint32_t)last;
}

extern "C" uint32_t usb_mount_now_ms(void)
{
	return (uint32_t)millis();
}
