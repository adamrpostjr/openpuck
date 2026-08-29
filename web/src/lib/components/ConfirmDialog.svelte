<script lang="ts">
	import TriangleAlertIcon from '@lucide/svelte/icons/triangle-alert';
	import type { Snippet } from 'svelte';

	export interface ConfirmSpec {
		title: string;
		/** Paragraphs of explanation; each renders on its own line. */
		body: string[];
		/** Bulleted consequences, for the destructive actions. */
		bullets?: string[];
		confirmLabel: string;
		danger?: boolean;
		/**
		 * When set, the confirm button stays disabled until this exact word is
		 * typed. The original used a prompt() for this; a modal keeps the browser
		 * responsive, which matters because a native dialog blocks WebUSB.
		 */
		typeToConfirm?: string;
		onConfirm: () => void;
	}

	interface Props {
		spec: ConfirmSpec | null;
		onCancel: () => void;
		children?: Snippet;
	}
	let { spec, onCancel }: Props = $props();

	let typed = $state('');
	// Reset the box whenever a different action opens the dialog.
	$effect(() => {
		if (spec) typed = '';
	});

	const ready = $derived(!spec?.typeToConfirm || typed === spec.typeToConfirm);
</script>

{#if spec}
	<div
		class="fixed inset-0 z-100 flex items-center justify-center bg-black/70 p-4"
		role="dialog"
		aria-modal="true"
		aria-label={spec.title}
	>
		<div class="bg-app-card border-app-line rounded-container w-[min(520px,94vw)] border p-5 shadow-2xl">
			<h3 class="mb-2 flex items-center gap-2 text-base font-semibold" class:text-error-700-300={spec.danger}>
				{#if spec.danger}<TriangleAlertIcon size={17} />{/if}
				{spec.title}
			</h3>

			{#each spec.body as line (line)}
				<p class="text-app-muted mt-2 text-sm">{line}</p>
			{/each}

			{#if spec.bullets?.length}
				<ul class="text-app-muted mt-2 list-disc space-y-0.5 pl-5 text-sm">
					{#each spec.bullets as b (b)}
						<li>{b}</li>
					{/each}
				</ul>
			{/if}

			{#if spec.typeToConfirm}
				<label class="mt-4 block">
					<span class="text-app-muted text-xs">
						Type <strong class="text-error-700-300 font-mono">{spec.typeToConfirm}</strong> to confirm:
					</span>
					<input
						bind:value={typed}
						class="input bg-app-well border-app-line mt-1 w-full border px-2 py-1 font-mono text-sm"
						autocomplete="off"
						spellcheck="false"
					/>
				</label>
			{/if}

			<div class="mt-5 flex justify-end gap-2">
				<button type="button" class="btn preset-tonal-surface btn-sm" onclick={onCancel}>Cancel</button>
				<button
					type="button"
					class="btn btn-sm {spec.danger ? 'preset-filled-error-500' : 'preset-filled-primary-500'}"
					disabled={!ready}
					onclick={() => {
						spec.onConfirm();
						onCancel();
					}}
				>
					{spec.confirmLabel}
				</button>
			</div>
		</div>
	</div>
{/if}
