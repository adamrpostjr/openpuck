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
			// sniffer.html is a redirect stub, not an app entry: the sniffer is a
			// section of the panel now. It is copied verbatim so the old URL,
			// linked from puck_sniffer/README.md, keeps working.
			input: { index: resolve(import.meta.dirname, 'index.html') },
		},
	},
	// Relative asset URLs so the same build works at /openpuck/ on Pages and at
	// the server root when previewing locally.
	base: './',
});
