<script lang="ts">
	import Trash2Icon from '@lucide/svelte/icons/trash-2';
	import { trail } from '$lib/state/trail.svelte';
	import StreamPanel from './StreamPanel.svelte';
</script>

<StreamPanel id="trail" title="Loop trail" badge={`${trail.entries.length}`}>
	{#snippet toolbar()}
		<span class="text-app-muted text-xs">
			Every loop-state change, kept in this browser — survives refreshes and reboots.
		</span>
		<button
			type="button"
			class="text-app-muted hover:text-error-700-300 ml-auto px-1"
			title="Clear trail"
			aria-label="Clear trail"
			onclick={() => trail.clear()}
		>
			<Trash2Icon size={14} />
		</button>
	{/snippet}

	{#if !trail.entries.length}
		<p class="text-app-muted">No events yet.</p>
	{:else}
		{#each trail.entries as e (e.t + e.msg)}
			<div class="flex gap-2">
				<span class="text-app-faint tabnum shrink-0">{new Date(e.t).toLocaleString()}</span>
				<span class="break-all whitespace-pre-wrap">{e.msg}</span>
			</div>
		{/each}
	{/if}
</StreamPanel>
