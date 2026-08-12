// bt_valve.c — Steam Controller 2 native-BLE client (see bt_valve.h).
//
// Ported from joypad-os btstack_host.c's Valve GATT client. One instance per
// slot; events are demuxed by con_handle.
//
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "bt/bt_valve.h"
#include "puck/personality.h"
#include "puck/triton.h"
#include "config/picopuck_config.h"
#include "sys/pplog.h"

#include <stdio.h>
#include <string.h>
#include "pico/time.h"

// 128-bit UUIDs in textual/big-endian order (as BTstack's uuid128 API expects).
static const uint8_t VALVE_SERVICE[16] = { 0x10, 0x0F, 0x6C, 0x32, 0x17, 0x35,
					   0x43, 0x13, 0xB4, 0x02, 0x38, 0x56,
					   0x71, 0x31, 0xE5, 0xF3 };
static const uint8_t VALVE_INPUT_45[16] = { 0x10, 0x0F, 0x6C, 0x7A, 0x17, 0x35,
					    0x43, 0x13, 0xB4, 0x02, 0x38, 0x56,
					    0x71, 0x31, 0xE5, 0xF3 };
static const uint8_t VALVE_INPUT_47[16] = { 0x10, 0x0F, 0x6C, 0x7C, 0x17, 0x35,
					    0x43, 0x13, 0xB4, 0x02, 0x38, 0x56,
					    0x71, 0x31, 0xE5, 0xF3 };
static const uint8_t VALVE_REPORT[16] = { 0x10, 0x0F, 0x6C, 0x34, 0x17, 0x35,
					  0x43, 0x13, 0xB4, 0x02, 0x38, 0x56,
					  0x71, 0x31, 0xE5, 0xF3 };

// Every write to the SC2's report characteristic (100F6C34) is BLE-segmented:
// each GATT write is [header][<=18 payload], header = 0x80(DATA) | seq(0-2) |
// 0x40(LAST). A report that fits one segment is prefixed 0xC0. This is the Steam
// Controller BLE framing (SDL SetFeatureReport); WITHOUT it the SC2 silently
// drops every write — keepalive, settings, and haptics all no-op.
#define BLE_SEG_DATA 0x80
#define BLE_SEG_LAST 0x40
#define BLE_SEG_PAYLOAD 18

// The SC2 re-enables lizard (keyboard/mouse) mode on a ~3 s watchdog, so resend
// lizard-off well inside that. {0x87 ID_SET_SETTINGS, 0x03 len, 0x09 id, u16 0},
// prefixed with the single-segment header 0xC0.
#define VALVE_KEEPALIVE_MS 2000u
static uint8_t s_lizard_off[6] = { 0xC0, 0x87, 0x03, 0x09, 0x00, 0x00 };
// IMU on: SEND_RAW_ACCEL(0x08) | SEND_RAW_GYRO(0x10) = 0x18 on setting id 0x30.
static uint8_t s_imu_on[6] = { 0xC0, 0x87, 0x03, 0x30, 0x18, 0x00 };

typedef enum {
	V_IDLE = 0,
	V_DISC_SERVICE,
	V_DISC_CHARS,
	V_ENABLE_CCC,
	V_READY,
} valve_state_t;

#define VALVE_WQ 8 // pending host→controller writes
#define VALVE_WMAX 62 // max report bytes per write

typedef struct {
	valve_state_t state;
	int slot;
	hci_con_handle_t handle;
	gatt_client_service_t service;
	gatt_client_characteristic_t input_char;
	uint8_t report_id; // 0x45 or 0x47
	uint16_t report_value_handle; // 100F6C34 write target
	gatt_client_notification_t notify;
	uint32_t last_keepalive_ms;
	bool imu_enabled;

	// Every WRITABLE characteristic discovered in the Valve service (handle +
	// last two UUID bytes), for the haptic-target bisector. 100F6C34 is the
	// feature/report char; the extras (6CBC/BD/BE…) are candidate output/haptic
	// targets whose correct char+framing we don't yet know.
	struct {
		uint16_t handle;
		uint8_t uuid2, uuid3;
	} wr[8];
	uint8_t wr_n;

	// One-shot haptic-bisector test write (target an arbitrary char/handle with a
	// chosen framing). Drained with top priority in valve_periodic.
	uint16_t test_handle;
	uint8_t test_buf[24];
	uint8_t test_len;
	bool test_pending;

	// Relayed feature/output reports awaiting a free GATT slot (one write in
	// flight at a time). Drop-oldest when full so a stall can't wedge input.
	uint8_t wq[VALVE_WQ][VALVE_WMAX];
	uint8_t wq_len[VALVE_WQ];
	uint16_t wq_handle[VALVE_WQ]; // per-entry target characteristic
	uint8_t wq_head, wq_tail, wq_count;

	// Dedicated Triton output-report characteristics (haptics/LED): one writable
	// char per report id, keyed by [report_id - 0x80]. SDL derives the id from the
	// last UUID byte: id = last_byte - 0x35, so rumble 0x80 is 100F6CB5. These are
	// the only valid targets for output reports; the feature/report char 100F6C34
	// is for 0x87 settings writes. Sending haptics to 100F6C34 is silently dropped.
	uint16_t out_handle[10];
} valve_t;

static valve_t s_valve[PP_NSLOT];

// Diagnostic: GATT feature writes actually issued to SC2 controllers.
volatile uint16_t g_valve_writes;

static uint32_t now_ms(void)
{
	return to_ms_since_boot(get_absolute_time());
}

static valve_t *by_handle(hci_con_handle_t h)
{
	for (int i = 0; i < PP_NSLOT; i++)
		if (s_valve[i].state != V_IDLE && s_valve[i].handle == h)
			return &s_valve[i];
	return NULL;
}

static void valve_reset(valve_t *v)
{
	if (v->state == V_READY)
		gatt_client_stop_listening_for_characteristic_value_updates(
			&v->notify);
	memset(v, 0, sizeof(*v));
	v->handle = HCI_CON_HANDLE_INVALID;
}

void valve_disconnected(hci_con_handle_t handle)
{
	valve_t *v = by_handle(handle);
	if (v)
		valve_reset(v);
}

// Input notifications: prepend the report id and forward verbatim to the slot.
static void notify_handler(uint8_t type, uint16_t channel, uint8_t *packet,
			   uint16_t size)
{
	(void)channel;
	(void)size;
	if (type != HCI_EVENT_PACKET ||
	    hci_event_packet_get_type(packet) != GATT_EVENT_NOTIFICATION)
		return;
	hci_con_handle_t h = gatt_event_notification_get_handle(packet);
	valve_t *v = by_handle(h);
	if (!v || v->state != V_READY)
		return;
	if (gatt_event_notification_get_value_handle(packet) !=
	    v->input_char.value_handle)
		return;

	uint16_t len = gatt_event_notification_get_value_length(packet);
	const uint8_t *val = gatt_event_notification_get_value(packet);
	if (len < 18)
		return;

	uint8_t rep[PUCK45_LEN + 8];
	if ((size_t)len + 1 > sizeof(rep))
		len = sizeof(rep) - 1;
	rep[0] = v->report_id;
	memcpy(rep + 1, val, len);
	// Decode into g_in so emulated modes work with an SC2 too; puck mode also
	// forwards the raw report verbatim (transparent). rep[1] is seq.
	triton_decode45(rep, (uint8_t)(len + 1), &g_in[v->slot]);
	puck_present_raw(v->slot, rep, (uint8_t)(len + 1));
}

static void write_cb(uint8_t type, uint16_t channel, uint8_t *packet,
		     uint16_t size)
{
	(void)type;
	(void)channel;
	(void)packet;
	(void)size; // completion only; ignore
}

static void gatt_handler(uint8_t type, uint16_t channel, uint8_t *packet,
			 uint16_t size)
{
	(void)channel;
	(void)size;
	if (type != HCI_EVENT_PACKET)
		return;
	hci_con_handle_t h = gatt_event_query_complete_get_handle(packet);
	valve_t *v = by_handle(h);
	if (!v)
		return;

	switch (hci_event_packet_get_type(packet)) {
	case GATT_EVENT_SERVICE_QUERY_RESULT:
		gatt_event_service_query_result_get_service(packet,
							    &v->service);
		break;
	case GATT_EVENT_CHARACTERISTIC_QUERY_RESULT: {
		gatt_client_characteristic_t ch;
		gatt_event_characteristic_query_result_get_characteristic(
			packet, &ch);
		// Dump EVERY characteristic in the Valve service (not just the three we
		// recognise) so a real SC2 connection reveals whether haptics have their
		// own output char we're currently ignoring. props: 0x04=write-no-resp,
		// 0x08=write, 0x10=notify. Mirror it into the WebUSB log ring so it shows
		// up in the panel even with no UART adapter attached.
		{
			char lb[48];
			snprintf(lb, sizeof(lb),
				 "char %02X%02X%02X%02X h=%04X p=%02X\n",
				 ch.uuid128[0], ch.uuid128[1], ch.uuid128[2],
				 ch.uuid128[3], ch.value_handle, ch.properties);
			printf("[sc2] %s", lb);
			pplog_chars(lb);
		}
		if (memcmp(ch.uuid128, VALVE_INPUT_45, 16) == 0) {
			v->input_char = ch;
			v->report_id = 0x45;
		} else if (memcmp(ch.uuid128, VALVE_INPUT_47, 16) == 0 &&
			   v->report_id == 0) {
			v->input_char = ch;
			v->report_id = 0x47;
		} else if (memcmp(ch.uuid128, VALVE_REPORT, 16) == 0) {
			v->report_value_handle = ch.value_handle;
		}
		// Record every writable char (props 0x08 write | 0x04 write-no-resp) as a
		// haptic-target candidate for the bisector.
		if ((ch.properties & 0x0C) && v->wr_n < 8) {
			v->wr[v->wr_n].handle = ch.value_handle;
			v->wr[v->wr_n].uuid2 = ch.uuid128[2];
			v->wr[v->wr_n].uuid3 = ch.uuid128[3];
			v->wr_n++;
		}
		// A Valve writable char whose last UUID byte is 0xB5..0xBE is a dedicated
		// output (haptic/LED) char for report id = byte-0x35 (rumble 0x80 →
		// 100F6CB5). Remember its handle so valve_feature_write routes
		// 0x80-0x89 there instead of the feature/report char 100F6C34.
		if ((ch.properties & 0x0C) && ch.uuid128[3] >= 0xB5 &&
		    ch.uuid128[3] <= 0xBE)
			v->out_handle[ch.uuid128[3] - 0xB5] = ch.value_handle;
		break;
	}
	case GATT_EVENT_QUERY_COMPLETE: {
		uint8_t status =
			gatt_event_query_complete_get_att_status(packet);
		if (status != ATT_ERROR_SUCCESS) {
			printf("[sc2] GATT query failed state=%d status=0x%02X\n",
			       v->state, status);
			valve_reset(v);
			break;
		}
		if (v->state == V_DISC_SERVICE) {
			if (v->service.start_group_handle == 0) {
				printf("[sc2] no Valve service\n");
				valve_reset(v);
				break;
			}
			v->state = V_DISC_CHARS;
			v->wr_n = 0;
			// fresh characteristic dump per discovery
			pplog_chars_reset();
			gatt_client_discover_characteristics_for_service(
				gatt_handler, v->handle, &v->service);
		} else if (v->state == V_DISC_CHARS) {
			if (v->report_id == 0 ||
			    v->input_char.value_handle == 0) {
				printf("[sc2] no Valve input characteristic\n");
				valve_reset(v);
				break;
			}
			v->state = V_ENABLE_CCC;
			gatt_client_write_client_characteristic_configuration(
				gatt_handler, v->handle, &v->input_char,
				GATT_CLIENT_CHARACTERISTICS_CONFIGURATION_NOTIFICATION);
		} else if (v->state == V_ENABLE_CCC) {
			gatt_client_listen_for_characteristic_value_updates(
				&v->notify, notify_handler, v->handle,
				&v->input_char);
			v->state = V_READY;
			v->last_keepalive_ms = now_ms();
			printf("[sc2] ready (report id 0x%02X)\n",
			       v->report_id);
		}
		break;
	}
	default:
		break;
	}
}

void valve_start(int slot, hci_con_handle_t handle)
{
	if (slot < 0 || slot >= PP_NSLOT)
		return;
	valve_t *v = &s_valve[slot];
	memset(v, 0, sizeof(*v));
	v->slot = slot;
	v->handle = handle;
	v->state = V_DISC_SERVICE;
	printf("[sc2] discovering Valve service (slot %d)\n", slot);
	gatt_client_discover_primary_services_by_uuid128(gatt_handler, handle,
							 VALVE_SERVICE);
}

void valve_periodic(void)
{
	for (int i = 0; i < PP_NSLOT; i++) {
		valve_t *v = &s_valve[i];
		if (v->state != V_READY || v->report_value_handle == 0)
			continue;
		if (!gatt_client_is_ready(v->handle))
			continue; // one GATT op at a time

		// Haptic bisector test write — highest priority, one-shot.
		if (v->test_pending) {
			v->test_pending = false;
			gatt_client_write_value_of_characteristic(
				write_cb, v->handle, v->test_handle,
				v->test_len, v->test_buf);
			g_valve_writes++;
			continue;
		}

		// Relayed host writes (haptics/rumble/config) take priority for latency.
		if (v->wq_count) {
			uint8_t *rep = v->wq[v->wq_tail];
			uint8_t rl = v->wq_len[v->wq_tail];
			uint16_t h = v->wq_handle[v->wq_tail];
			v->wq_tail = (uint8_t)((v->wq_tail + 1) % VALVE_WQ);
			v->wq_count--;
			gatt_client_write_value_of_characteristic(
				write_cb, v->handle, h, rl, rep);
			g_valve_writes++;
			continue;
		}

		// Enable IMU once — but only when Steam isn't configuring it itself
		// (its own 0x87 writes are relayed through; don't fight them).
		if (!v->imu_enabled && !puck_steam_active()) {
			v->imu_enabled = true;
			gatt_client_write_value_of_characteristic(
				write_cb, v->handle, v->report_value_handle,
				sizeof(s_imu_on), s_imu_on);
			continue;
		}
		uint32_t t = now_ms();
		if (t - v->last_keepalive_ms >= VALVE_KEEPALIVE_MS) {
			v->last_keepalive_ms = t;
			gatt_client_write_value_of_characteristic(
				write_cb, v->handle, v->report_value_handle,
				sizeof(s_lizard_off), s_lizard_off);
		}
	}
}

// Haptic-target bisector: fire a strong test rumble at one writable char with
// one framing, selected by `variant`. variant = charIndex*3 + framing:
//   framing 0 = raw report [0x80][body]
//   framing 1 = single BLE segment [0xC0][0x80][body]
//   framing 2 = SDL feature form [0x03][0xC0][0x80][body], zero-padded to 20
// Cycle variant from the panel until the SC2 buzzes, then report which won.
void bt_test_haptic(uint8_t variant)
{
	for (int i = 0; i < PP_NSLOT; i++) {
		valve_t *v = &s_valve[i];
		if (v->state != V_READY || v->wr_n == 0)
			continue;
		uint8_t ci = (uint8_t)((variant / 3) % v->wr_n);
		uint8_t fr = (uint8_t)(variant % 3);
		// [id][type=0][inten16=0][spdL16=FFFF][gainL=0][spdR16=FFFF][gainR=0]
		static const uint8_t rep[10] = { 0x80, 0, 0,	0,    0xFF,
						 0xFF, 0, 0xFF, 0xFF, 0 };
		uint8_t *b = v->test_buf;
		uint8_t n = 0;
		if (fr == 0) {
			memcpy(b, rep, sizeof(rep));
			n = sizeof(rep);
		} else if (fr == 1) {
			b[0] = 0xC0;
			memcpy(b + 1, rep, sizeof(rep));
			n = 1 + sizeof(rep);
		} else {
			memset(b, 0, 20);
			b[0] = 0x03;
			b[1] = 0xC0;
			memcpy(b + 2, rep, sizeof(rep));
			n = 20;
		}
		v->test_handle = v->wr[ci].handle;
		v->test_len = n;
		v->test_pending = true;
		char lb[52];
		snprintf(lb, sizeof(lb),
			 "TEST v=%u ch=100F6C%02X%02X h=%04X fr=%u\n", variant,
			 v->wr[ci].uuid2, v->wr[ci].uuid3, v->test_handle, fr);
		pplog(lb);
	}
}

// Append one write to the per-slot queue (drop-oldest if full). The target
// handle rides per entry so haptics and feature writes can hit different chars.
static void vq_push(valve_t *v, uint16_t handle, const uint8_t *data,
		    uint8_t len)
{
	if (v->wq_count == VALVE_WQ) {
		v->wq_tail = (uint8_t)((v->wq_tail + 1) % VALVE_WQ);
		v->wq_count--;
	}
	memcpy(v->wq[v->wq_head], data, len);
	v->wq_len[v->wq_head] = len;
	v->wq_handle[v->wq_head] = handle;
	v->wq_head = (uint8_t)((v->wq_head + 1) % VALVE_WQ);
	v->wq_count++;
}

// Forward the HID report [report_id][body] to an SC2. Output reports
// (0x80-0x89, haptics/LED) go to their per-id output char (id+0x35 → rumble
// 0x80 = 100F6CB5) as the raw body; everything else (0x87 settings, power,
// passthrough) goes to the feature/report char 100F6C34 BLE-segmented. Drained
// in valve_periodic, drop-oldest if the queue fills.
void valve_feature_write(int slot, uint8_t report_id, const uint8_t *body,
			 uint16_t len)
{
	if (slot < 0 || slot >= PP_NSLOT)
		return;
	valve_t *v = &s_valve[slot];
	if (v->state != V_READY || v->report_value_handle == 0)
		return;

	// Triton output reports: dedicated char, body-only (no report-id, no 0xC0
	// segment header — the char is per-report-id so no demux header is needed).
	// SDL HIDDeviceBLESteamController routes them exactly this way. If discovery
	// hasn't pinned the char yet, drop (the SC2 on this link is unusable anyway).
	// Log actuator writes so the panel shows each one reaching the SC2 — the
	// key datum for "is rumble firing at all?".
	if (report_id >= 0x80 && report_id <= 0x86) {
		char lb[40];
		snprintf(lb, sizeof(lb), "hap %02X n=%u b=%02X %02X %02X\n",
			 report_id, (unsigned)(len + 1), len > 0 ? body[0] : 0,
			 len > 1 ? body[1] : 0, len > 2 ? body[2] : 0);
		pplog(lb);
	}

	if (report_id >= 0x80 && report_id <= 0x89) {
		uint16_t oh = v->out_handle[report_id - 0x80];
		if (!oh || len == 0)
			return;
		if (len > VALVE_WMAX)
			len = VALVE_WMAX;
		vq_push(v, oh, body, (uint8_t)len);
		return;
	}

	// Feature report [report_id][body], BLE-segmented to the report char 100F6C34
	// (see BLE_SEG_*); most (settings 0x87) fit in a single 0xC0 segment.
	uint8_t full[1 + VALVE_WMAX];
	if (len > sizeof(full) - 1)
		len = (uint16_t)(sizeof(full) - 1);
	full[0] = report_id;
	memcpy(full + 1, body, len);
	uint16_t total = (uint16_t)(len + 1);

	uint16_t off = 0;
	uint8_t seg = 0;
	while (off < total) {
		uint16_t n = (uint16_t)(total - off);
		if (n > BLE_SEG_PAYLOAD)
			n = BLE_SEG_PAYLOAD;
		uint8_t hdr =
			(uint8_t)(BLE_SEG_DATA | (seg & 0x07) |
				  ((off + n) >= total ? BLE_SEG_LAST : 0));
		uint8_t segbuf[1 + BLE_SEG_PAYLOAD];
		segbuf[0] = hdr;
		memcpy(segbuf + 1, full + off, n);
		vq_push(v, v->report_value_handle, segbuf, (uint8_t)(n + 1));
		off = (uint16_t)(off + n);
		seg++;
	}
}
