<script lang="ts">
	import PlusIcon from '@lucide/svelte/icons/plus';
	import RotateCcwIcon from '@lucide/svelte/icons/rotate-ccw';
	import SaveIcon from '@lucide/svelte/icons/save';
	import Trash2Icon from '@lucide/svelte/icons/trash-2';
	import { device } from '$lib/state/device.svelte';
	import {
		defaultPayload,
		isAnalog,
		LZ_AXIS_SRC,
		LZ_BTNS,
		LZ_CONSUMER,
		LZ_GYRO_ACT,
		LZ_KEYS,
		LZ_MAX,
		LZ_MBTNS,
		LZ_MODS,
		LZ_OUT_LABELS,
		LZO,
	} from '$lib/protocol/lizard';
	import Panel from '$lib/components/Panel.svelte';
	import InfoPopover from '$lib/components/InfoPopover.svelte';
	import GateBadge from '$lib/components/GateBadge.svelte';

	const status = $derived(device.status);
	const bindings = $derived(device.lizard);
	const capable = $derived(device.lizardCapable);

	const TRIG_OPTS: [number, string][] = [[0, '— pick input —'], ...LZ_BTNS];
	const HOLD_OPTS: [number, string][] = [[0, '(none)'], ...LZ_BTNS];

	let confirmReset = $state(false);

	function addBinding() {
		if (bindings.length >= LZ_MAX) return;
		device.lizard.push({ outType: LZO.KBD, od: defaultPayload(LZO.KBD), trig: 0, hold: 0 });
	}

	function setOutType(i: number, v: number) {
		device.lizard[i].outType = v;
		device.lizard[i].od = defaultPayload(v);
	}

	function setMod(i: number, bit: number, on: boolean) {
		const od = device.lizard[i].od;
		od[0] = on ? od[0] | bit : od[0] & ~bit;
	}
</script>

<Panel title="Lizard / desktop mapping">
	{#snippet info()}
		<InfoPopover title="Lizard / desktop mapping">
			Keyboard, mouse &amp; media bindings for <strong>Lizard (always)</strong> mode — each row maps a controller
			input to a desktop action. (Steam-mode seamless lizard, when Steam is closed, keeps the built-in default
			behavior and is not affected by this map.) Edits apply after <strong>Save to device</strong>.
		</InfoPopover>
	{/snippet}

	{#snippet actions()}
		<GateBadge ok={capable} requires="v16" />
	{/snippet}

	{#if !status}
		<p class="text-app-muted text-sm">Connect a puck to edit desktop bindings.</p>
	{:else if !capable}
		<p class="text-app-muted text-sm">
			This puck's firmware predates the desktop map (needs status protocol v16). Update it to configure bindings.
		</p>
	{:else}
		<div class="mb-3 flex flex-wrap items-center gap-2">
			<button
				type="button"
				class="btn preset-tonal-surface btn-sm flex items-center gap-1.5"
				disabled={bindings.length >= LZ_MAX}
				onclick={addBinding}
			>
				<PlusIcon size={14} /> Add binding
			</button>
			<button
				type="button"
				class="btn preset-filled-primary-500 btn-sm flex items-center gap-1.5"
				disabled={device.busy.lizard}
				onclick={() => device.saveLizard()}
			>
				<SaveIcon size={14} /> Save to device
			</button>
			<button type="button" class="btn preset-tonal-surface btn-sm" onclick={() => device.loadLizard()}>
				Reload
			</button>
			<span class="text-app-muted ml-auto text-xs">{bindings.length} / {LZ_MAX} bindings</span>
			{#if confirmReset}
				<span class="text-app-muted text-xs">Reset to built-in defaults? Unsaved edits are lost.</span>
				<button
					type="button"
					class="btn preset-filled-error-500 btn-sm"
					onclick={() => {
						confirmReset = false;
						device.resetLizard();
					}}
				>
					Reset
				</button>
				<button type="button" class="btn preset-tonal-surface btn-sm" onclick={() => (confirmReset = false)}>
					Cancel
				</button>
			{:else}
				<button
					type="button"
					class="btn preset-tonal-surface btn-sm flex items-center gap-1.5"
					onclick={() => (confirmReset = true)}
				>
					<RotateCcwIcon size={14} /> Reset to defaults
				</button>
			{/if}
		</div>

		{#if bindings.length === 0}
			<p class="text-app-muted text-sm">No bindings. Add one to map a controller input to a desktop action.</p>
		{/if}

		<div class="space-y-2">
			{#each bindings as b, i (i)}
				{@const analog = isAnalog(b)}
				<div class="bg-app-well border-app-line rounded-base flex flex-wrap items-center gap-2 border p-2">
					{#if analog}
						<span class="text-app-muted shrink-0 text-xs">Analog source</span>
					{:else}
						<span class="text-app-muted shrink-0 text-xs">When</span>
						<select class="select w-52 text-sm" value={b.trig} onchange={(e) => (b.trig = +e.currentTarget.value >>> 0)}>
							{#each TRIG_OPTS as [v, l] (v)}
								<option value={v}>{l}</option>
							{/each}
						</select>
						<span class="text-app-muted shrink-0 text-xs">+ hold</span>
						<select class="select w-44 text-sm" value={b.hold} onchange={(e) => (b.hold = +e.currentTarget.value >>> 0)}>
							{#each HOLD_OPTS as [v, l] (v)}
								<option value={v}>{l}</option>
							{/each}
						</select>
					{/if}

					<span class="text-app-faint shrink-0">→</span>

					<select class="select w-40 text-sm" value={b.outType} onchange={(e) => setOutType(i, +e.currentTarget.value)}>
						{#each LZ_OUT_LABELS as [v, l] (v)}
							<option value={v}>{l}</option>
						{/each}
					</select>

					{#if b.outType === LZO.KBD}
						{#each LZ_MODS as [bit, name] (bit)}
							<label class="flex shrink-0 items-center gap-1 text-xs">
								<input
									type="checkbox"
									class="checkbox"
									checked={!!(b.od[0] & bit)}
									onchange={(e) => setMod(i, bit, e.currentTarget.checked)}
								/>
								{name}
							</label>
						{/each}
						<select
							class="select w-36 text-sm"
							value={b.od[1] ?? 0}
							onchange={(e) => {
								b.od[1] = +e.currentTarget.value;
								for (let k = 2; k < 7; k++) b.od[k] = 0;
							}}
						>
							{#each LZ_KEYS as [v, l] (v)}
								<option value={v}>{l}</option>
							{/each}
						</select>
					{:else if b.outType === LZO.MBTN}
						<select class="select w-36 text-sm" value={b.od[0] || 1} onchange={(e) => (b.od[0] = +e.currentTarget.value)}>
							{#each LZ_MBTNS as [v, l] (v)}
								<option value={v}>{l}</option>
							{/each}
						</select>
					{:else if b.outType === LZO.AXIS}
						<select class="select w-40 text-sm" value={b.od[0] || 0} onchange={(e) => (b.od[0] = +e.currentTarget.value)}>
							{#each LZ_AXIS_SRC as [v, l] (v)}
								<option value={v}>{l}</option>
							{/each}
						</select>
						{#if (b.od[0] || 0) === 2}
							<span class="text-app-muted shrink-0 text-xs">when</span>
							<select
								class="select w-52 text-sm"
								value={b.od[1] || 0}
								onchange={(e) => (b.od[1] = +e.currentTarget.value)}
							>
								{#each LZ_GYRO_ACT as [v, l] (v)}
									<option value={v}>{l}</option>
								{/each}
							</select>
						{/if}
					{:else if b.outType === LZO.SCROLL}
						<span class="text-app-muted text-xs">Left trackpad → scroll</span>
					{:else if b.outType === LZO.CONSUMER}
						<select class="select w-36 text-sm" value={b.od[0] || 1} onchange={(e) => (b.od[0] = +e.currentTarget.value)}>
							{#each LZ_CONSUMER as [v, l] (v)}
								<option value={v}>{l}</option>
							{/each}
						</select>
					{/if}

					<button
						type="button"
						class="text-app-muted hover:text-error-700-300 ml-auto shrink-0 px-2"
						title="Delete binding"
						aria-label="Delete binding"
						onclick={() => device.lizard.splice(i, 1)}
					>
						<Trash2Icon size={15} />
					</button>
				</div>
			{/each}
		</div>
	{/if}
</Panel>
