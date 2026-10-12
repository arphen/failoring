// Minimal host for failoring.wasm (WASI reactor): instantiate with stub WASI imports.
const fs = require('fs');
async function load(path) {
  const bytes = fs.readFileSync(path);
  let mem;
  const wasi = new Proxy({}, { get: (_, name) => {
    if (name === 'fd_write') return (fd, iov, n, out) => { let t = 0; const dv = new DataView(mem.buffer); for (let i = 0; i < n; i++) t += dv.getUint32(iov + i * 8 + 4, true); dv.setUint32(out, t, true); return 0; };
    if (name === 'proc_exit') return (c) => { throw new Error('exit ' + c); };
    if (name === 'clock_time_get') return (id, prec, out) => { new DataView(mem.buffer).setBigUint64(out, BigInt(Date.now()) * 1000000n, true); return 0; };
    if (name === 'environ_sizes_get' || name === 'args_sizes_get') return (a, b) => { const dv = new DataView(mem.buffer); dv.setUint32(a, 0, true); dv.setUint32(b, 0, true); return 0; };
    return () => 0;
  }});
  const { instance } = await WebAssembly.instantiate(bytes, { wasi_snapshot_preview1: wasi });
  mem = instance.exports.memory;
  if (instance.exports._initialize) instance.exports._initialize();
  return instance.exports;
}
module.exports = { load };
