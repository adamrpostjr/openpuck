<script lang="ts">
	import ChevronsLeftIcon from '@lucide/svelte/icons/chevrons-left';
	import ChevronsRightIcon from '@lucide/svelte/icons/chevrons-right';
	import { SECTIONS, ui } from '$lib/state/ui.svelte';
</script>

<nav
	class="border-app-line bg-app-chrome flex shrink-0 flex-col gap-1 border-r p-2 transition-[width]"
	class:w-44={!ui.railCollapsed}
	class:w-14={ui.railCollapsed}
	aria-label="Sections"
>
	{#each SECTIONS as s (s.id)}
		{@const locked = s.beta && !ui.beta}
		{@const SectionIcon = s.icon}
		<button
			type="button"
			onclick={() => ui.go(s.id)}
			aria-current={ui.section === s.id ? 'page' : undefined}
			title={ui.railCollapsed ? s.label : undefined}
			class="rounded-base flex items-center gap-2.5 px-2.5 py-2 text-left text-sm transition-colors
				{ui.section === s.id
				? 'bg-primary-500/15 text-primary-700-300 ring-primary-500/40 ring-1'
				: 'text-app-muted hover:bg-app-card hover:text-app-strong'}"
		>
			<SectionIcon size={16} class="shrink-0" />
			{#if !ui.railCollapsed}
				<span class="truncate">{s.label}</span>
				{#if locked}
					<span class="text-warning-700-300 ml-auto text-[10px] font-semibold tracking-wide uppercase">beta</span>
				{/if}
			{/if}
		</button>
	{/each}

	<button
		type="button"
		onclick={() => (ui.railCollapsed = !ui.railCollapsed)}
		class="text-app-muted hover:text-app-strong rounded-base mt-auto flex items-center gap-2.5 px-2.5 py-2 text-left text-xs"
		aria-label={ui.railCollapsed ? 'Expand sidebar' : 'Collapse sidebar'}
	>
		{#if ui.railCollapsed}
			<ChevronsRightIcon size={16} />
		{:else}
			<ChevronsLeftIcon size={16} />
			<span>Collapse</span>
		{/if}
	</button>
</nav>
