#include "mode_switch_hori.h"
#include "triton.h"
#include "gamepad_util.h"
#include "src/common/report_build.h"
#include "config.h"
#include "bonds.h"
#include "usb_mount.h"
#include "usb_tx.h"
#include <Adafruit_TinyUSB.h>
#include <Arduino.h>
#include "src/common/hid_reports.h"

SwitchHoriController g_switchHori;

// 8-byte input report: [btn_lo][btn_hi][hat][LX][LY][RX][RY][vendor], sticks uint8 center 0x80.
// NSLOT HORIPAD HID instances -- one per bond slot. The Switch console binds each as a separate gamepad.
static Adafruit_USBD_HID g_switch[NSLOT];
static unsigned long g_swLastMs[NSLOT] = { 0 };

// back-paddle / QAM code (g_back[], g_qamMap) -> Switch button bit. 0=none 1=A 2=B 3=X 4=Y 5=LB 6=RB 7=L3 8=R3 9=Back(Minus) 10=Start(Plus) 11=Guide(Home) 18=Capture
// Back-paddle code 12..15 map to D-pad Up/Down/Left/Right; fold them into the hat direction flags.
// HORIPAD/Switch button bits: Y=1 B=2 A=4 X=8 L=10 R=20 ZL=40 ZR=80 Minus=100 Plus=200 LClick=400 RClick=800 Home=1000 Capture=2000
// Per-slot: each controller's decoded input is in g_in[slot].
static void switchBuildHoripad(uint8_t slot, uint8_t out[8])
{
	// Report layout is in the shared builder (src/common/report_build.c).
	report_cfg_t cfg = { g_abSwap != 0,
			     { g_back[0], g_back[1], g_back[2], g_back[3] },
			     g_qamMap };
	build_switch_hori(&g_in[slot], &cfg, out);
}

// Dynamic-mount mode: begin() is unused (setup() calls beginPool()+usbReenumerate instead).
void SwitchHoriController::begin()
{
}
// Wake mouse (1 HID) is present in Switch mode, leaving CFG_TUD_HID-1 for the HORIPAD pool.
uint8_t SwitchHoriController::maxSlots() const
{
	uint8_t cap = (uint8_t)(CFG_TUD_HID - 1);
	return cap < NSLOT ? cap : (uint8_t)NSLOT;
}
void SwitchHoriController::usbIdentity()
{
	USBDevice.setID(0x0F0D, 0x0092);
	USBDevice.setDeviceVersion(0x0210);
	USBDevice.setManufacturerDescriptor("HORI CO.,LTD.");
	USBDevice.setProductDescriptor("POKKEN CONTROLLER");
}
void SwitchHoriController::beginPool()
{
	uint8_t pool = maxSlots();
	for (uint8_t s = 0; s < pool; s++) {
		g_switch[s].enableOutEndpoint(true);
		g_switch[s].setReportDescriptor(SWITCH_HID_DESC,
						sizeof SWITCH_HID_DESC);
		g_switch[s].setPollInterval(1);
		g_switch[s].begin();
	}
}
void SwitchHoriController::mountSlots(uint8_t k)
{
	for (uint8_t u = 0; u < k; u++)
		USBDevice.addInterface(g_switch[u]);
}
void SwitchHoriController::task()
{
	// stream the 8-byte HORIPAD report at ~250Hz per CONNECTED controller. usbSlot u -> bond slot for input;
	// switchBuildHoripad reads g_in[bond] (no per-USB-slot state in the HORIPAD report).
	for (uint8_t u = 0; u < g_usbMountCount; u++) {
		if (!g_switch[u].ready())
			continue;
		if (millis() - g_swLastMs[u] < USB_STREAM_MS)
			continue;
		int bond = g_usbToBond[u];
		if (bond < 0)
			continue;
		g_swLastMs[u] = millis();
		uint8_t p[8];
		switchBuildHoripad((uint8_t)bond, p);
		usbTxHid(&g_switch[u], 0, p,
			 sizeof p); // report-id-less descriptor
	}
}
