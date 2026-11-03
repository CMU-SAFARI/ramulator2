// Run the app's TypeScript under Node: resolves extensionless relative imports to .ts files and
// "~/" to app/. Usage: node --experimental-transform-types --import ./scripts/ts-loader.mjs <script.ts>
import { existsSync } from 'node:fs';
import { register } from 'node:module';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const APP = fileURLToPath(new URL('../app/', import.meta.url));

export async function resolve(specifier, context, next) {
  let spec = specifier;
  if (spec.startsWith('~/')) spec = pathToFileURL(APP + spec.slice(2)).href;
  if ((spec.startsWith('.') || spec.startsWith('file:')) && !/\.[cm]?[jt]s$/.test(spec)) {
    const base = spec.startsWith('file:') ? fileURLToPath(spec) : path.resolve(path.dirname(fileURLToPath(context.parentURL)), spec);
    if (existsSync(`${base}.ts`)) spec = pathToFileURL(`${base}.ts`).href;
  }
  return next(spec, context);
}

if (!process.env.__TS_LOADER) {
  process.env.__TS_LOADER = '1';
  register(import.meta.url);
}
