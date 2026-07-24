// PicoPuck main — core0 cooperative loop.
//
// Presents either the Valve puck (Steam/Lizard) or an emulated controller
// (Xbox/Switch/PS5/PS3/…) over USB, fed by controllers connected over Bluetooth.
// The active mode is loaded from flash at boot; switching it persists + reboots.
//
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "config/picopuck_config.h"
#include "config/modes.h"
#include "puck/identity.h"
#include "puck/slots.h"
#include "puck/personality.h"
#include "puck/emu_present.h"
#include "puck/xinput.h"
#include "usb/usb_tx.h"
#include "usb/usb_descriptors.h"
#include "usb/webusb.h"
#include "bt/bt_host.h"
#include "sys/settings.h"

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/watchdog.h"
#include "tusb.h"

int main(void)
{
	stdio_init_all();

	settings_load();  // active USB mode (before descriptors)
	identity_init();
	slots_init();
	puck_personality_init();
	webusb_init();

	// Build the descriptor set for the active mode, then start USB.
	usb_descriptors_init();
	tud_init(0);

	bool puck = mode_is_puck(settings_mode());
	bool xinput = mode_is_xinput(settings_mode());

	// The user LED hangs off the CYW43 chip; this init also hands the radio to
	// BTstack (btstack_cyw43_init on the async_context).
	bool have_radio = (cyw43_arch_init() == 0);
	bool have_bt = have_radio && bt_host_init();

	printf("\n[picopuck] boot commit=%s board=%d mode=%d radio=%d bt=%d\n",
	       PICOPUCK_GIT_COMMIT, PP_BOARD, settings_mode(), have_radio, have_bt);

	watchdog_enable(PP_WATCHDOG_MS, /*pause_on_debug=*/true);

	absolute_time_t next_led = make_timeout_time_ms(PP_STATUS_LED_MS);
	bool led_on = false;

	while (1) {
		tud_task();       // USB device events
		usb_tx_pump();    // drain queued HID reports

		if (puck)
			puck_personality_task();  // puck 0x79 / 0x7B / 0x43 + synth stream
		else if (xinput)
			xinput_task();            // Xbox 360 XInput report stream
		else
			emu_present_task();       // emulated controller report stream

		webusb_task();    // panel commands (works in every mode)

		if (have_bt) {
			cyw43_arch_poll();  // services BTstack on its async_context
			bt_host_task();     // scan / battery / rssi / rumble
		} else if (have_radio) {
			cyw43_arch_poll();
		}

		if (absolute_time_diff_us(get_absolute_time(), next_led) <= 0) {
			led_on = !led_on;
			if (have_radio)
				cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_on);
			next_led = make_timeout_time_ms(PP_STATUS_LED_MS);
		}

		watchdog_update();
	}

	return 0;
}
