import { describe, expect, it } from 'vitest';
import {
	decodeBindings,
	encodeBinding,
	filterSavable,
	LZO,
	LZ_MAX,
	LZ_KEYS,
	btnLabel,
	type LizardBinding,
} from '../lizard';

/** Build a [0xAA][count][count*16] frame the way the firmware sends one. */
function frame(bindings: LizardBinding[]): Uint8Array {
	const buf = new Uint8Array(2 + bindings.length * 16);
	buf[0] = 0xaa;
	buf[1] = bindings.length;
	bindings.forEach((b, i) => {
		const q = 2 + i * 16;
		buf[q] = b.outType;
		for (let k = 0; k < 7; k++) buf[q + 1 + k] = b.od[k] ?? 0;
		for (let k = 0; k < 4; k++) buf[q + 8 + k] = (b.trig >>> (8 * k)) & 0xff;
		for (let k = 0; k < 4; k++) buf[q + 12 + k] = (b.hold >>> (8 * k)) & 0xff;
	});
	return buf;
}

describe('lizard bindings', () => {
	const ctrlC: LizardBinding = { outType: LZO.KBD, od: [0x01, 0x06, 0, 0, 0, 0, 0], trig: 0x1, hold: 0 };
	const gyroLook: LizardBinding = { outType: LZO.AXIS, od: [2, 1, 0, 0, 0, 0, 0], trig: 0, hold: 0 };

	it('round-trips a binding through encode and decode', () => {
		const decoded = decodeBindings(frame([ctrlC]), 1);
		expect(decoded[0]).toEqual(ctrlC);
	});

	it('round-trips the high trigger bit without sign corruption', () => {
		// L-stick up is 0x80000000; a signed shift would decode it negative.
		const b: LizardBinding = { outType: LZO.MBTN, od: [1, 0, 0, 0, 0, 0, 0], trig: 0x80000000, hold: 0x40000000 };
		const decoded = decodeBindings(frame([b]), 1);
		expect(decoded[0].trig).toBe(0x80000000);
		expect(decoded[0].hold).toBe(0x40000000);
	});

	it('decodes a full 32-binding map', () => {
		const many = Array.from({ length: LZ_MAX }, (_, i) => ({ ...ctrlC, trig: 1 << i % 31 }));
		expect(decodeBindings(frame(many), LZ_MAX)).toHaveLength(LZ_MAX);
	});

	it('pads a short payload to the 7 bytes the wire format expects', () => {
		const enc = encodeBinding(0, { outType: LZO.MBTN, od: [1], trig: 0x2, hold: 0 });
		expect(enc).toHaveLength(18); // op + index + outType + 7 od + 4 trig + 4 hold
		expect(enc.slice(3, 10)).toEqual([1, 0, 0, 0, 0, 0, 0]);
	});

	it('drops a digital binding with no trigger and no hold', () => {
		// Without a trigger the firmware skips its guard and the binding fires
		// forever -- it would type or click without stopping.
		const orphan: LizardBinding = { outType: LZO.KBD, od: [0, 0x04, 0, 0, 0, 0, 0], trig: 0, hold: 0 };
		expect(filterSavable([ctrlC, orphan])).toEqual([ctrlC]);
	});

	it('keeps an analog binding that has no trigger', () => {
		// Analog outputs are driven by a source, so a missing trigger is normal.
		expect(filterSavable([gyroLook])).toEqual([gyroLook]);
	});

	it('drops disabled rows', () => {
		const none: LizardBinding = { outType: LZO.NONE, od: [0, 0, 0, 0, 0, 0, 0], trig: 0x4, hold: 0 };
		expect(filterSavable([none])).toEqual([]);
	});

	it('keeps a hold-only digital binding', () => {
		const holdOnly: LizardBinding = { outType: LZO.MBTN, od: [1, 0, 0, 0, 0, 0, 0], trig: 0, hold: 0x10 };
		expect(filterSavable([holdOnly])).toEqual([holdOnly]);
	});

	it('names known trigger bits and falls back to hex', () => {
		expect(btnLabel(0x1)).toBe('A');
		expect(btnLabel(0x80000000)).toBe('L-stick → up');
		expect(btnLabel(0x123)).toBe('0x123');
		expect(btnLabel(0)).toBe('');
	});

	it('offers a keycode table with no duplicate values', () => {
		const values = LZ_KEYS.map(([v]) => v);
		expect(new Set(values).size).toBe(values.length);
	});
});
