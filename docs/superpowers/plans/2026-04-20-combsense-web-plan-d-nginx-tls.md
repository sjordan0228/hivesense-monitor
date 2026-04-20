# CombSense Web — Plan D: nginx Reverse Proxy + Self-Signed TLS

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Some tasks run on the `combsense-web` LXC (192.168.1.61) via SSH; most run in this working directory.

**Goal:** Put nginx in front of gunicorn on `combsense-web`, serve `/static/` directly, terminate TLS with a self-signed cert for `dashboard.combsense.com`, and make Django's cookies/CSRF behaviour reverse-proxy-aware.

**Architecture:** nginx listens on :80 (ACME challenge passthrough + HTTPS redirect) and :443 (self-signed TLS). Static files served directly from `/opt/combsense-web/web/staticfiles/`; everything else proxies to gunicorn on `127.0.0.1:8000`. Django settings read four new env vars (secure-cookie toggles + CSRF trusted origins + updated ALLOWED_HOSTS); local dev is unchanged because defaults are off.

**Tech Stack:** Debian 12, nginx 1.22 (apt), OpenSSL 3.0, Django 5.2.13, pytest-django 4.8.

**Reference spec:** [docs/superpowers/specs/2026-04-20-combsense-web-plan-d-nginx-tls-design.md](../specs/2026-04-20-combsense-web-plan-d-nginx-tls-design.md)

---

## Scope notes

- **LAN-only.** No public DNS, no port-forwarding, no Let's Encrypt. Spec §4 lists the Phase 2 switch procedure.
- **Operator-owned env file.** `provision.sh` never rewrites `/etc/combsense-web/env`. Task 7 walks the one-time migration of the existing env file.
- **No code changes to gunicorn, Postgres, Redis, systemd unit.** Plan A's artefacts stay untouched.
- **Test strategy.** The only new Python code is a trivial `env_bool` helper — Task 1 TDD's it. The rest is config + shell; the smoke test in Task 7 is the real verification (matches spec §8).

---

## File / Component Structure

**New files (in this repo):**
- `deploy/web/nginx/combsense-web.conf` — nginx site config, one server block for :80 and one for :443.
- `web/core/tests/test_settings_helpers.py` — unit tests for the `env_bool` helper.

**Modified files (in this repo):**
- `web/combsense/settings.py` — add `env_bool` helper, `SECURE_PROXY_SSL_HEADER`, `SESSION_COOKIE_SECURE`, `CSRF_COOKIE_SECURE`, `CSRF_TRUSTED_ORIGINS`.
- `deploy/web/env.template` — add four new keys with comments.
- `deploy/web/provision.sh` — add env-key guard + nginx install/config/cert block.
- `deploy/web/README.md` — client `/etc/hosts` setup, env-file migration one-liner, first-visit cert UX, Phase 2 switch steps.
- `.mex/ROUTER.md` — infrastructure block reflects Plan D complete; routing table unchanged; update completion bullet.

**LXC-side changes (one-off, Task 7):**
- `/etc/combsense-web/env` — append three keys, rewrite `DJANGO_ALLOWED_HOSTS`, set `DJANGO_DEBUG=0`.

---

## Task 1: `env_bool` helper + unit tests

**Files:**
- Modify: `web/combsense/settings.py` — add helper
- Create: `web/core/tests/test_settings_helpers.py`

**Rationale:** New helper, trivial but easy to get subtly wrong (case handling, default fallback). TDD.

- [ ] **Step 1.1: Write the failing test**

Create `web/core/tests/test_settings_helpers.py`:

```python
import os
from unittest import mock

import pytest

from combsense.settings import env_bool


class TestEnvBool:
    def test_unset_returns_default_false(self):
        with mock.patch.dict(os.environ, {}, clear=False):
            os.environ.pop("CS_TEST_FLAG", None)
            assert env_bool("CS_TEST_FLAG", default=False) is False

    def test_unset_returns_default_true(self):
        with mock.patch.dict(os.environ, {}, clear=False):
            os.environ.pop("CS_TEST_FLAG", None)
            assert env_bool("CS_TEST_FLAG", default=True) is True

    @pytest.mark.parametrize("value", ["1", "true", "True", "TRUE", "yes", "YES"])
    def test_truthy_values(self, value):
        with mock.patch.dict(os.environ, {"CS_TEST_FLAG": value}):
            assert env_bool("CS_TEST_FLAG") is True

    @pytest.mark.parametrize("value", ["0", "false", "False", "no", "off", "garbage", ""])
    def test_falsy_values(self, value):
        with mock.patch.dict(os.environ, {"CS_TEST_FLAG": value}):
            assert env_bool("CS_TEST_FLAG") is False
```

- [ ] **Step 1.2: Run test to verify it fails**

```
cd web && pytest core/tests/test_settings_helpers.py -v
```
Expected: `ImportError: cannot import name 'env_bool' from 'combsense.settings'`

- [ ] **Step 1.3: Add `env_bool` helper to settings**

Edit `web/combsense/settings.py`. After the `_parse_dsn` function (line ~30), add:

```python
def env_bool(key: str, default: bool = False) -> bool:
    """Read an env var as a boolean. Truthy: 1/true/yes (case-insensitive)."""
    raw = os.environ.get(key)
    if raw is None:
        return default
    return raw.strip().lower() in ("1", "true", "yes")
```

- [ ] **Step 1.4: Run tests to verify they pass**

```
cd web && pytest core/tests/test_settings_helpers.py -v
```
Expected: `15 passed` (2 default-value cases + 6 truthy-value parametrizations + 7 falsy-value parametrizations).

- [ ] **Step 1.5: Run full suite — no regressions**

```
cd web && pytest
```
Expected: all prior tests still pass (was 19 passing pre-Plan D; now 34 with the new parametrized ones — the exact number is not load-bearing, but none should fail).

- [ ] **Step 1.6: Commit**

```bash
git add web/combsense/settings.py web/core/tests/test_settings_helpers.py
git commit -m "feat(web): env_bool helper for settings"
```

---

## Task 2: HTTPS-aware Django settings

**Files:**
- Modify: `web/combsense/settings.py` — add four settings

**Rationale:** Settings that make Django correct behind a reverse proxy. Each is env-gated with dev-safe default; no new tests (existing suite verifies no regression).

- [ ] **Step 2.1: Add settings**

Edit `web/combsense/settings.py`. Append after the `SESSION_EXPIRE_AT_BROWSER_CLOSE = False` line (line ~106):

```python
# --- Reverse-proxy / HTTPS awareness (Plan D) ---
# nginx terminates TLS; tell Django to trust the X-Forwarded-Proto header.
SECURE_PROXY_SSL_HEADER = ("HTTP_X_FORWARDED_PROTO", "https")

# Only send session + CSRF cookies over HTTPS in prod. Off by default so local
# `manage.py runserver` (HTTP) keeps working; prod env sets DJANGO_SECURE_COOKIES=1.
SESSION_COOKIE_SECURE = env_bool("DJANGO_SECURE_COOKIES", default=False)
CSRF_COOKIE_SECURE    = env_bool("DJANGO_SECURE_COOKIES", default=False)

# CSRF needs to know which origins it should accept POSTs from when behind a
# proxy. Comma-separated env var, e.g. https://dashboard.combsense.com,https://192.168.1.61
CSRF_TRUSTED_ORIGINS = [
    o.strip()
    for o in os.environ.get("DJANGO_CSRF_TRUSTED_ORIGINS", "").split(",")
    if o.strip()
]
```

- [ ] **Step 2.2: Run full test suite — no regressions**

```
cd web && pytest
```
Expected: all tests pass. The new settings read from env vars that are unset in the test environment, so defaults apply (cookies insecure, CSRF_TRUSTED_ORIGINS empty) — prior behaviour preserved.

- [ ] **Step 2.3: Verify local runserver still starts**

```
cd web && python manage.py check
```
Expected: `System check identified no issues (0 silenced).`

- [ ] **Step 2.4: Commit**

```bash
git add web/combsense/settings.py
git commit -m "feat(web): reverse-proxy + HTTPS cookie settings"
```

---

## Task 3: nginx site config

**Files:**
- Create: `deploy/web/nginx/combsense-web.conf`

- [ ] **Step 3.1: Create the nginx config directory**

```bash
mkdir -p deploy/web/nginx
```

- [ ] **Step 3.2: Write the site config**

Create `deploy/web/nginx/combsense-web.conf`:

```nginx
# /etc/nginx/sites-available/combsense-web
# Day 1: self-signed, LAN-only.  Phase 2: swap cert paths + uncomment HSTS.

# --- HTTP (:80) — ACME challenge passthrough; everything else → HTTPS
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

    client_max_body_size 16m;   # room for Phase 2 firmware uploads
}
```

- [ ] **Step 3.3: Syntax-check locally (optional but cheap if `nginx` installed on Mac)**

If `nginx` is available locally:
```
nginx -t -c deploy/web/nginx/combsense-web.conf -p /tmp/
```
Expected: syntax warnings about missing `events {}` block (it's a site-config snippet, not a full nginx.conf) — ignore. Real validation happens on the LXC in Task 7 via `nginx -t` against the full config.

If `nginx` is not available locally, skip this step.

- [ ] **Step 3.4: Commit**

```bash
git add deploy/web/nginx/combsense-web.conf
git commit -m "feat(web): nginx reverse-proxy site config"
```

---

## Task 4: `env.template` — new keys

**Files:**
- Modify: `deploy/web/env.template`

- [ ] **Step 4.1: Update env.template**

Replace the full contents of `deploy/web/env.template` with:

```
# /etc/combsense-web/env  (mode 600, owned root:combsense)
# Copy this template, fill in values, chmod 600, chown root:combsense.

DJANGO_SECRET_KEY=<generate with: python -c "import secrets; print(secrets.token_urlsafe(50))">
DJANGO_DEBUG=0
DJANGO_ALLOWED_HOSTS=dashboard.combsense.com,combsense-web,192.168.1.X,localhost,127.0.0.1

POSTGRES_DSN=postgres://combsense:<password>@127.0.0.1:5432/combsense

REDIS_URL=redis://127.0.0.1:6379/0

# Email — console for first deploy; real SMTP configured in a later plan
DJANGO_EMAIL_BACKEND=django.core.mail.backends.console.EmailBackend
DJANGO_DEFAULT_FROM_EMAIL=noreply@combsense.local

# Reverse-proxy / HTTPS (Plan D) — nginx terminates TLS
DJANGO_SECURE_COOKIES=1
DJANGO_CSRF_TRUSTED_ORIGINS=https://dashboard.combsense.com,https://192.168.1.X
```

- [ ] **Step 4.2: Commit**

```bash
git add deploy/web/env.template
git commit -m "feat(web): env.template gains Plan D keys"
```

---

## Task 5: `provision.sh` — env guard + nginx block

**Files:**
- Modify: `deploy/web/provision.sh`

- [ ] **Step 5.1: Add the env-key guard**

Edit `deploy/web/provision.sh`. Find the existing env-file check (the block that starts `# Ensure /etc/combsense-web/env exists — operator must fill in`). Replace that entire block with:

```bash
# Ensure /etc/combsense-web/env exists — operator must fill in
if [ ! -f /etc/combsense-web/env ]; then
  echo "!! /etc/combsense-web/env missing — copy deploy/web/env.template and fill in values"
  exit 1
fi

# Plan D gate: DJANGO_CSRF_TRUSTED_ORIGINS is the newest key; its presence proves
# the operator has migrated the env file per the current env.template.
grep -q "^DJANGO_CSRF_TRUSTED_ORIGINS=" /etc/combsense-web/env || {
  echo "!! /etc/combsense-web/env missing Plan D keys — see deploy/web/env.template"
  exit 1
}
```

- [ ] **Step 5.2: Add the nginx block**

Still in `deploy/web/provision.sh`. At the end of the file (after `systemctl status combsense-web.service --no-pager`), append:

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

- [ ] **Step 5.3: Local shell syntax check**

```bash
bash -n deploy/web/provision.sh
```
Expected: no output (syntax OK). Any errors here must be fixed before the LXC run.

- [ ] **Step 5.4: Commit**

```bash
git add deploy/web/provision.sh
git commit -m "feat(web): provision.sh installs nginx + self-signed cert"
```

---

## Task 6: `deploy/web/README.md` — runbook updates

**Files:**
- Modify: `deploy/web/README.md`

- [ ] **Step 6.1: Replace the README with the Plan D runbook**

Replace the full contents of `deploy/web/README.md` with:

```markdown
# combsense-web LXC — operator runbook

LXC hosting the Django admin dashboard behind nginx (TLS) on
`https://dashboard.combsense.com/`.

## First-time provision

1. Create LXC per Task 1 in the Plan A document.
2. Install `deploy/web/env.template` to `/etc/combsense-web/env`, fill in values
   (including the Plan D keys — `DJANGO_SECURE_COOKIES`, `DJANGO_CSRF_TRUSTED_ORIGINS`,
   and a `dashboard.combsense.com` entry in `DJANGO_ALLOWED_HOSTS`),
   `chmod 600`, `chown root:combsense`.
3. `bash deploy/web/provision.sh` — clones repo, builds venv, migrates, collects static,
   installs systemd unit, starts gunicorn, installs nginx, generates self-signed cert,
   reloads nginx.

## Migrating an existing env file from Plan A to Plan D

On the LXC, back up first, then append the three new keys and rewrite ALLOWED_HOSTS:

```
sudo cp /etc/combsense-web/env /etc/combsense-web/env.plan-a.bak
sudo sed -i 's|^DJANGO_ALLOWED_HOSTS=.*|DJANGO_ALLOWED_HOSTS=dashboard.combsense.com,combsense-web,192.168.1.61,localhost,127.0.0.1|' /etc/combsense-web/env
sudo sed -i 's|^DJANGO_DEBUG=.*|DJANGO_DEBUG=0|' /etc/combsense-web/env
sudo tee -a /etc/combsense-web/env >/dev/null <<'EOF'

# Reverse-proxy / HTTPS (Plan D) — nginx terminates TLS
DJANGO_SECURE_COOKIES=1
DJANGO_CSRF_TRUSTED_ORIGINS=https://dashboard.combsense.com,https://192.168.1.61
EOF
```

Then re-run `provision.sh`.

## Update to a new commit

On the LXC:

```
bash /opt/combsense-web/src/deploy/web/provision.sh
```

Idempotent — pulls from origin, rebuilds venv, re-migrates, restarts gunicorn,
reinstalls nginx config, reloads nginx. The self-signed cert is generated once
and never rotated by re-runs.

## Create the first superuser

```
cd /opt/combsense-web/web
sudo -u combsense bash -c 'set -a; . /etc/combsense-web/env; set +a; exec "$@"' _ \
  ../.venv/bin/python manage.py createsuperuser
```

## Client DNS setup (LAN, pre-Phase-2)

Public DNS for `dashboard.combsense.com` is not configured yet, so each client
points the hostname at the LXC manually.

**macOS / Linux** — add to `/etc/hosts`:

```
sudo sh -c "echo '192.168.1.61 dashboard.combsense.com' >> /etc/hosts"
```

**iOS / Android** — no `/etc/hosts`; pick one:

- Add a DNS override on the LAN router, Pi-hole, or AdGuard-Home.
- Or access via IP directly: `https://192.168.1.61/` (the cert SAN lists the IP).

## First-visit cert warning

The cert is self-signed, so the browser shows
`NET::ERR_CERT_AUTHORITY_INVALID` on first visit. Accept once per host:

- Chrome/Edge: "Advanced" → "Proceed to dashboard.combsense.com (unsafe)".
- Safari: "Show Details" → "Visit Website" → authenticate.
- Firefox: "Advanced" → "Accept the Risk and Continue".

Phase 2 replaces the cert with Let's Encrypt and the warning goes away.

## Access

- Dashboard: `https://dashboard.combsense.com/` (needs a hosts entry — see above).
- Direct-IP fallback: `https://192.168.1.61/`.
- Django admin: `https://dashboard.combsense.com/admin/`.

## Logs

```
journalctl -u combsense-web.service -f    # Django / gunicorn
journalctl -u nginx.service -f            # nginx systemd events
tail -f /var/log/nginx/access.log         # request log
tail -f /var/log/nginx/error.log          # nginx errors
```

## Phase 2 switch — public internet + Let's Encrypt

No repo changes beyond uncommenting the HSTS line in
`deploy/web/nginx/combsense-web.conf`.

1. At the registrar: create A record `dashboard.combsense.com → <home public IP>`.
2. Router: port-forward 80/443 to `192.168.1.61`.
3. On the LXC:
   ```
   sudo apt install certbot python3-certbot-nginx
   sudo certbot --nginx -d dashboard.combsense.com
   ```
   certbot rewrites the `ssl_certificate` lines in the site config in place.
4. In the repo: uncomment the `Strict-Transport-Security` line in
   `deploy/web/nginx/combsense-web.conf`, commit, push.
5. Re-run `provision.sh` on the LXC. (certbot's in-place edits survive because
   `provision.sh` deploys the repo copy; the repo copy is the canonical source
   once HSTS is uncommented — if certbot rewrote the cert paths, they are
   preserved by certbot's own re-apply hooks on renewal.)
```

- [ ] **Step 6.2: Commit**

```bash
git add deploy/web/README.md
git commit -m "docs(web): Plan D runbook — client DNS, env migration, cert UX"
```

---

## Task 7: Deploy to LXC + smoke test

**Files:** None in repo — this task runs against the live LXC at `192.168.1.61`.

**Prerequisite:** Tasks 1–6 pushed to `dev` on GitHub (`git push origin dev`).

- [ ] **Step 7.1: Push the branch**

From the Mac:
```
git push origin dev
```

- [ ] **Step 7.2: Back up the existing env file on the LXC**

```
ssh natas@192.168.1.61 "sudo cp /etc/combsense-web/env /etc/combsense-web/env.plan-a.bak"
```

- [ ] **Step 7.3: Migrate the env file**

```
ssh natas@192.168.1.61 "sudo sed -i 's|^DJANGO_ALLOWED_HOSTS=.*|DJANGO_ALLOWED_HOSTS=dashboard.combsense.com,combsense-web,192.168.1.61,localhost,127.0.0.1|' /etc/combsense-web/env && sudo sed -i 's|^DJANGO_DEBUG=.*|DJANGO_DEBUG=0|' /etc/combsense-web/env && sudo tee -a /etc/combsense-web/env >/dev/null <<'EOF'

# Reverse-proxy / HTTPS (Plan D) — nginx terminates TLS
DJANGO_SECURE_COOKIES=1
DJANGO_CSRF_TRUSTED_ORIGINS=https://dashboard.combsense.com,https://192.168.1.61
EOF
sudo grep -E 'DJANGO_(DEBUG|ALLOWED_HOSTS|SECURE_COOKIES|CSRF_TRUSTED_ORIGINS)=' /etc/combsense-web/env"
```

Expected: output shows all four keys with the new values.

- [ ] **Step 7.4: Re-run provision.sh**

```
ssh natas@192.168.1.61 "sudo bash /opt/combsense-web/src/deploy/web/provision.sh"
```

Expected final lines include:
- `nginx: configuration file /etc/nginx/nginx.conf test is successful`
- systemd status for `combsense-web.service` shows `Active: active (running)`

If `provision.sh` exits with `!! /etc/combsense-web/env missing Plan D keys`, Step 7.3 did not take — re-check the env file.

- [ ] **Step 7.5: Smoke test from the Mac**

```bash
# 1. nginx listens on 80 and 443
ssh natas@192.168.1.61 "sudo ss -tlnp | grep -E ':80|:443'"
```
Expected: two lines, one for `:80`, one for `:443`, both `nginx`.

```bash
# 2. nginx config validates
ssh natas@192.168.1.61 "sudo nginx -t"
```
Expected: `syntax is ok` + `test is successful`.

```bash
# 3. HTTP redirects to HTTPS
curl -sI -H 'Host: dashboard.combsense.com' http://192.168.1.61/ | head -2
```
Expected: `HTTP/1.1 301 Moved Permanently` + `Location: https://dashboard.combsense.com/`.

```bash
# 4. ACME challenge path is NOT redirected
curl -sI -H 'Host: dashboard.combsense.com' http://192.168.1.61/.well-known/acme-challenge/probe | head -1
```
Expected: `HTTP/1.1 404 Not Found` (served by nginx on :80, not redirected to :443).

```bash
# 5. HTTPS admin login reachable
curl -sk -H 'Host: dashboard.combsense.com' https://192.168.1.61/admin/login/ -o /dev/null -w '%{http_code}\n'
```
Expected: `200`.

```bash
# 6. Static files served by nginx
curl -sk -H 'Host: dashboard.combsense.com' -I https://192.168.1.61/static/admin/css/base.css | grep -i '^server:'
```
Expected: `Server: nginx/1.22.x` (or similar). Proves nginx `alias` directive hit, not proxy.

```bash
# 7. DEBUG is off — 404 is Django default, not a traceback
curl -sk -H 'Host: dashboard.combsense.com' https://192.168.1.61/does-not-exist -o /dev/null -w '%{http_code}\n'
```
Expected: `404`.

```bash
# 8. CSRF trusted origins active — POST admin login with valid creds returns 302
TOKEN=$(ssh natas@192.168.1.61 "curl -sk -c /tmp/cj -H 'Host: dashboard.combsense.com' https://127.0.0.1/admin/login/ | grep -oE 'name=\"csrfmiddlewaretoken\" value=\"[^\"]+\"' | sed -E 's/.*value=\"([^\"]+)\".*/\1/'")
ssh natas@192.168.1.61 "curl -sk -o /dev/null -w '%{http_code} %{redirect_url}\n' -b /tmp/cj -c /tmp/cj -H 'Host: dashboard.combsense.com' -H 'Referer: https://dashboard.combsense.com/admin/login/' -X POST --data-urlencode 'csrfmiddlewaretoken=$TOKEN' --data-urlencode 'username=shane@linuxgangster.org' --data-urlencode 'password=p4nWi29x!@' --data-urlencode 'next=/admin/' https://127.0.0.1/admin/login/"
```
Expected: `302 https://127.0.0.1/admin/` (successful login redirect; no CSRF 403).

- [ ] **Step 7.6: Add local `/etc/hosts` entry on the Mac**

```
sudo sh -c "echo '192.168.1.61 dashboard.combsense.com' >> /etc/hosts"
```

- [ ] **Step 7.7: Manual UAT in browser** *(operator, not scripted)*

1. Open `https://dashboard.combsense.com/admin/` in Chrome/Safari.
2. Accept the cert warning once.
3. Log in with superuser credentials.
4. Admin page renders with full CSS (header, filter sidebar, icon buttons — not the 2000s-era unstyled layout from before Plan D).
5. Click into a model (e.g. Users), edit your record, hit Save — no CSRF failure.
6. Refresh — session persists.

If all of the above pass, Plan D is live. This task has no commit — it's a deploy + verify step only.

---

## Task 8: `.mex/ROUTER.md` — reflect Plan D complete

**Files:**
- Modify: `.mex/ROUTER.md`

- [ ] **Step 8.1: Update the Infrastructure block**

Edit `.mex/ROUTER.md`. Find the `combsense-web LXC: 192.168.1.61` block. Add an nginx bullet after the `Django (gunicorn)` line, so the block reads:

```markdown
- **combsense-web LXC:** 192.168.1.61 — Proxmox LXC 125, NFS-backed, Debian 12 (unprivileged)
  - **PostgreSQL 15** on 127.0.0.1:5432 (db: `combsense`, user: `combsense`)
  - **Redis 7** on 127.0.0.1:6379 (for Celery later)
  - **Django (gunicorn)** on 127.0.0.1:8000 via `combsense-web.service`
  - **nginx 1.22** on :80 / :443 — reverse-proxies gunicorn, serves `/static/` directly, self-signed TLS for `dashboard.combsense.com` (cert at `/etc/ssl/combsense/`)
  - Credentials at `/root/.combsense-web-creds` (mode 600)
  - Systemd drop-in at `/etc/systemd/system/combsense-web.service.d/override.conf` (unprivileged LXC workaround)
```

- [ ] **Step 8.2: Update the Completed section**

Find the `**combsense-web Plan A complete**` bullet. Immediately below it (still within the Completed list), add a new sibling bullet:

```markdown
- **combsense-web Plan D complete** — nginx reverse proxy + self-signed TLS
  - `deploy/web/nginx/combsense-web.conf` — :80 redirects to :443 (ACME challenge passthrough reserved); :443 terminates TLS, serves `/static/` via `alias`, proxies `/` to gunicorn with `X-Forwarded-*` headers
  - `provision.sh` extended: apt-installs nginx, deploys site config, generates self-signed cert (once, 10-year, SAN covers hostname + IP), reloads nginx after `nginx -t`
  - Django settings: `SECURE_PROXY_SSL_HEADER`, `SESSION_COOKIE_SECURE` / `CSRF_COOKIE_SECURE` (env-gated via `DJANGO_SECURE_COOKIES`), `CSRF_TRUSTED_ORIGINS` from env
  - `env.template` + runbook updated; operator migrates `/etc/combsense-web/env` once per the README
  - Access: `https://dashboard.combsense.com/` (hosts entry) or `https://192.168.1.61/`; Phase 2 switches to Let's Encrypt via deployment-only change
```

- [ ] **Step 8.3: Update the "Not yet built" section**

Find the line starting with `- combsense-web Plan B onward:`. Leave it — Plan B is still what's next.

- [ ] **Step 8.4: Update the routing table**

Find the row `| combsense-web LXC ops | deploy/web/README.md |`. Leave it — the README now covers Plan D runbook too.

- [ ] **Step 8.5: Update the `last_updated` frontmatter**

Find the frontmatter `last_updated:` line. Replace it with:

```
last_updated: 2026-04-20 (combsense-web Plan D complete: nginx reverse proxy + self-signed TLS)
```

- [ ] **Step 8.6: Commit**

```bash
git add .mex/ROUTER.md
git commit -m "docs(mex): reflect combsense-web Plan D completion"
```

- [ ] **Step 8.7: Push and open the PR** *(final step — optional, can be done via finishing-a-development-branch skill)*

```
git push origin dev
```

---

## Definition of Done for Plan D

- [ ] `pytest` green (≥ 34 tests across `accounts`, `core`, and the new `test_settings_helpers.py`)
- [ ] `deploy/web/nginx/combsense-web.conf` in repo; `provision.sh` deploys it idempotently
- [ ] LXC serves `https://dashboard.combsense.com/admin/` with valid (self-signed) TLS
- [ ] Django admin UI renders with full styling (CSS served by nginx, not Django)
- [ ] `DJANGO_DEBUG=0` on the LXC; 404 responses show Django's default page (no traceback)
- [ ] HTTP requests 301-redirect to HTTPS; `/.well-known/acme-challenge/` stays on :80
- [ ] Superuser can log in and submit admin POSTs without CSRF errors
- [ ] `.mex/ROUTER.md` Infrastructure block includes nginx; Completed section lists Plan D

---

## What's next

**Plan B — Ingest + hive readings.** Adds the MQTT subscriber service (auto-claim `Device` rows from unknown `sensor_id`s), the Influx query client with bucket resolution, and the hive detail Readings tab with Chart.js charts. Depends on Plan A's auth and Plan D's nginx (for serving Chart.js assets and the hive detail HTML efficiently).
