import { pathToFileURL } from 'node:url';
import fs from 'node:fs';
import path from 'node:path';
const FS = '/tmp/fs_probe/freesewing', SHIM = '/home/claude/work/fsprobe/shims';
const map = {
  'bezier-js': '/tmp/fs_probe/bezierjs/src/bezier.js',
  'lodash.get': SHIM + '/get.mjs', 'lodash.set': SHIM + '/set.mjs', 'lodash.unset': SHIM + '/unset.mjs', 'lodash.clonedeep': SHIM + '/clonedeep.mjs',
};
export async function resolve(spec, ctx, next) {
  if (map[spec]) return { url: pathToFileURL(map[spec]).href, shortCircuit: true };
  const m = spec.match(/^@freesewing\/(.+)$/);
  if (m) {
    const n = m[1];
    for (const dir of ['designs', 'plugins', 'packages']) {
      const p = path.join(FS, dir, n);
      if (fs.existsSync(p)) {
        const pj = JSON.parse(fs.readFileSync(path.join(p, 'package.json'), 'utf8'));
        const entry = (pj.exports && (typeof pj.exports === 'string' ? pj.exports : pj.exports['.'] && (pj.exports['.'].default || pj.exports['.']))) || pj.module || pj.main;
        const e = typeof entry === 'object' ? (entry.default || entry.import) : entry;
        return { url: pathToFileURL(path.join(p, e)).href, shortCircuit: true };
      }
    }
  }
  return next(spec, ctx);
}
