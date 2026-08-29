import { defineConfig } from 'vite';
import { svelte } from '@sveltejs/vite-plugin-svelte';
import tailwindcss from '@tailwindcss/vite';
import { resolve } from 'node:path';

export default defineConfig({
	// Tailwind must come before the Svelte plugin.
	plugins: [tailwindcss(), svelte()],
	resolve: { alias: { $lib: resolve(import.meta.dirname, 'src/lib') } },
	build: {
		// Pages serves docs/ straight from the branch, and that directory also
		// holds hand-authored markdown (PROTOCOL.md, BUILD_AND_DEPLOY.md,
		// WEBUSB_LINUX.md, TESTING_GUIDE.md) plus .nojekyll -- emptyOutDir
		// would delete them.
		outDir: '../docs',
		emptyOutDir: false,
		rollupOptions: {
			input: {
				index: resolve(import.meta.dirname, 'index.html'),
				sniffer: resolve(import.meta.dirname, 'sniffer.html'),
			},
		},
	},
	// Relative asset URLs so the same build works at /openpuck/ on Pages and at
	// the server root when previewing locally.
	base: './',
});
