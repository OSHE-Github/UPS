import { createRequire } from 'node:module';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
const require = createRequire('C:/Users/john/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/package.json');
const sharp = require('sharp');
const review = resolve(dirname(fileURLToPath(import.meta.url)), '../review');
for (const face of ['front', 'bottom']) {
  await sharp(resolve(review, `native-${face}-assembly.svg`), { density: 180 })
    .flatten({ background: '#ffffff' })
    .png().toFile(resolve(review, `native-${face}-assembly.png`));
}
