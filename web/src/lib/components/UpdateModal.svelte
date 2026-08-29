<script lang="ts">
	import { fwup } from '$lib/state/fwup.svelte';

	const m = $derived(fwup.modal);
</script>

<!--
	Blocking by design: while an image is being staged, a stray click that
	switched modes or wrote a config field would interleave with the transfer on
	the same endpoint. This covers everything until the run ends.
-->
{#if m.open}
	<!--
		No Escape handler while an update is running: dismissing the only surface
		reporting a flash in progress would leave the transfer running unseen.
		It becomes closable once the run finishes.
	-->
	<div
		class="fixed inset-0 z-100 flex items-center justify-center bg-black/70 p-4"
		role="alertdialog"
		aria-modal="true"
		aria-live="polite"
		aria-label={m.title}
	>
		<div class="bg-app-card border-app-line rounded-container w-[min(500px,92vw)] border p-6 shadow-2xl">
			<h3 class="mb-1 text-base font-semibold" class:text-error-700-300={m.failed}>{m.title}</h3>
			<p class="mt-2 text-sm">{m.stage}</p>

			<div class="bg-app-well border-app-line mt-3 h-2.5 overflow-hidden rounded-full border">
				<div
					class="h-full transition-[width] duration-200 {m.failed
						? 'bg-error-500'
						: m.pct === null
							? 'bg-primary-500 animate-pulse'
							: 'bg-success-500'}"
					style="width: {m.pct === null ? 100 : m.pct}%"
				></div>
			</div>

			{#if m.pct !== null && !m.failed}
				<p class="text-app-muted tabnum mt-1.5 text-xs">{m.pct}%</p>
			{/if}
			{#if m.detail}
				<p class="text-app-muted mt-2 text-xs">{m.detail}</p>
			{/if}

			{#if m.done}
				<div class="mt-4 flex justify-end">
					<button type="button" class="btn preset-tonal-surface btn-sm" onclick={() => fwup.close()}>Close</button>
				</div>
			{/if}
		</div>
	</div>
{/if}
