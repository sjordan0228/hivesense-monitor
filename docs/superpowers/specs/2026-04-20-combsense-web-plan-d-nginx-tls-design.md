# CombSense Web — Plan D: nginx Reverse Proxy + TLS — Design Spec

**Date:** 2026-04-20
**Scope:** Put nginx in front of gunicorn on the `combsense-web` LXC. Serve `/static/` directly from disk, terminate TLS with a self-signed cert, and structure the config so the Phase 2 switch to Let's Encrypt + public internet is a config-only diff.

**Reference spec:** [2026-04-20-combsense-web-dashboard-design.md](2026-04-20-combsense-web-dashboard-design.md)
**Prior plan:** [2026-04-20-combsense-web-plan-a-infra-auth.md](../plans/2026-04-20-combsense-web-plan-a-infra-auth.md)

---

## 1. Goals / Non-goals

### Goals
- Browser reaches the admin dashboard at `https://dashboard.combsense.com/` from the LAN.
- nginx serves `/static/` directly from disk — admin UI renders correctly.
- `DEBUG=False` in the deployed environment (proper posture behind a reverse proxy).
- Self-signed TLS cert with `CN = dashboard.combsense.com` — one-time browser warning, then trusted per-host.
- Port 80 redirects to 443, except `/.well-known/acme-challenge/` which stays on 80 (reserved for Phase 2 LE HTTP-01 challenge).
- nginx config structured so Phase 2 switch = swap cert paths + uncomment HSTS + `certbot --nginx`, no layout changes.

### Non-goals (deferred)
- Public-internet exposure, DNS registration at the registrar, home-router port forwarding.
- Let's Encrypt certbot integration.
- HSTS header (unsafe against a self-signed cert — enabled in Phase 2).
- Rate limits, fail2ban, WAF.
- Media-file serving (no firmware uploads yet; Plan B/Phase 2).
- Celery / MQTT ingest (deferred to their own plans).

---

## 2. Architecture

### Request path (Day 1, LAN)

```
Browser (Mac/phone)
   │  https://dashboard.combsense.com/   (resolved via /etc/hosts → 192.168.1.61)
   ▼
nginx (:443, self-signed cert)           ── on combsense-web LXC
   ├── /static/*                         → sendfile from /opt/combsense-web/web/staticfiles/
   ├── /.well-known/acme-challenge/*     served from :80, root /var/www/html (empty day 1)
   └── /*                                → proxy_pass → http://127.0.0.1:8000 (gunicorn)
                                               │
                                               ▼
                                          gunicorn → Django
                                          (DEBUG=False,
                                           ALLOWED_HOSTS includes dashboard.combsense.com)
```

Port 80 listener:
- `/.well-known/acme-challenge/` → `root /var/www/html;` (empty directory day 1; certbot writes here in Phase 2).
- Everything else: `301 → https://$host$request_uri`.

### New components on the LXC

| Component | Purpose |
|-----------|---------|
| `nginx` (apt, Debian 12, 1.22+) | TLS termination, static files, proxy |
| `/etc/nginx/sites-available/combsense-web` | Site config — canonical copy lives in the repo, symlinked into `sites-enabled/` by `provision.sh` |
| `/etc/ssl/combsense/dashboard.crt` + `dashboard.key` | Self-signed cert (generated once by `provision.sh`, never rotated) |
| `/var/www/html/` | ACME challenge webroot (empty day 1) |

### Unchanged
- gunicorn still binds `127.0.0.1:8000`.
- `combsense-web.service` systemd unit — no edits.
- Postgres, Redis — untouched.

---

## 3. nginx configuration

Single config file `deploy/web/nginx/combsense-web.conf`, committed to the repo. Structured so Phase 2 is a three-line diff.

```nginx
# /etc/nginx/sites-available/combsense-web
# Day 1: self-signed, LAN-only.  Phase 2: swap cert paths + enable HSTS.

# --- HTTP (:80) — ACME challenge passthrough, everything else redirects to HTTPS
server {
    listen 80;
    listen [::]:80;
    server_name dashboard.combsense.com;

    location /.well-known/acme-challenge/ {
        root /var/www/html;
    }

    location / {
        return 301 https://$host$request_uri;
    }
}

# --- HTTPS (:443) — the actual app
server {
    listen 443 ssl;
    listen [::]:443 ssl;
    http2 on;
    server_name dashboard.combsense.com;

    # Phase 2: replace with /etc/letsencrypt/live/dashboard.combsense.com/{fullchain,privkey}.pem
    ssl_certificate     /etc/ssl/combsense/dashboard.crt;
    ssl_certificate_key /etc/ssl/combsense/dashboard.key;
    ssl_protocols       TLSv1.2 TLSv1.3;
    ssl_ciphers         HIGH:!aNULL:!MD5;

    # Phase 2: uncomment once cert is publicly trusted
    # add_header Strict-Transport-Security "max-age=31536000; includeSubDomains" always;

    # Static files served directly by nginx
    location /static/ {
        alias /opt/combsense-web/web/staticfiles/;
        expires 7d;
        access_log off;
    }

    # Everything else proxies to gunicorn
    location / {
        proxy_pass         http://127.0.0.1:8000;
        proxy_http_version 1.1;
        proxy_set_header   Host              $host;
        proxy_set_header   X-Real-IP         $remote_addr;
        proxy_set_header   X-Forwarded-For   $proxy_add_x_forwarded_for;
        proxy_set_header   X-Forwarded-Proto $scheme;
        proxy_read_timeout 60s;
    }

    client_max_body_size 16m;   # Phase 2 firmware uploads
}
```

**Notes:**
- Default `/etc/nginx/sites-enabled/default` is removed by `provision.sh` so our server block owns :80/:443 unambiguously.
- `X-Forwarded-Proto` lets Django recognise requests as HTTPS (see `SECURE_PROXY_SSL_HEADER` in §5).
- HSTS stays commented — enabling it against a self-signed cert would brick browsers that pinned it.
- `client_max_body_size 16m` is forward-looking for Phase 2 firmware binaries; harmless today.

---

## 4. TLS certificate lifecycle

### Day 1 — self-signed generated on first provision

Inside `provision.sh`, after `apt-get install nginx openssl`:

```bash
CERT_DIR=/etc/ssl/combsense
install -d -m 755 "${CERT_DIR}"
if [ ! -f "${CERT_DIR}/dashboard.crt" ]; then
  openssl req -x509 -nodes -newkey rsa:2048 \
    -keyout "${CERT_DIR}/dashboard.key" \
    -out    "${CERT_DIR}/dashboard.crt" \
    -days   3650 \
    -subj   "/CN=dashboard.combsense.com" \
    -addext "subjectAltName=DNS:dashboard.combsense.com,DNS:combsense-web,IP:192.168.1.61"
  chmod 600 "${CERT_DIR}/dashboard.key"
fi
```

Key properties:
- **Idempotent** — cert only generated if absent; re-runs of `provision.sh` never rotate it.
- **10-year validity** — no renewal chore on the LAN path.
- **SAN includes LAN IP + bare hostname** — `https://192.168.1.61` and `https://combsense-web` also work without cert-mismatch warnings beyond the initial CA warning.
- **Key mode 600** — owned by root; nginx reads it before dropping privileges.

### First-visit UX
- Browser shows "NET::ERR_CERT_AUTHORITY_INVALID" once.
- Accept (Chrome: Advanced → Proceed; Safari: Show Details → Visit Website). Persists per-host.
- Django admin loads with full styling.

### Phase 2 switch (deployment-only, no repo diff beyond HSTS uncomment)
1. Add public A record: `dashboard.combsense.com → <home public IP>`.
2. Port-forward 80/443 on the home router to `192.168.1.61`.
3. On the LXC: `apt install certbot python3-certbot-nginx && certbot --nginx -d dashboard.combsense.com`.
4. certbot rewrites `ssl_certificate` paths in the site config to `/etc/letsencrypt/live/.../fullchain.pem`.
5. Uncomment the HSTS line in `deploy/web/nginx/combsense-web.conf` and re-run `provision.sh` (or edit `/etc/nginx/sites-available/combsense-web` in place — certbot tolerates it).
6. `systemctl reload nginx`.

No Django changes. No additional repo work.

---

## 5. Django settings changes

Four env-driven additions to `web/combsense/settings.py`. Local dev (`DEBUG=1`, HTTP runserver) keeps working because all new behaviour is gated on env defaults that are off in dev.

```python
# Trust X-Forwarded-Proto from nginx so request.is_secure() / secure cookies work
SECURE_PROXY_SSL_HEADER = ("HTTP_X_FORWARDED_PROTO", "https")

# Cookies only over HTTPS (toggle off in dev)
SESSION_COOKIE_SECURE = env_bool("DJANGO_SECURE_COOKIES", default=False)
CSRF_COOKIE_SECURE    = env_bool("DJANGO_SECURE_COOKIES", default=False)

# CSRF needs to know the reverse-proxied origin for POSTs
CSRF_TRUSTED_ORIGINS = [
    o.strip()
    for o in os.environ.get("DJANGO_CSRF_TRUSTED_ORIGINS", "").split(",")
    if o.strip()
]
```

`env_bool` helper (added once to `settings.py`):
```python
def env_bool(key, default=False):
    return os.environ.get(key, str(int(default))).lower() in ("1", "true", "yes")
```

### New env keys on the LXC `/etc/combsense-web/env`

```
DJANGO_DEBUG=0
DJANGO_ALLOWED_HOSTS=dashboard.combsense.com,combsense-web,192.168.1.61,localhost,127.0.0.1
DJANGO_SECURE_COOKIES=1
DJANGO_CSRF_TRUSTED_ORIGINS=https://dashboard.combsense.com,https://192.168.1.61
```

### `deploy/web/env.template` updated
Add all four keys with the defaults above and short comments.

### Local dev unchanged
Local `.env` keeps `DEBUG=1` and omits the three new keys — defaults leave secure cookies off and CSRF origins empty. `manage.py runserver` keeps working as today.

---

## 6. Client access

Since public DNS is out of scope for Day 1, each client needs a `/etc/hosts` entry (or equivalent).

**macOS / Linux:**
```
# /etc/hosts
192.168.1.61  dashboard.combsense.com
```
Add: `sudo sh -c "echo '192.168.1.61 dashboard.combsense.com' >> /etc/hosts"`.

**iOS / Android (no hosts-file edit possible):**
- Preferred: add a DNS override on your router / Pi-hole / AdGuard-Home. One-time setup; all LAN devices pick it up.
- Workaround: access via IP directly — `https://192.168.1.61/` — cert SAN lists the IP so it works.

**Zero-hostfile fallback from any device:** `https://192.168.1.61/` works because the single `:443` server block matches and the cert SAN covers the IP.

The `deploy/web/README.md` runbook gains a "Client DNS setup" section with these instructions.

---

## 7. `provision.sh` extensions

Appended after the systemd unit install, before the final gunicorn restart. Idempotent.

```bash
# --- nginx reverse proxy (Plan D) -----------------------------------
apt-get install -y --no-install-recommends nginx openssl

install -m 644 "${CHECKOUT_DIR}/deploy/web/nginx/combsense-web.conf" \
        /etc/nginx/sites-available/combsense-web
ln -snf /etc/nginx/sites-available/combsense-web \
        /etc/nginx/sites-enabled/combsense-web
rm -f /etc/nginx/sites-enabled/default

CERT_DIR=/etc/ssl/combsense
install -d -m 755 "${CERT_DIR}"
if [ ! -f "${CERT_DIR}/dashboard.crt" ]; then
  openssl req -x509 -nodes -newkey rsa:2048 \
    -keyout "${CERT_DIR}/dashboard.key" \
    -out    "${CERT_DIR}/dashboard.crt" \
    -days   3650 \
    -subj   "/CN=dashboard.combsense.com" \
    -addext "subjectAltName=DNS:dashboard.combsense.com,DNS:combsense-web,IP:192.168.1.61"
  chmod 600 "${CERT_DIR}/dashboard.key"
fi

install -d -m 755 /var/www/html

nginx -t
systemctl enable nginx
systemctl reload nginx
# -------------------------------------------------------------------
```

### Env-file guard (added earlier in `provision.sh`, with existing `/etc/combsense-web/env` check)
```bash
# DJANGO_CSRF_TRUSTED_ORIGINS is Plan D-new — its presence proves the operator
# has also updated DJANGO_ALLOWED_HOSTS and the other Plan D keys per env.template.
grep -q "^DJANGO_CSRF_TRUSTED_ORIGINS=" /etc/combsense-web/env || {
  echo "!! /etc/combsense-web/env missing Plan D keys — see deploy/web/env.template"
  exit 1
}
```

### Order matters
- nginx block runs **after** gunicorn is restarted, so if nginx fails validation, gunicorn stays up.
- `nginx -t` before `reload`; `set -e` aborts provision on validation failure, leaving any previous working config live.

### Not auto-managed
`provision.sh` does not rewrite `/etc/combsense-web/env`. Operator updates the env file once before the first Plan D provision (the runbook carries this step).

---

## 8. Verification / test plan

No Django test changes (all new behaviour is env-gated; dev defaults keep the existing suite green). Verification is behavioural on the deployed LXC.

### Smoke test from the Mac after `provision.sh`

```bash
# 1. nginx listens on 80 and 443
ssh natas@192.168.1.61 "sudo ss -tlnp | grep -E ':80|:443'"

# 2. nginx config validates
ssh natas@192.168.1.61 "sudo nginx -t"

# 3. HTTP redirects to HTTPS
curl -sI -H 'Host: dashboard.combsense.com' http://192.168.1.61/ | head -1
#   Expect: HTTP/1.1 301 Moved Permanently  →  Location: https://dashboard.combsense.com/

# 4. ACME challenge path stays on HTTP
curl -sI -H 'Host: dashboard.combsense.com' http://192.168.1.61/.well-known/acme-challenge/test | head -1
#   Expect: HTTP/1.1 404 Not Found  (served by nginx, not redirected)

# 5. HTTPS admin login reachable
curl -sk -H 'Host: dashboard.combsense.com' https://192.168.1.61/admin/login/ -o /dev/null -w '%{http_code}\n'
#   Expect: 200

# 6. Static files served by nginx (not Django)
curl -sk -H 'Host: dashboard.combsense.com' -I https://192.168.1.61/static/admin/css/base.css | grep -i server
#   Expect: Server: nginx/...

# 7. DEBUG is off (404 body is Django default page, not traceback)
curl -sk -H 'Host: dashboard.combsense.com' https://192.168.1.61/does-not-exist -o /dev/null -w '%{http_code}\n'
#   Expect: 404
```

### Manual UAT (operator, browser)
- Visit `https://dashboard.combsense.com/admin/` → accept cert warning once → log in → admin renders with full styling (CSS, icons, filter dropdowns).
- Edit your user in admin → no CSRF failures on POST.
- Session persists across reloads.

### Rollback
`systemctl disable --now nginx`, then the Plan A SSH-tunnel access path keeps working. No state loss.

---

## 9. Open questions (resolve during planning; non-blocking for spec approval)

1. **Operator env-file migration on the existing LXC.** The deployed `/etc/combsense-web/env` lacks the four new keys. Runbook documents a one-liner `cat >>` append before first Plan D provision, rather than shipping a migration script — single LXC, one-shot.
2. **nginx access log retention.** Debian defaults (weekly rotate, 14 copies) are fine; no action.
3. **gunicorn binding.** Stays `127.0.0.1:8000`; no change.
4. **ALLOWED_HOSTS still includes `localhost,127.0.0.1`.** Kept so the `ssh -L 8000:127.0.0.1:8000` emergency tunnel keeps working for debugging.
5. **`http2 on;` directive.** Debian 12 ships nginx 1.22; supported. Confirmed during planning.

---

## 10. Deliverables summary

- `deploy/web/nginx/combsense-web.conf` — new
- `deploy/web/provision.sh` — extended (nginx install, config deploy, cert generation, env-key guard)
- `deploy/web/env.template` — four new keys added
- `deploy/web/README.md` — new sections for env-file migration, client `/etc/hosts` setup, first-visit cert-warning UX, and Phase 2 switch steps
- `web/combsense/settings.py` — `env_bool` helper, `SECURE_PROXY_SSL_HEADER`, two `*_COOKIE_SECURE`, `CSRF_TRUSTED_ORIGINS`
- `.mex/ROUTER.md` — updated infrastructure block (Plan D reflected), completion bullet

No new Python dependencies. No new systemd units beyond the stock `nginx.service`. No database changes.
