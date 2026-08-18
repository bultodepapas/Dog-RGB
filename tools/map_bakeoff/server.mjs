import { createReadStream, statSync } from "node:fs";
import { createServer } from "node:http";
import { extname, join, normalize } from "node:path";
import { fileURLToPath } from "node:url";

const root = fileURLToPath(new URL("./", import.meta.url));
const args = process.argv.slice(2);
const portIndex = args.indexOf("--port");
const port = portIndex >= 0 ? Number(args[portIndex + 1]) : 4174;

if (!Number.isInteger(port) || port < 1024 || port > 65535) {
  throw new Error("--port must be an integer from 1024 to 65535");
}

const types = new Map([
  [".html", "text/html; charset=utf-8"],
  [".css", "text/css; charset=utf-8"],
  [".js", "text/javascript; charset=utf-8"],
  [".mjs", "text/javascript; charset=utf-8"],
]);

createServer((request, response) => {
  const pathname = new URL(request.url ?? "/", "http://127.0.0.1").pathname;
  if (pathname === "/favicon.ico") {
    response.writeHead(204, { "Cache-Control": "no-store" }).end();
    return;
  }
  const requested = pathname === "/" ? "index.html" : pathname.slice(1);
  const normalized = normalize(requested);
  if (normalized.startsWith("..") || normalized.includes(":")) {
    response.writeHead(400).end("Bad path");
    return;
  }

  const path = join(root, normalized);
  try {
    const stat = statSync(path);
    if (!stat.isFile()) throw new Error("not a file");
    response.writeHead(200, {
      "Content-Type": types.get(extname(path)) ?? "application/octet-stream",
      "Cache-Control": "no-store",
      "Referrer-Policy": "origin-when-cross-origin",
      "X-Content-Type-Options": "nosniff",
      "X-Frame-Options": "DENY",
    });
    createReadStream(path).pipe(response);
  } catch {
    response.writeHead(404).end("Not found");
  }
}).listen(port, "127.0.0.1", () => {
  console.log(`Map bakeoff: http://127.0.0.1:${port}/?provider=stadia-dark`);
});
