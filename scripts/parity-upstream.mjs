import {readFile, writeFile, mkdtemp, rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {spawnSync} from 'node:child_process';
import {fileURLToPath, pathToFileURL} from 'node:url';
import path from 'node:path';
import {ensureUpstream} from './upstream.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const backend = process.argv[2] || 'native';
if (!['original','native','wasm'].includes(backend)) throw new Error('usage: node scripts/parity-upstream.mjs [original|native|wasm]');
const adapter = pathToFileURL(path.join(root, backend === 'native' ? 'tests/native-adapter.mjs' : 'integrations/wasm/test-adapter.mjs')).href;
const {rbushRoot} = await ensureUpstream();
if (backend === 'original') {
    const run=spawnSync(process.execPath,['--test',path.join(rbushRoot,'test/test.js')],{cwd:rbushRoot,stdio:'inherit',env:process.env});
    process.exit(run.status ?? 1);
}
const original = await readFile(path.join(rbushRoot,'test/test.js'),'utf8');
const generated = original.replace("import RBush from '../index.js';", `import RBush from '${adapter}';`);
if (generated === original) throw new Error('upstream test import changed; inspect before adapting');
const temporary = await mkdtemp(path.join(tmpdir(),'rbush-native-parity-'));
const target = path.join(temporary,`upstream-${backend}.test.mjs`);
await writeFile(target,generated);
try {
    const run=spawnSync(process.execPath,['--test',target],{cwd:root,stdio:'inherit',env:process.env});
    process.exitCode=run.status ?? 1;
} finally { await rm(temporary,{recursive:true,force:true}); }
