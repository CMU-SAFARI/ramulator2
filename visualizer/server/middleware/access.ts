/**
 * Applies the access rules in server/utils/access.ts to every HTTP request except the health
 * check.  With a token, the first visit carries it as `?token=…`; it is then kept in a cookie and
 * removed from the address bar.  Pages opened without it show a form to enter it.
 */
const PAGE = (message: string, askToken: boolean) => `<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>RamViz</title>
<style>
  :root { color-scheme: light dark; --bg: #fbfaf8; --ink: #1d1b19; --muted: #6b665f; --rule: #d9d4cc; }
  @media (prefers-color-scheme: dark) { :root { --bg: #161514; --ink: #ebe7e1; --muted: #a39d94; --rule: #3a3733; } }
  body { margin: 0; min-height: 100vh; display: grid; place-items: center; background: var(--bg); color: var(--ink);
         font: 15px/1.5 system-ui, sans-serif; padding: 0 16px; }
  main { max-width: 440px; display: grid; gap: 14px; }
  h1 { margin: 0; font-size: 22px; font-weight: 600; }
  p { margin: 0; color: var(--muted); }
  code { font-family: ui-monospace, monospace; font-size: 13px; color: var(--ink); }
  form { display: flex; gap: 8px; }
  input { flex: 1; min-width: 0; padding: 8px 10px; border: 1px solid var(--rule); background: transparent; color: inherit; font: 13px ui-monospace, monospace; }
  button { padding: 8px 14px; border: 1px solid var(--ink); background: var(--ink); color: var(--bg); font: inherit; cursor: pointer; }
</style></head>
<body><main>
  <h1>RamViz</h1>
  <p>${message}</p>
  ${askToken ? `<p>Open the link that <code>python -m ramulator visualize</code> printed, or paste its token here.</p>
  <form method="get"><input id="token" name="token" aria-label="Access token" autocomplete="off" autofocus><button type="submit">Open</button></form>` : ''}
</main></body></html>`;

/** The browser keeps the token this long; a visualizer started later with a new token asks again. */
const COOKIE_DAYS = 30;

export default defineEventHandler((event) => {
  if (event.path === '/api/health') return;

  const query = getQuery(event);
  const queryToken = typeof query.token === 'string' ? pastedToken(query.token) : null;
  if (queryToken !== null && tokenRequired() && tokenMatches(queryToken) && event.method === 'GET') {
    setCookie(event, tokenCookieName(), queryToken, { httpOnly: true, sameSite: 'lax', path: '/', maxAge: COOKIE_DAYS * 86400 });
    const url = getRequestURL(event);
    url.searchParams.delete('token');
    return sendRedirect(event, url.pathname + url.search, 302);
  }

  const denied = accessDenied(event.headers);
  if (!denied) return;
  setResponseStatus(event, denied.status);
  if (event.method === 'GET' && (getHeader(event, 'accept') ?? '').includes('text/html')) {
    setHeader(event, 'content-type', 'text/html; charset=utf-8');
    if (denied.status !== 401) return PAGE(denied.reason, false);
    return PAGE(queryToken !== null ? 'That token does not match this visualizer.' : 'This visualizer is protected by an access token.', true);
  }
  return { error: denied.reason };
});
