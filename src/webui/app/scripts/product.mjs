// The name of the product, as the build has it: CLIENT_NAME of the CMakeLists.txt of the repository (the host
// says it too, app.info.name, once the page runs). The pages get it as __CLIENT_NAME__ and in their <title>
// (%CLIENT_NAME%), so that no name of a product is written into the interface.
import { existsSync, readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const CMAKELISTS = join(dirname(fileURLToPath(import.meta.url)), '../../../../CMakeLists.txt');

/** CLIENT_NAME of CMakeLists.txt; the environment's CLIENT_NAME for a copy of src/webui/app alone. */
export function clientName() {
  if (existsSync(CMAKELISTS)) {
    const m = readFileSync(CMAKELISTS, 'utf8').match(/^\s*set\(CLIENT_NAME\s+"([^"]+)"\)/m);
    if (m) return m[1];
  }
  return process.env.CLIENT_NAME || '';
}

/** The plugin of vite: __CLIENT_NAME__ in the scripts, %CLIENT_NAME% in the pages. */
export default function product() {
  const name = clientName();
  return {
    name: 'product',
    config: () => ({ define: { __CLIENT_NAME__: JSON.stringify(name) } }),
    transformIndexHtml: { order: 'pre', handler: (html) => html.replaceAll('%CLIENT_NAME%', name.replace(/[<&]/g, '')) },
  };
}
