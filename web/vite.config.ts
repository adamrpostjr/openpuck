import { defineConfig } from 'vite';
import { svelte } from '@sveltejs/vite-plugin-svelte';
import tailwindcss from '@tailwindcss/vite';
import { resolve } from 'node:path';

export default defineConfig({
	// Tailwind must come before the Svelte plugin.
	plugins: [tailwindcss(), svelte()],
	resolve: { alias: { $lib: resolve(import.meta.dirname, 'src/lib') } },
	build: {
		// Builds to web/dist until the port is functional. At cutover this
		// becomes '../docs', which Pages serves straight from the branch; that
		// directory also holds hand-authored markdown (PROTOCOL.md et al) and
		// .nojekyll, so emptyOutDir must stay false there.
		outDir: 'dist',
		emptyOutDir: true,
		rollupOptions: {
			// docs/sniffer.html is still the original standalone page; it stays
			// untouched (and working) until it is ported as a second entry.
			input: { index: resolve(import.meta.dirname, 'index.html') },
		},
	},
	// Relative asset URLs so the same build works at /openpuck/ on Pages and at
	// the server root when previewing locally.
	base: './',
});
