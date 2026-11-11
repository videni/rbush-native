import {ensureUpstream} from './upstream.mjs';
console.log(JSON.stringify(await ensureUpstream(), null, 2));
