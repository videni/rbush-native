import {readFile, writeFile, mkdir, rm} from 'node:fs/promises';
import {spawnSync} from 'node:child_process';
import {fileURLToPath, pathToFileURL} from 'node:url';
import path from 'node:path';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const backend = process.argv[2] || 'native';
if (!['native','wasm'].includes(backend)) throw new Error('usage: node scripts/parity-upstream.mjs [native|wasm]');
const adapter = pathToFileURL(path.join(root, backend === 'native' ? 'cpp/test/native-adapter.mjs' : 'wasm/test-adapter.mjs')).href;
const original = await readFile(path.join(root,'test/test.js'),'utf8');
const generated = original.replace("import RBush from '../index.js';", `import RBush from '${adapter}';`);
if (generated === original) throw new Error('upstream test import changed; inspect before adapting');
await mkdir(path.join(root,'build'),{recursive:true});
const target = path.join(root,'build',`upstream-${backend}.test.mjs`);
await writeFile(target,generated);
try {
    const run=spawnSync(process.execPath,['--test',target],{cwd:root,stdio:'inherit',env:process.env});
    process.exitCode=run.status ?? 1;
} finally { await rm(target); }
