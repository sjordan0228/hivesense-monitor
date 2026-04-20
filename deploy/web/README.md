# combsense-web LXC — operator runbook

LXC hosting the Django admin dashboard.

## First-time provision

1. Create LXC per Task 1 in the Plan A document.
2. Install `deploy/web/env.template` to `/etc/combsense-web/env`, fill in values,
   `chmod 600`, `chown root:combsense`.
3. `bash deploy/web/provision.sh` — clones repo, builds venv, migrates, collects static,
   installs systemd unit, starts gunicorn.

## Update to a new commit

On the LXC:

```
bash /opt/combsense-web/src/deploy/web/provision.sh
```

Idempotent — pulls from origin, rebuilds venv, re-migrates, restarts.

## Create the first superuser

```
cd /opt/combsense-web/web
sudo -u combsense env --file /etc/combsense-web/env \
  ../.venv/bin/python manage.py createsuperuser
```

## Access

- Django admin: `http://<lxc-ip>:8000/admin/` (gunicorn binds 127.0.0.1 — use SSH tunnel or nginx from Plan D)
- SSH tunnel from laptop: `ssh -L 8000:127.0.0.1:8000 root@<lxc-ip>`
- Then open `http://localhost:8000/admin/` in browser

## Logs

```
journalctl -u combsense-web.service -f
```
