<script lang="ts">
	import AppBar from '$lib/components/AppBar.svelte';
	import Rail from '$lib/components/Rail.svelte';
	import MonitorSidebar from '$lib/components/MonitorSidebar.svelte';
	import LogPanel from '$lib/panels/LogPanel.svelte';
	import UpdateModal from '$lib/components/UpdateModal.svelte';
	import Overview from '$lib/sections/Overview.svelte';
	import Modes from '$lib/sections/Modes.svelte';
	import Mapping from '$lib/sections/Mapping.svelte';
	import Feel from '$lib/sections/Feel.svelte';
	import Desktop from '$lib/sections/Desktop.svelte';
	import Firmware from '$lib/sections/Firmware.svelte';
	import Diagnostics from '$lib/sections/Diagnostics.svelte';
	import { ui, type SectionId } from '$lib/state/ui.svelte';
	import { device, supported } from '$lib/state/device.svelte';
	import { logs } from '$lib/state/log.svelte';

	const SECTION_COMPONENTS = {
		overview: Overview,
		modes: Modes,
		mapping: Mapping,
		feel: Feel,
		desktop: Desktop,
		firmware: Firmware,
		diagnostics: Diagnostics,
	} satisfies Record<SectionId, unknown>;

	const Current = $derived(SECTION_COMPONENTS[ui.section]);

	// One-shot bootstrap. In an $effect this would read and write the same state
	// and loop forever.
	if (new URLSearchParams(location.search).get('fixture') === 'true') {
		device.loadFixture();
		logs.ok('loaded v17 fixture (no device attached)');
	} else if (supported) {
		device.init();
	}
</script>

<div class="bg-app-bg text-app-strong flex h-screen flex-col overflow-hidden">
	<AppBar />

	{#if !supported}
		<div class="flex flex-1 items-center justify-center p-8">
			<div class="max-w-md text-center">
				<h1 class="mb-2 text-lg font-semibold">This browser can't talk to the puck</h1>
				<p class="text-app-muted text-sm">
					The panel needs WebUSB, which is available in Chrome and Edge. Firefox and Safari do not implement it.
				</p>
			</div>
		</div>
	{:else if !device.connected}
		<div class="flex flex-1 items-center justify-center p-8">
			<div class="max-w-lg text-center">
				<h1 class="mb-2 text-lg font-semibold">Connect your puck</h1>
				<p class="text-app-muted mb-4 text-sm">
					First connect uses the Chrome/Edge device picker; reconnects are automatic. If the picker is empty, quit
					any app holding the device or replug.
				</p>
				<button type="button" class="btn preset-filled-primary-500" onclick={() => device.connect()}>
					{device.conn === 'connecting' ? 'Connecting…' : 'Connect'}
				</button>
				<p class="text-app-muted mt-4 text-xs">
					On Linux, a puck that stays "disconnected" after the picker is usually a udev permissions issue — see
					<a class="text-primary-700-300 hover:underline" href="./WEBUSB_LINUX.md">WEBUSB_LINUX.md</a>.
				</p>
			</div>
		</div>
	{:else}
		<div class="flex min-h-0 flex-1">
			<Rail />
			<main class="min-w-0 flex-1 overflow-y-auto p-4">
				<div class="mx-auto max-w-[1600px]">
					<Current />
				</div>
			</main>
			{#if ui.monitorOpen}
				<MonitorSidebar />
			{/if}
		</div>
	{/if}

	<LogPanel />
	<UpdateModal />
</div>
