<script lang="ts">
	import { device } from '$lib/state/device.svelte';
	import Panel from '$lib/components/Panel.svelte';
	import Stat from '$lib/components/Stat.svelte';
	import InfoPopover from '$lib/components/InfoPopover.svelte';

	const status = $derived(device.status);
	const hex = (v: number) => '0x' + v.toString(16).padStart(8, '0');

	// Least-ever free stack on the usbd task. Low means overflow risk; the old
	// panel flagged red under 24 words (~96B) and amber under 48.
	const usbdTone = $derived.by(() => {
		const w = status?.loop.usbdStackWords;
		if (w == null) return 'none' as const;
		return w < 24 ? ('down' as const) : w < 48 ? ('warn' as const) : ('none' as const);
	});
</script>

<div class="space-y-4">
	<Panel title="Global telemetry">
		{#snippet info()}
			<InfoPopover title="Global telemetry">
				Sums over all controllers; the sidebar shows the selected controller's own rates. Polls/s ≈ 250 expected;
				it's capped by the loop period (≈4000 µs needed).
			</InfoPopover>
		{/snippet}
		<div class="grid gap-2 [grid-template-columns:repeat(auto-fit,minmax(150px,1fr))]">
			<Stat label="Delivered (all)" value={status?.link.up ? `${status.rates.delivered} /s` : null} />
			<Stat label="New reports (all)" value={status?.link.up ? `${status.rates.newReports} /s` : null} />
			<Stat label="Polls/s (all)" value={status ? `${status.rates.polls} /s` : null} />
			<Stat label="Relay/s (all)" value={status?.rates.relay != null ? `${status.rates.relay} /s` : null} />
			<Stat
				label="RF fails/s"
				value={status ? `${status.rates.crc} · ${status.rates.norx} · ${status.rates.heal}` : null}
				hint="crc · noRx · heal"
			/>
			<Stat
				label="Clock LF / HF"
				value={status?.clock ? `${status.clock.lf} / ${status.clock.hf}` : null}
				tone={status?.clock && (status.clock.lfBad || status.clock.hfBad) ? 'down' : 'none'}
			/>
			<Stat
				label="µs per ms"
				value={status?.clock?.usPerMs || null}
				tone={status?.clock?.usPerMsBad ? 'down' : 'none'}
				hint="ideal 1000"
			/>
			<Stat label="usbd stack free" value={status?.loop.usbdStackWords} tone={usbdTone} hint="words" />
			<Stat label="Loop period" value={status?.loop.periodUs ? `${status.loop.periodUs} µs` : null} />
			<Stat
				label="Slowest stage"
				value={status?.loop.periodUs ? `${status.loop.worstStage} ${status.loop.worstUs}µs` : null}
			/>
			<Stat
				label="Poll period act/want"
				value={status?.link.up ? `${status.loop.pollUs} / ${status.loop.pollIntendedUs} µs` : null}
			/>
			<Stat label="Ring faults" value={status?.rates.ringFault || null} tone={status?.rates.ringFault ? 'down' : 'none'} />
		</div>
	</Panel>

	<Panel title="Reset &amp; fault history">
		{#snippet info()}
			<InfoPopover title="Reset detail">
				<em>watchdog (hang)</em>/<em>CPU lockup</em>/<em>HARDFAULT</em> = a fault worth reporting (issue #72);
				<em>reboot</em> = an intentional reset (mode change / config); <em>power-on</em> and <em>pin/replug</em> = normal
				plug-in.
			</InfoPopover>
		{/snippet}
		{#if status?.reset}
			<div class="grid gap-2 [grid-template-columns:repeat(auto-fit,minmax(150px,1fr))]">
				<Stat label="Last reset" value={status.reset.name} tone={status.reset.isFault ? 'down' : 'none'} />
				<Stat label="Hung in" value={status.reset.hangStageName} />
				<Stat label="Hang PC" value={status.reset.hangPC ? hex(status.reset.hangPC) : null} mono />
				<Stat label="Hang LR" value={status.reset.hangLR ? hex(status.reset.hangLR) : null} mono />
			</div>
			<p class="text-app-muted mt-2 font-mono text-xs">RESETREAS={hex(status.reset.raw)}</p>
		{:else}
			<p class="text-app-muted text-sm">Not reported by this firmware.</p>
		{/if}
	</Panel>

	<Panel title="IMU (raw)">
		{#snippet info()}
			<InfoPopover title="IMU (raw)">
				Raw SC2 accel (before scaling). This controller is a ±2 g sensor (~16384 = 1 g); the Switch report divides
				it by 4 to present the genuine ±8 g (4096 = 1 g) scale so the console's gravity-correction engages and the
				gyro stops drifting.
			</InfoPopover>
		{/snippet}
		{#if status?.imu}
			<p class="font-mono text-sm">
				a=({status.imu.ax}, {status.imu.ay}, {status.imu.az}) |a|={status.imu.magnitude}
			</p>
		{:else}
			<p class="text-app-muted text-sm">—</p>
		{/if}
	</Panel>
</div>
