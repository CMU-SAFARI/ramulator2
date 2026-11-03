/**
 * Who may use the server.
 *
 * - Every request needs the access token (server/utils/token.ts): in the `token` query parameter
 *   once (it is then kept in a cookie), in the cookie, or in an `Authorization: Bearer` header
 *   (the simulator).
 * - With the token turned off, requests must name this machine (localhost, 127.0.0.1, [::1]), a GitHub
 *   Codespaces forwarding address, or a host in RAMULATOR_VISUALIZER_ALLOWED_HOSTS
 *   (comma-separated; `*.example.org` allows subdomains).  This stops other web sites from using
 *   the server by DNS rebinding.  A program can claim any Host, though, so a server other
 *   machines can reach serves nothing without a token.
 * - Either way, browser requests must come from this server's own pages or from a page on this
 *   machine.  WebSockets skip the browser's cross-site checks, so this is what stops other sites
 *   (including other people's Codespaces) from reading live sessions.
 */
const LOCAL_HOSTS = ['localhost', '127.0.0.1', '[::1]', '*.app.github.dev'];

let tokenSource: TokenSource | undefined;
let allowedHosts: string[] | undefined;

/** Where the token came from; reading it the first time makes the token file if needed. */
export function accessToken(): TokenSource {
  tokenSource ??= loadToken();
  return tokenSource;
}

function configuredToken(): string {
  return accessToken().token;
}

function hostPatterns(): string[] {
  allowedHosts ??= [
    ...LOCAL_HOSTS,
    ...(nodeProcess.env.RAMULATOR_VISUALIZER_ALLOWED_HOSTS ?? '').split(',').map(h => h.trim().toLowerCase()).filter(Boolean),
  ];
  return allowedHosts;
}

/** Hostname of a Host header or URL authority, without the port. */
function hostname(authority: string): string {
  const a = authority.trim().toLowerCase();
  if (a.startsWith('[')) return a.slice(0, a.indexOf(']') + 1);
  const colon = a.indexOf(':');
  return colon < 0 ? a : a.slice(0, colon);
}

function hostAllowed(authority: string | null | undefined): boolean {
  if (!authority) return false;
  const name = hostname(authority);
  return hostPatterns().some(p => (p.startsWith('*.') ? name.endsWith(p.slice(1)) : name === p));
}

function isLoopback(host: string): boolean {
  const h = host.trim().toLowerCase().replace(/^\[(.*)\]$/, '$1');
  return h === 'localhost' || h === '::1' || /^127\.\d+\.\d+\.\d+$/.test(h);
}

/**
 * A browser request comes from a page of this server (as addressed, or as a forwarding proxy
 * addressed it) or from a page on this machine.  Pages cannot set these headers themselves.
 */
function originAllowed(headers: Headers): boolean {
  const origin = headers.get('origin');
  if (!origin) return true; // not a browser request
  let url: URL;
  try {
    url = new URL(origin);
  } catch {
    return false;
  }
  const own = [headers.get('host'), headers.get('x-forwarded-host')]
    .map(h => h?.split(',')[0]?.trim().toLowerCase())
    .filter(Boolean);
  return own.includes(url.host.toLowerCase()) || isLoopback(url.hostname);
}

/**
 * The address the server can be reached at: RAMULATOR_VISUALIZER_BIND (Docker passes the address
 * it publishes the port on), else the address Nitro listens on, all addresses if unset.  Null for
 * the dev server without a BIND: nuxi listens there, on localhost unless given --host.
 */
function bindAddress(): string | null {
  const env = nodeProcess.env;
  if (env.RAMULATOR_VISUALIZER_BIND) return env.RAMULATOR_VISUALIZER_BIND;
  if (import.meta.dev) return null;
  return env.NITRO_HOST || env.HOST || '0.0.0.0';
}

/** Why the server refuses everything (other machines can reach it and it has no token), or null. */
export function exposedWithoutToken(): string | null {
  const bind = bindAddress();
  if (tokenRequired() || bind === null || isLoopback(bind)) return null;
  return `This visualizer listens on ${bind}, where other machines can reach it, but its access token is `
    + 'turned off, so it serves nothing. Remove RAMULATOR_VISUALIZER_NO_TOKEN, or listen on 127.0.0.1 only.';
}

function sameSecret(a: string, b: string): boolean {
  if (a.length !== b.length) return false;
  let diff = 0;
  for (let i = 0; i < a.length; i++) diff |= a.charCodeAt(i) ^ b.charCodeAt(i);
  return diff === 0;
}

/** Cookie name for this server's token, so visualizers on one host keep separate cookies. */
export function tokenCookieName(): string {
  let h = 0x811c9dc5;
  for (const c of configuredToken()) h = Math.imul(h ^ c.charCodeAt(0), 0x01000193);
  return `ramviz_${(h >>> 0).toString(36)}`;
}

function cookieValue(cookieHeader: string | null | undefined, name: string): string | undefined {
  for (const part of (cookieHeader ?? '').split(';')) {
    const eq = part.indexOf('=');
    if (eq > 0 && part.slice(0, eq).trim() === name) return decodeURIComponent(part.slice(eq + 1).trim());
  }
  return undefined;
}

/** A token as people paste it: with spaces around it, or as the whole printed link. */
export function pastedToken(text: string): string {
  const t = text.trim();
  const inLink = /[?&]token=([^&#\s]+)/.exec(t)?.[1];
  if (!inLink) return t;
  try {
    return decodeURIComponent(inLink);
  } catch {
    return inLink;
  }
}

export function tokenRequired(): boolean {
  return configuredToken() !== '';
}

export function tokenMatches(candidate: string | null | undefined): boolean {
  return !!candidate && sameSecret(candidate, configuredToken());
}

/** Why a request with these headers is refused, or null if it may proceed. */
export function accessDenied(headers: Headers): { status: number; reason: string } | null {
  const exposed = exposedWithoutToken();
  if (exposed) return { status: 503, reason: exposed };
  if (tokenRequired()) {
    const bearer = headers.get('authorization')?.match(/^Bearer\s+(.+)$/i)?.[1];
    const ok = tokenMatches(bearer) || tokenMatches(cookieValue(headers.get('cookie'), tokenCookieName()));
    if (!ok) return { status: 401, reason: 'This visualizer needs its access token.' };
  } else if (!hostAllowed(headers.get('host'))) {
    return { status: 403, reason: 'Requests to this host name are not allowed. Add it to RAMULATOR_VISUALIZER_ALLOWED_HOSTS, or start the visualizer with a token.' };
  }
  if (!originAllowed(headers)) return { status: 403, reason: 'Requests from other web sites are not allowed.' };
  return null;
}
