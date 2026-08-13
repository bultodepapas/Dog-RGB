import { createReadStream, existsSync } from 'node:fs';
import { extname, join, resolve } from 'node:path';
import { createServer } from 'node:http';
import { fileURLToPath } from 'node:url';

import { buildArtifacts } from '../../webui/build.mjs';

// URL.pathname produces paths such as /C:/... on Windows. Convert the file
// URL with Node's platform-aware helper before passing it to path.resolve().
const root = resolve(fileURLToPath(new URL('../..', import.meta.url)));
const outDir = join(root, '.ap-portal-preview');
const args = process.argv.slice(2);
const portFlag = args.indexOf('--port');
const port = portFlag >= 0 ? Number(args[portFlag + 1]) : 4173;

await buildArtifacts();

const routes = new Map([
  ['/', 'index.html'],
  ['/wifi', 'wifi.html'],
  ['/config', 'config.html'],
  ['/dev', 'dev.html'],
]);

const contentTypes = new Map([
  ['.html', 'text/html; charset=utf-8'],
  ['.css', 'text/css; charset=utf-8'],
  ['.js', 'text/javascript; charset=utf-8'],
]);

const server = createServer((req, res) => {
  const url = new URL(req.url ?? '/', `http://${req.headers.host ?? '127.0.0.1'}`);
  const pathname = url.pathname;

  if (pathname.startsWith('/api/')) {
    res.writeHead(404, { 'content-type': 'application/json; charset=utf-8' });
    res.end(JSON.stringify({ status: 'error', reason: 'api route must be mocked by Playwright' }));
    return;
  }

  const filename = routes.get(pathname);
  if (!filename) {
    res.writeHead(302, { location: '/' });
    res.end();
    return;
  }

  const filePath = join(outDir, filename);
  if (!existsSync(filePath)) {
    res.writeHead(500, { 'content-type': 'text/plain; charset=utf-8' });
    res.end(`Missing generated page: ${filename}`);
    return;
  }

  res.writeHead(200, { 'content-type': contentTypes.get(extname(filePath)) ?? 'text/plain; charset=utf-8' });
  createReadStream(filePath).pipe(res);
});

server.listen(port, '127.0.0.1', () => {
  console.log(`AP portal preview server: http://127.0.0.1:${port}/`);
});
