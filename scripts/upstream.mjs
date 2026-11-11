import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {mkdir, mkdtemp, rename, rm, realpath, symlink} from 'node:fs/promises';
import {homedir} from 'node:os';
import path from 'node:path';
import {fileURLToPath, pathToFileURL} from 'node:url';

export const RBUSH_PIN = 'e59712716a84dda2e31f9b471b32faa178d8056f';
export const QUICKSELECT_PIN = '0711310d28fe5f8a5ca75c721626d8e49feb9ccb';
const exec = promisify(execFile);
const repository = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const defaultCache = process.platform === 'darwin'
    ? path.join(homedir(), 'Library', 'Caches')
    : (process.env.XDG_CACHE_HOME || path.join(homedir(), '.cache'));
const requestedCache = process.env.RBUSH_UPSTREAM_CACHE || path.join(defaultCache, 'rbush-native', 'upstream');
export const upstreamCache = path.resolve(requestedCache.replace(/^~(?=\/|$)/, homedir()));

async function git(args, cwd) {
    const {stdout} = await exec('git', args, {cwd, encoding:'utf8', maxBuffer:4*1024*1024});
    return stdout.trim();
}
async function verify(root, pin) {
    const actual = await git(['rev-parse', 'HEAD'], root);
    if (actual !== pin) throw new Error(`Pinned reference at ${root} has HEAD ${actual}; expected ${pin}`);
    // Tracked reference inputs must remain the actual pinned source, not local edits.
    const changes = await git(['status', '--porcelain', '--untracked-files=no'], root);
    if (changes) throw new Error(`Pinned reference at ${root} contains tracked modifications`);
}
async function pinnedCheckout(name, pin) {
    const root = path.join(upstreamCache, `${name}-${pin}`);
    try {
        await realpath(root);
        await verify(root, pin);
        return root;
    } catch (error) {
        if (error.code !== 'ENOENT') throw error;
    }
    const temporary = await mkdtemp(path.join(upstreamCache, `.${name}-fetch-`));
    try {
        await git(['init', '--quiet'], temporary);
        await git(['remote', 'add', 'origin', `https://github.com/mourner/${name}.git`], temporary);
        await git(['fetch', '--quiet', '--depth=1', 'origin', pin], temporary);
        await git(['checkout', '--quiet', '--detach', 'FETCH_HEAD'], temporary);
        await verify(temporary, pin);
        try {
            await rename(temporary, root);
        } catch (error) {
            // Independent native/browser checks can initialize the cache together.
            if (!['EEXIST','ENOTEMPTY'].includes(error.code)) throw error;
            await verify(root, pin);
        }
    } finally {
        await rm(temporary, {recursive:true, force:true});
    }
    return root;
}
let ready;
export function ensureUpstream() {
    if (!ready) ready = (async () => {
        await mkdir(upstreamCache, {recursive:true});
        const actualCache = await realpath(upstreamCache);
        const actualRepository = await realpath(repository);
        const relative = path.relative(actualRepository, actualCache);
        if (relative === '' || (!relative.startsWith(`..${path.sep}`) && relative !== '..' && !path.isAbsolute(relative))) {
            throw new Error('RBUSH_UPSTREAM_CACHE must be outside the rbush-native repository');
        }
        const [rbushRoot, quickselectRoot] = await Promise.all([
            pinnedCheckout('rbush', RBUSH_PIN), pinnedCheckout('quickselect', QUICKSELECT_PIN)
        ]);
        const modules = path.join(rbushRoot, 'node_modules');
        await mkdir(modules, {recursive:true});
        const link = path.join(modules, 'quickselect');
        try { await symlink(quickselectRoot, link, 'dir'); }
        catch (error) {
            if (error.code !== 'EEXIST') throw error;
            if (await realpath(link) !== await realpath(quickselectRoot)) {
                throw new Error(`Reference dependency ${link} does not resolve to pinned quickselect`);
            }
        }
        return {rbushRoot, quickselectRoot};
    })();
    return ready;
}
export async function loadOriginalRBush() {
    const {rbushRoot} = await ensureUpstream();
    return (await import(pathToFileURL(path.join(rbushRoot, 'index.js')).href)).default;
}
