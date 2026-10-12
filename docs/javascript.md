# How JavaScript works in AREOS

Many websites are an empty page that JavaScript fills in. The browser runs
that JavaScript, then shows the result as words, links, buttons and text boxes.

## The parts

- **QuickJS** (https://bellard.org/quickjs, by Fabrice Bellard and Charlie Gordon,
  MIT licence, version 2025-04-26) is the JavaScript engine. It is copied into
  `apps/js/` with its licence. It needs floating point, so it is built apart
  from the rest of the browser (which has floating point turned off); the two
  talk using only whole numbers and pointers (`apps/js/jshost.h`).
- `apps/js/libc/` is a very small C library for it (strings, printf, memory,
  and maths done with the processor's x87 instructions).
- `apps/js/prelude/*.js` is JavaScript that builds a web page's world on top of
  the bare engine: `window`, `document` and the DOM, events, timers, `fetch`,
  `XMLHttpRequest`, `localStorage`, `URL`, `history`, `Intl` (English only) and more.
  The files are joined into one by `prelude/build.sh` and built into the browser.
- `apps/js/jshost.c` starts a page, runs its scripts and timers, and gives the
  browser the page back as simple HTML. `apps/desktop/browser_net.c` lets scripts
  fetch things over http and https.

## What happens when you open a page

1. The browser downloads the page as before and shows it as the server sent it.
2. If it has a `<script>`, the engine starts: classic scripts run, then deferred
   and module scripts (loaded over the network, import maps supported), then
   `DOMContentLoaded` and `load`.
3. When the page changes, the browser asks for it again as simple HTML (about a
   tenth of a second after the last change) and lays it out. The place on the page
   and the control Tab was on are kept.
4. Enter on a link or button, and typing in a text box, are passed to the page's
   scripts, so the page's own code reacts. Links that scripts handle (single-page
   sites) change the page without loading a new one; Back and Forward then move
   through the page's own history first.

## Limits

- No styles: elements hidden only by a stylesheet are still shown.
- No cookies, so logging in to sites won't work. `localStorage` lasts only until AREOS is turned off.
- Forms are sent with GET only; POST navigation is refused. `fetch` and XHR can POST.
- No pictures, canvas, audio, video, WebSocket, Web Workers or WebAssembly.
- Scripts run one at a time on one thread; network requests from scripts block the desktop
  for a moment (up to 20 seconds) while they wait.
- `alert` is read aloud; `confirm` answers no and `prompt` answers nothing.
- A page that runs too long or uses too much memory is stopped (the browser says so).
- If something is wrong, **Alt+J** shows the page's console.

## Testing

`apps/js/` builds on an ordinary Linux computer too: compile `jshost.c`, QuickJS
and a small program that provides the `jsp_*` functions from `jsplatform.h`
with real sockets, and run pages from the command line.
