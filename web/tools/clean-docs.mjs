// Removes the generated assets from docs/ before a build.
//
// The build cannot use emptyOutDir: docs/ also holds hand-authored markdown and
// .nojekyll that Pages serves, plus the legacy panel. But without a clean the
// hashed chunks from every previous build linger, and since docs/ is committed
// that means the repo grows by a bundle every time anyone rebuilds.

import { existsSync, rmSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const assets = fileURLToPath(new URL('../../docs/assets', import.meta.url));
if (existsSync(assets)) {
	rmSync(assets, { recursive: true, force: true });
	console.log('cleaned docs/assets');
}
