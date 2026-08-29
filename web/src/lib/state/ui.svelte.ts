// Which section the rail is showing, plus the two visibility tiers the old
// panel drove with .debugonly / .logonly classes and ?debug= / ?beta= flags.
// Kept as the same URL flags so existing links and muscle memory still work.

import ActivityIcon from '@lucide/svelte/icons/activity';
import Gamepad2Icon from '@lucide/svelte/icons/gamepad-2';
import GaugeIcon from '@lucide/svelte/icons/gauge';
import HardDriveDownloadIcon from '@lucide/svelte/icons/hard-drive-download';
import KeyboardIcon from '@lucide/svelte/icons/keyboard';
import VibrateIcon from '@lucide/svelte/icons/vibrate';
import WaypointsIcon from '@lucide/svelte/icons/waypoints';
import type { Component } from 'svelte';

export type SectionId = 'overview' | 'modes' | 'mapping' | 'feel' | 'desktop' | 'firmware' | 'diagnostics';

export interface SectionDef {
	id: SectionId;
	label: string;
	icon: Component;
	/** Only reachable once the Beta warning has been accepted. */
	beta?: boolean;
}

export const SECTIONS: SectionDef[] = [
	{ id: 'overview', label: 'Overview', icon: GaugeIcon },
	{ id: 'modes', label: 'Modes', icon: Gamepad2Icon },
	{ id: 'mapping', label: 'Mapping', icon: WaypointsIcon },
	{ id: 'feel', label: 'Feel', icon: VibrateIcon },
	{ id: 'desktop', label: 'Desktop', icon: KeyboardIcon },
	{ id: 'firmware', label: 'Firmware', icon: HardDriveDownloadIcon, beta: true },
	{ id: 'diagnostics', label: 'Diagnostics', icon: ActivityIcon },
];

function readFlag(name: string): boolean {
	if (typeof location === 'undefined') return false;
	return new URLSearchParams(location.search).get(name) === 'true';
}

class UiState {
	section = $state<SectionId>('overview');
	/** ?debug=true -- reveals what the old panel marked .debugonly. */
	debug = $state(readFlag('debug'));
	/** ?beta=true or the Beta button -- unlocks firmware flashing. */
	beta = $state(readFlag('beta'));
	/** Collapses the rail to icons; also forced by the <1280px breakpoint. */
	railCollapsed = $state(false);
	monitorOpen = $state(true);

	go(id: SectionId) {
		this.section = id;
	}
}

export const ui = new UiState();
