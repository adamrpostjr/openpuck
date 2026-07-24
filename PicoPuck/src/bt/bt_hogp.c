// bt_hogp.c — manual HID-over-GATT client (see bt_hogp.h).
//
// Modeled on the working Valve GATT client. One instance per slot, demuxed by
// con_handle. Enables notifications on the HID service's input Report
// characteristics and forwards each notification to the slot's driver.
//
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "bt/bt_hogp.h"
#include "bt/input_driver.h"
#include "puck/personality.h"
#include "puck/triton.h"
#include "puck/slots.h"
#include "config/picopuck_config.h"

#include <stdio.h>
#include <string.h>
#include "pico/time.h"

// From bt_host.c: which driver is bound to a slot, and the diagnostic counter.
const input_driver_t *bt_slot_driver(int slot);
void bt_host_note_report(int slot);

#define HOGP_SVC_HID 0x1812u
#define HOGP_CHR_REPORT 0x2A4Du
#define HOGP_MAX_INPUTS 4  // input Report characteristics we subscribe per device

typedef enum {
	H_IDLE = 0,
	H_DISC_SERVICE,
	H_DISC_CHARS,
	H_ENABLE_CCC,
	H_READY,
} hogp_state_t;

typedef struct {
	hogp_state_t state;
	int slot;
	hci_con_handle_t handle;
	gatt_client_service_t service;
	gatt_client_characteristic_t input[HOGP_MAX_INPUTS];
	gatt_client_notification_t notify[HOGP_MAX_INPUTS];
	uint8_t n_input;
	uint8_t cccd_idx;  // which input char's CCCD we're enabling
	gatt_client_characteristic_t output;  // first writable report char (rumble)
	bool have_output;
	uint32_t last_batt_ms;  // periodic Battery Service poll
} hogp_t;

#define HOGP_BATT_POLL_MS 30000u

static hogp_t s_hogp[PP_NSLOT];

static hogp_t *by_handle(hci_con_handle_t h)
{
	for (int i = 0; i < PP_NSLOT; i++)
		if (s_hogp[i].state != H_IDLE && s_hogp[i].handle == h)
			return &s_hogp[i];
	return NULL;
}

static void hogp_reset(hogp_t *g)
{
	if (g->state == H_READY)
		for (int i = 0; i < g->n_input; i++)
			gatt_client_stop_listening_for_characteristic_value_updates(
				&g->notify[i]);
	memset(g, 0, sizeof(*g));
	g->handle = HCI_CON_HANDLE_INVALID;
}

void hogp_disconnected(hci_con_handle_t handle)
{
	hogp_t *g = by_handle(handle);
	if (g)
		hogp_reset(g);
}

static void notify_handler(uint8_t type, uint16_t channel, uint8_t *packet,
			   uint16_t size)
{
	(void)channel;
	(void)size;
	if (type != HCI_EVENT_PACKET ||
	    hci_event_packet_get_type(packet) != GATT_EVENT_NOTIFICATION)
		return;
	hogp_t *g = by_handle(gatt_event_notification_get_handle(packet));
	if (!g || g->state != H_READY)
		return;

	uint16_t len = gatt_event_notification_get_value_length(packet);
	const uint8_t *val = gatt_event_notification_get_value(packet);
	const input_driver_t *drv = bt_slot_driver(g->slot);
	if (!drv || !drv->decode || len < 1)
		return;

	bt_host_note_report(g->slot);
	// HOGP report-characteristic values carry no report-id byte (the id lives in
	// the report reference). For the Xbox layout, the 14–16 byte report is the
	// main gamepad state (report 0x01); a short one is the legacy guide (0x02).
	uint8_t rid = (len >= 14) ? 0x01 : 0x02;
	if (drv->decode(rid, val, len, &g_in[g->slot]))
		puck_present_synth(g->slot);
}

static void gatt_handler(uint8_t type, uint16_t channel, uint8_t *packet,
			 uint16_t size);

static void hogp_write_next_ccc(hogp_t *g)
{
	// Write the CCCD (enable notifications) for input[cccd_idx].
	gatt_client_write_client_characteristic_configuration(
		gatt_handler, g->handle, &g->input[g->cccd_idx],
		GATT_CLIENT_CHARACTERISTICS_CONFIGURATION_NOTIFICATION);
}

static void gatt_handler(uint8_t type, uint16_t channel, uint8_t *packet,
			 uint16_t size)
{
	(void)channel;
	(void)size;
	if (type != HCI_EVENT_PACKET)
		return;
	hogp_t *g = by_handle(gatt_event_query_complete_get_handle(packet));
	if (!g)
		return;

	switch (hci_event_packet_get_type(packet)) {
	case GATT_EVENT_SERVICE_QUERY_RESULT:
		gatt_event_service_query_result_get_service(packet, &g->service);
		break;
	case GATT_EVENT_CHARACTERISTIC_QUERY_RESULT: {
		gatt_client_characteristic_t ch;
		gatt_event_characteristic_query_result_get_characteristic(packet, &ch);
		// Input report characteristics support NOTIFY (0x10); the rumble output
		// report supports WRITE (0x08) or WRITE-WITHOUT-RESPONSE (0x04).
		if (ch.uuid16 == HOGP_CHR_REPORT && (ch.properties & 0x10) &&
		    g->n_input < HOGP_MAX_INPUTS)
			g->input[g->n_input++] = ch;
		else if (ch.uuid16 == HOGP_CHR_REPORT && (ch.properties & 0x0C) &&
			 !g->have_output) {
			g->output = ch;
			g->have_output = true;
		}
		break;
	}
	case GATT_EVENT_QUERY_COMPLETE: {
		uint8_t status = gatt_event_query_complete_get_att_status(packet);
		if (status != ATT_ERROR_SUCCESS) {
			printf("[hogp] query failed state=%d status=0x%02X\n",
			       g->state, status);
			hogp_reset(g);
			break;
		}
		if (g->state == H_DISC_SERVICE) {
			if (g->service.start_group_handle == 0) {
				printf("[hogp] no HID service\n");
				hogp_reset(g);
				break;
			}
			g->state = H_DISC_CHARS;
			gatt_client_discover_characteristics_for_service(
				gatt_handler, g->handle, &g->service);
		} else if (g->state == H_DISC_CHARS) {
			if (g->n_input == 0) {
				printf("[hogp] no input report characteristic\n");
				hogp_reset(g);
				break;
			}
			g->state = H_ENABLE_CCC;
			g->cccd_idx = 0;
			hogp_write_next_ccc(g);
		} else if (g->state == H_ENABLE_CCC) {
			// One CCCD written; start listening on that char, then next.
			gatt_client_listen_for_characteristic_value_updates(
				&g->notify[g->cccd_idx], notify_handler, g->handle,
				&g->input[g->cccd_idx]);
			g->cccd_idx++;
			if (g->cccd_idx < g->n_input) {
				hogp_write_next_ccc(g);
			} else {
				g->state = H_READY;
				printf("[hogp] ready (%d input chars, slot %d)\n",
				       g->n_input, g->slot);
			}
		}
		break;
	}
	default:
		break;
	}
}

// Battery Service (0x180F / level char 0x2A19) read completion.
static void battery_read_handler(uint8_t type, uint16_t channel, uint8_t *packet,
				 uint16_t size)
{
	(void)channel;
	(void)size;
	if (type != HCI_EVENT_PACKET ||
	    hci_event_packet_get_type(packet) !=
		    GATT_EVENT_CHARACTERISTIC_VALUE_QUERY_RESULT)
		return;
	hogp_t *g = by_handle(
		gatt_event_characteristic_value_query_result_get_handle(packet));
	if (!g)
		return;
	if (gatt_event_characteristic_value_query_result_get_value_length(packet) >= 1) {
		uint8_t pct = gatt_event_characteristic_value_query_result_get_value(packet)[0];
		if (pct > 100)
			pct = 100;
		g_battery[g->slot] = pct;
		g_battery_state[g->slot] = 1;  // discharging (BAS has no charge state)
	}
}

// Periodic Battery Service poll for connected HOGP pads. Call from the loop.
void hogp_periodic(void)
{
	uint32_t now = to_ms_since_boot(get_absolute_time());
	for (int i = 0; i < PP_NSLOT; i++) {
		hogp_t *g = &s_hogp[i];
		if (g->state != H_READY)
			continue;
		if (g->last_batt_ms && (now - g->last_batt_ms) < HOGP_BATT_POLL_MS)
			continue;
		if (!gatt_client_is_ready(g->handle))
			continue;
		g->last_batt_ms = now ? now : 1;
		gatt_client_read_value_of_characteristics_by_uuid16(
			battery_read_handler, g->handle, 0x0001, 0xFFFF, 0x2A19);
	}
}

void hogp_send_output(int slot, const uint8_t *data, uint16_t len)
{
	if (slot < 0 || slot >= PP_NSLOT)
		return;
	hogp_t *g = &s_hogp[slot];
	if (g->state != H_READY || !g->have_output)
		return;
	// Write-without-response for low-latency rumble; ignore the return (best-effort).
	gatt_client_write_value_of_characteristic_without_response(
		g->handle, g->output.value_handle, len, (uint8_t *)data);
}

void hogp_start(int slot, hci_con_handle_t handle)
{
	if (slot < 0 || slot >= PP_NSLOT)
		return;
	hogp_t *g = &s_hogp[slot];
	memset(g, 0, sizeof(*g));
	g->slot = slot;
	g->handle = handle;
	g->state = H_DISC_SERVICE;
	printf("[hogp] discovering HID service (slot %d)\n", slot);
	gatt_client_discover_primary_services_by_uuid16(gatt_handler, handle,
							HOGP_SVC_HID);
}
