import createModule from './dist/rbush.mjs';
import {createWasmRBush} from './adapter.mjs';
export const module=await createModule();
export const WasmRBush=createWasmRBush(module);
