import { parseBlob, STAGE_NAMES, type DeviceStatus } from '$lib/protocol/blob';
import { buildBlob, FIXTURE_LIZARD } from '$lib/protocol/fixtures';
import { Transport, MARK_PAIRED } from '$lib/usb/transport';
import {
	decodeBindings,
	encodeBinding,
	filterSavable,
	LZ_OP,
	type LizardBinding,
} from '$lib/protocol/lizard';
import { logs } from '$lib/state/log.svelte';
import { trail } from '$lib/state/trail.svelte';

export type ConnState = 'disconnected' | 'connecting' | 'connected' | 'lost';

/** Status poll cadence, matching the original panel. */
const POLL_MS = 600;
/**
 * A soft wedge still lets the usbd task answer, so the STALLED pill covers it.
 * The lockup/hardfault class (issue #72) kills the whole MCU: USB goes silent,
 * transferIn just pends, and the panel would sit there showing "running"
 * forever. Only the host can see that, hence this timeout.
 */
const HEARTBEAT_TIMEOUT_MS = 2500;

class DeviceState {
	conn = $state<ConnState>('disconnected');
	status = $state<DeviceStatus | null>(null);
	/** True for a ReversePuck controller dongle (28DE:1302). */
	isDongle = $state(false);
	serial = $state('');
	activeSlot = $state(0);
	/** Set when no status blob has arrived for longer than the timeout. */
	hardWedge = $state<number | null>(null);

	/** Raw last payload, kept for the backup export which replays every field. */
	lastBlob: Uint8Array | null = null;

	private transport: Transport;
	private polling = false;
	private inflight = false;
	private lastBlobTs = 0;
	private hbLostEpisode = false;
	private stallEpisode = false;
	private stallPeak = 0;

	/**
	 * Paths that legitimately take over the endpoint and must suspend the blob
	 * poll: a nested transferIn would steal the other path's reply.
	 */
	busy = $state({ capture: false, backup: false, flight: false, lizard: false });

	constructor() {
		this.transport = new Transport({
			onOpen: ({ isDongle, serial }) => {
				this.isDongle = isDongle;
				this.serial = serial;
				this.conn = 'connected';
				this.hardWedge = null;
				this.lastBlobTs = 0;
				this.hbLostEpisode = false;
				this.lizardLoaded = false;
				void this.startPolling();
			},
			onGone: () => {
				this.polling = false;
				this.conn = 'lost';
				this.status = null;
				this.lastBlobTs = 0;
				this.hbLostEpisode = false;
				this.hardWedge = null;
				logs.warn('device disconnected (mode-switch / watchdog reboot) — auto-reconnecting when it returns');
				trail.add('device disconnected — reset or replug; reason logged on reconnect');
			},
			onWedge: ({ stage, stallMs }) => {
				const name = STAGE_NAMES[stage] ?? `stage ${stage}`;
				trail.add(`WEDGED @ ${name} (${stallMs}ms — live 0xA9 report; watchdog reset imminent)`);
				logs.error(`LOOP WEDGED @ ${name} (stuck ${stallMs}ms) — watchdog reset imminent`);
			},
		});
	}

	get connected() {
		return this.conn === 'connected';
	}

	get caps() {
		return this.status?.caps ?? null;
	}

	get bondedSlots(): number[] {
		const s = this.status;
		if (!s) return [];
		return s.slots.map((slot, i) => (slot ? i : -1)).filter((i) => i >= 0);
	}

	get anyBusy() {
		return this.busy.capture || this.busy.backup || this.busy.flight || this.busy.lizard;
	}

	init() {
		this.transport.listen();
		void this.transport.autoConnect();
		setInterval(() => this.checkHeartbeat(), 1000);
	}

	async connect() {
		this.conn = 'connecting';
		const ok = await this.transport.connect();
		if (!ok && this.conn === 'connecting') this.conn = 'disconnected';
	}

	/** Write one config field, then re-read so the UI reflects what stuck. */
	async setField(field: number, value: number) {
		await this.transport.send([0x02, field, value & 0xff]);
		const p = await this.transport.readBlob();
		if (p) this.applyBlob(p);
	}

	async sendRaw(bytes: number[]) {
		await this.transport.send(bytes);
	}

	private applyBlob(p: Uint8Array) {
		this.lastBlob = p;
		this.lastBlobTs = Date.now();
		this.hardWedge = null;
		const next = parseBlob(p);

		// Log a stall once per episode, then again each time it climbs
		// meaningfully towards the ~8s watchdog, so the record survives the
		// reset that is probably coming.
		if (next.loop.stalled) {
			if (!this.stallEpisode || next.loop.stallMs > this.stallPeak + 800) {
				if (!this.stallEpisode) {
					trail.add(`STALLED @ ${next.loop.stage} (${next.loop.stallMs}ms, soft wedge — usbd still alive)`);
				}
				this.stallEpisode = true;
				this.stallPeak = next.loop.stallMs;
				logs.warn(`LOOP STALL @ ${next.loop.stage} (${next.loop.stallMs}ms) — watchdog reset imminent`);
			}
		} else if (this.stallEpisode) {
			trail.add(`running again — stall recovered without a reset (peaked ${this.stallPeak}ms)`);
			this.stallEpisode = false;
			this.stallPeak = 0;
		}

		// Clamp the selected slot to one that is actually bonded.
		if (!next.slots[this.activeSlot]) {
			const first = next.slots.findIndex((s) => s);
			this.activeSlot = first >= 0 ? first : 0;
		}
		this.status = next;

		// Lazy, capability-gated: only after a blob proves the puck speaks v16+.
		if (!this.lizardLoaded && next.caps.lizard) {
			this.lizardLoaded = true;
			void this.loadLizard();
		}
	}

	private async refresh() {
		if (!this.transport.connected || this.inflight || this.anyBusy) return;
		this.inflight = true;
		// try/finally matters: an exception anywhere in here must never leave
		// inflight latched true, which would kill every future poll silently and
		// read as a fake heartbeat loss.
		try {
			await this.transport.send([0x01]);
			if (this.isDongle) {
				const p = await this.transport.readFrame(MARK_PAIRED, 3, 256);
				if (p) this.lastBlob = p;
			} else {
				const p = await this.transport.readBlob();
				if (p) this.applyBlob(p);
			}
		} catch (e) {
			logs.error(`refresh err: ${(e as Error).message}`);
		} finally {
			this.inflight = false;
		}
	}

	private async startPolling() {
		this.polling = true;
		await this.refresh();
		while (this.polling && this.transport.connected) {
			await new Promise((r) => setTimeout(r, POLL_MS));
			await this.refresh();
		}
	}

	private checkHeartbeat() {
		if (!this.transport.connected || !this.lastBlobTs || this.anyBusy) return;
		const age = Date.now() - this.lastBlobTs;
		if (age > HEARTBEAT_TIMEOUT_MS) {
			const secs = Math.round(age / 1000);
			this.hardWedge = secs;
			if (!this.hbLostEpisode) {
				this.hbLostEpisode = true;
				logs.error(
					`heartbeat lost — no status blob for ${secs}s: hard wedge ` +
						`(USB stack dead too, so the live wedge reporter can't run) — check the flight trail after the reset`,
				);
				trail.add(`HEARTBEAT LOST — no status for ${secs}s (hard wedge: USB silent, whole MCU likely stopped)`);
			}
		} else if (this.hbLostEpisode) {
			this.hbLostEpisode = false;
			this.hardWedge = null;
			trail.add('heartbeat back — status blobs resumed without a reset');
		}
	}

	// ---- lizard map ----
	lizard = $state<LizardBinding[]>([]);
	lizardStatus = $state('');
	/** One-shot per connection: the map is fetched lazily once v16+ is proven. */
	private lizardLoaded = false;

	/**
	 * Every lizard op sends 0x11..0x15 then does a blocking read. Pre-v16
	 * firmware drops the byte and never replies, so the read would hang the
	 * shared endpoint forever and take the status poll with it. Never send one
	 * until a status blob has proven the puck speaks v16.
	 */
	get lizardCapable() {
		return !!this.status?.caps.lizard;
	}

	private async lizardExchange<T>(fn: () => Promise<T>): Promise<T | null> {
		this.busy.lizard = true;
		// Let an in-flight status poll finish first; a nested transferIn would
		// steal its reply.
		for (let i = 0; i < 25 && this.inflight; i++) await new Promise((r) => setTimeout(r, 20));
		try {
			return await fn();
		} catch (e) {
			logs.error(`lizard err: ${(e as Error).message}`);
			return null;
		} finally {
			this.busy.lizard = false;
		}
	}

	private async readLizard(): Promise<LizardBinding[] | null> {
		const frame = await this.transport.readLizardFrame();
		return frame ? decodeBindings(frame.acc, frame.count) : null;
	}

	async loadLizard() {
		if (!this.lizardCapable) return;
		await this.lizardExchange(async () => {
			await this.transport.send([LZ_OP.dump]);
			const b = await this.readLizard();
			if (b) {
				this.lizard = b;
				logs.info(`lizard map loaded — ${b.length} bindings`);
			}
		});
	}

	async saveLizard() {
		if (!this.transport.connected || !this.lizardCapable) return;
		const valid = filterSavable($state.snapshot(this.lizard) as LizardBinding[]);
		const dropped = this.lizard.length - valid.length;
		await this.lizardExchange(async () => {
			await this.transport.send([LZ_OP.beginEdit, valid.length & 0xff]);
			for (let i = 0; i < valid.length; i++) await this.transport.send(encodeBinding(i, valid[i]));
			await this.transport.send([LZ_OP.commit]);
			const b = await this.readLizard();
			if (b) this.lizard = b;
			const skipped = dropped ? ` (skipped ${dropped} incomplete: no trigger/hold)` : '';
			logs.ok(`lizard map saved — ${valid.length} bindings${skipped}`);
		});
	}

	async resetLizard() {
		if (!this.transport.connected || !this.lizardCapable) return;
		await this.lizardExchange(async () => {
			await this.transport.send([LZ_OP.reset]);
			const b = await this.readLizard();
			if (b) this.lizard = b;
			logs.ok('lizard map reset to defaults');
		});
	}

	/** Renders the shell against a recorded blob, for layout work without hardware. */
	loadFixture() {
		this.applyBlob(buildBlob());
		this.conn = 'connected';
		this.lizardLoaded = true; // no device to dump from
		this.lizard = structuredClone(FIXTURE_LIZARD);
	}
}

export const device = new DeviceState();
export const supported = typeof navigator !== 'undefined' && 'usb' in navigator;
