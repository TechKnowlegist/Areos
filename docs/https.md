# How https works in AREOS

The browser opens `https://` pages over a secure (TLS) connection. The
cryptography is **BearSSL** (https://bearssl.org, by Thomas Pornin, MIT
licence), copied into `apps/bearssl/` with its licence. It runs inside the
browser app, in user mode, not in the kernel, so a bug in it can't take the
kernel down.

- `apps/desktop/browser_tls.c` joins BearSSL to AREOS: the random seed, the
  date for checking certificates, and the TCP connection (`areos_tcp_*`).
- `apps/desktop/browser_trust.inc` is the list of root certificates the
  browser trusts (121 certificates, from the Mozilla list in Ubuntu 24.04,
  October 2026).
- `apps/bearssl/src/config.h` has a block marked AREOS that turns off the
  parts of BearSSL that need Linux, SSE or special CPU instructions.

## What is checked

The website's certificate must be signed by one of the trusted roots, be for
the website's name, and be within its dates. If not, the page is not shown
and the browser says why in words.

## Limits (honest list)

- TLS 1.2 and earlier only (BearSSL doesn't do TLS 1.3 yet). Almost every
  site still allows 1.2.
- No certificate revocation checking.
- The random seed comes from the CPU cycle counter, the clock and RDRAND when
  the CPU has it. That's reasonable for a hobby OS, not for protecting
  something valuable.
- AREOS doesn't know your time zone yet, so certificate dates are checked
  with a 14-hour allowance. A certificate that expired in the last 14 hours
  would still be accepted.
- Keep the trusted list fresh: roots expire and new ones appear.

## Refreshing the trusted list

1. Get BearSSL's source (`bearssl-0.6`, from bearssl.org or the Ubuntu `bearssl` source package) and build its tool: `make build/brssl`.
2. Put the roots together: `cat /usr/share/ca-certificates/mozilla/*.crt > roots.pem`
3. Make the list: `build/brssl ta roots.pem > list.inc`
4. Replace the contents of `apps/desktop/browser_trust.inc` with the comment at its top, then `list.inc`.

## Testing without the internet

Make your own certificate authority with `openssl`, add it to the roots in
step 2 above, serve a folder with Python's `ssl` module on port 8443 and open
`https://10.0.2.2:8443/` in the browser (10.0.2.2 is your computer, as seen
from QEMU). Use the name `10.0.2.2` in the server certificate. Put the real
list back afterwards.
