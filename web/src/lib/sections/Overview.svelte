<script lang="ts">
	import { device } from '$lib/state/device.svelte';
	import Panel from '$lib/components/Panel.svelte';
	import Stat from '$lib/components/Stat.svelte';
	import InfoPopover from '$lib/components/InfoPopover.svelte';

	const status = $derived(device.status);
</script>

<div class="grid gap-4 [grid-template-columns:repeat(auto-fit,minmax(340px,1fr))]">
	<Panel title="Version">
		{#snippet info()}
			<InfoPopover title="Version">
				This section reflects the connected puck. Reconnect after flashing or changing modes to verify what is
				currently running.
			</InfoPopover>
		{/snippet}
		<div class="grid grid-cols-3 gap-2">
			<Stat label="Firmware build" value={status?.build.id} />
			<Stat label="Status protocol" value={status ? `v${status.protocol}` : null} />
			<Stat label="Git tree" value={status ? (status.build.dirty ? 'dirty' : 'clean') : null} tone={status ? (status.build.dirty ? 'warn' : 'up') : 'none'} />
		</div>
	</Panel>

	<Panel title="Backup &amp; clone">
		{#snippet info()}
			<InfoPopover title="Backup &amp; clone">
				Save this puck's controller pairings <em>and</em> every setting to a file, then restore them onto another puck.
				The other puck becomes a clone — any controller paired to this one connects to it with no re-pairing. Import
				overwrites all pairings and settings on the connected puck, then reboots it.
			</InfoPopover>
		{/snippet}
		<div class="flex flex-wrap gap-2">
			<button type="button" class="btn preset-filled-primary-500 btn-sm">Export to file…</button>
			<button type="button" class="btn preset-tonal-surface btn-sm">Import from file…</button>
		</div>
		<p class="text-app-muted mt-2 text-xs">
			Tip: pair every controller to one puck, export once, then import onto each spare.
		</p>
	</Panel>
</div>
