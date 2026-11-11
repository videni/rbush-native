import {readFile} from 'node:fs/promises';
import createModule from './dist/rbush.mjs';
import {createWasmRBush} from './adapter.mjs';
const wasmBinary=await readFile(new URL('./dist/rbush.wasm',import.meta.url));
export const module = await createModule({wasmBinary});
export default createWasmRBush(module);
