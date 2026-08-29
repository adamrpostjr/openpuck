import { parseBlob, type DeviceStatus } from '$lib/protocol/blob';
import { buildBlob } from '$lib/protocol/fixtures';

export type ConnState = 'disconnected' | 'connecting' | 'connected' | 'lost';

class DeviceState {
	conn = $state<ConnState>('disconnected');
	status = $state<DeviceStatus | null>(null);
	/** True for a ReversePuck controller dongle (28DE:1302), which shows a different surface. */
	isDongle = $state(false);
	error = $state<string | null>(null);

	get connected() {
		return this.conn === 'connected';
	}

	get caps() {
		return this.status?.caps ?? null;
	}

	/** The active controller slot the per-slot readouts follow. */
	activeSlot = $state(0);

	get bondedSlots(): number[] {
		const s = this.status;
		if (!s) return [];
		return s.slots.map((slot, i) => (slot ? i : -1)).filter((i) => i >= 0);
	}

	/**
	 * Development stand-in until the WebUSB transport lands, so the layout can
	 * be built and reviewed against realistic values rather than placeholders.
	 */
	loadFixture() {
		this.status = parseBlob(buildBlob());
		this.conn = 'connected';
	}
}

export const device = new DeviceState();
export const supported = typeof navigator !== 'undefined' && 'usb' in navigator;
