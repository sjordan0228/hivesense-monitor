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
