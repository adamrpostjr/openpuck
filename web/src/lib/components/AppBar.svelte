<script lang="ts">
	import CheckIcon from '@lucide/svelte/icons/check';
	import ChevronDownIcon from '@lucide/svelte/icons/chevron-down';
	import MoonIcon from '@lucide/svelte/icons/moon';
	import PanelsTopLeftIcon from '@lucide/svelte/icons/panels-top-left';
	import PlugZapIcon from '@lucide/svelte/icons/plug-zap';
	import SunIcon from '@lucide/svelte/icons/sun';
	import UsbIcon from '@lucide/svelte/icons/usb';
	import { Menu, Portal } from '@skeletonlabs/skeleton-svelte';
	import { device } from '$lib/state/device.svelte';
	import { ui } from '$lib/state/ui.svelte';
	import { panels, type PanelId } from '$lib/state/panels.svelte';
	import { theme } from '$lib/state/theme.svelte';

	const status = $derived(device.status);

	const CONN_LABEL: Record<string, string> = {
		disconnected: 'disconnected',
		connecting: 'connecting…',
		connected: 'connected',
		lost: 'device lost',
	};

	// Everything that reboots, erases, or re-enumerates the puck. These sat as
	// six always-clickable buttons in the old top bar, Factory erase and Wipe
	// board included, right next to Connect.
	const DEVICE_ACTIONS = [
		{ id: 'debugCdc', label: 'Debug CDC', danger: false },
		{ id: 'dfuSerial', label: 'Serial DFU', danger: false },
		{ id: 'dfuUf2', label: 'UF2 DFU', danger: false },
		{ id: 'factoryErase', label: 'Factory erase', danger: true },
		{ id: 'wipeBoard', label: 'Wipe board', danger: true, debugOnly: true },
	];

	const PANEL_LABELS: Record<PanelId, string> = { logs: 'Logs', capture: 'Capture', trail: 'Loop trail' };
</script>

<header class="border-app-line bg-app-chrome flex shrink-0 items-center gap-3 border-b px-4 py-2">
	<span class="font-bold tracking-tight">OpenPuck</span>

	<span
		class="rounded-full px-2 py-0.5 text-xs font-semibold
		{device.connected ? 'bg-success-100-900 text-success-700-300' : 'bg-error-100-900 text-error-700-300'}"
	>
		{CONN_LABEL[device.conn]}
	</span>

	{#if status}
		<span class="text-app-strong rounded-base bg-app-well border-app-line border px-2 py-0.5 text-xs">
			{status.modeName}
		</span>
		<span class="text-app-muted text-xs">fw {status.build.id || '—'}</span>
	{/if}

	<div class="flex-1"></div>

	<Menu>
		<Menu.Trigger class="btn preset-tonal-surface flex items-center gap-1.5 text-sm">
			<PanelsTopLeftIcon size={15} />
			Panels
			<ChevronDownIcon size={13} />
		</Menu.Trigger>
		<Portal>
			<Menu.Positioner class="z-50">
				<Menu.Content class="bg-app-card border-app-line rounded-container min-w-44 border p-1 shadow-xl">
					{#each Object.entries(PANEL_LABELS) as [id, label] (id)}
						<Menu.Item
							value={id}
							onclick={() => panels.toggle(id as PanelId)}
							class="hover:bg-app-hover rounded-base flex cursor-pointer items-center gap-2 px-2.5 py-1.5 text-sm"
						>
							<span class="text-primary-700-300 flex w-3.5 justify-center">
								{#if panels.panels[id as PanelId].open}<CheckIcon size={14} />{/if}
							</span>
							{label}
						</Menu.Item>
					{/each}
				</Menu.Content>
			</Menu.Positioner>
		</Portal>
	</Menu>

	<button
		type="button"
		onclick={() => theme.toggle()}
		class="btn preset-tonal-surface text-sm"
		aria-label="Toggle colour scheme"
		title="Toggle light / dark"
	>
		{#if theme.mode === 'dark'}
			<MoonIcon size={15} />
		{:else}
			<SunIcon size={15} />
		{/if}
	</button>

	<button
		type="button"
		onclick={() => (ui.beta = !ui.beta)}
		class="btn text-sm {ui.beta ? 'preset-filled-warning-500' : 'preset-tonal-warning'}"
	>
		Beta {ui.beta ? 'on' : ''}
	</button>

	<Menu>
		<Menu.Trigger class="btn preset-tonal-surface flex items-center gap-1.5 text-sm">
			<UsbIcon size={15} />
			Device
			<ChevronDownIcon size={13} />
		</Menu.Trigger>
		<Portal>
			<Menu.Positioner class="z-50">
				<Menu.Content class="bg-app-card border-app-line rounded-container min-w-52 border p-1 shadow-xl">
					{#each DEVICE_ACTIONS as a (a.id)}
						{#if !a.debugOnly || ui.debug}
							<Menu.Item
								value={a.id}
								class="rounded-base cursor-pointer px-2.5 py-1.5 text-sm
									{a.danger ? 'text-error-700-300 hover:bg-error-900/40' : 'hover:bg-app-hover'}"
							>
								{a.label}
							</Menu.Item>
						{/if}
					{/each}
				</Menu.Content>
			</Menu.Positioner>
		</Portal>
	</Menu>

	<button type="button" class="btn preset-filled-primary-500 flex items-center gap-1.5 text-sm">
		<PlugZapIcon size={15} />
		{device.connected ? 'Reconnect' : 'Connect'}
	</button>
</header>
