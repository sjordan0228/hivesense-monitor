#!/usr/bin/env bash
# Provisions /opt/combsense-web on a fresh combsense-web LXC.
# Idempotent — safe to re-run after a git pull.
#
# Run as root on the LXC.

set -euo pipefail

REPO_URL="${REPO_URL:-https://github.com/sjordan0228/combsense-monitor.git}"
BRANCH="${BRANCH:-main}"
INSTALL_DIR="/opt/combsense-web"
CHECKOUT_DIR="${INSTALL_DIR}/src"
VENV_DIR="${INSTALL_DIR}/.venv"
APP_USER="combsense"

# Ensure base tree exists
install -d -o "${APP_USER}" -g "${APP_USER}" "${INSTALL_DIR}"

# Clone or pull
if [ ! -d "${CHECKOUT_DIR}/.git" ]; then
  sudo -u "${APP_USER}" git clone -b "${BRANCH}" "${REPO_URL}" "${CHECKOUT_DIR}"
else
  sudo -u "${APP_USER}" git -C "${CHECKOUT_DIR}" fetch origin
  sudo -u "${APP_USER}" git -C "${CHECKOUT_DIR}" checkout "${BRANCH}"
  sudo -u "${APP_USER}" git -C "${CHECKOUT_DIR}" reset --hard "origin/${BRANCH}"
fi

# Venv
if [ ! -d "${VENV_DIR}" ]; then
  sudo -u "${APP_USER}" python3 -m venv "${VENV_DIR}"
fi
sudo -u "${APP_USER}" "${VENV_DIR}/bin/pip" install --upgrade pip
sudo -u "${APP_USER}" "${VENV_DIR}/bin/pip" install -r "${CHECKOUT_DIR}/web/requirements.txt"

# Link the web source directly so manage.py / wsgi work
ln -snf "${CHECKOUT_DIR}/web" "${INSTALL_DIR}/web"

# Ensure /etc/combsense-web/env exists — operator must fill in
if [ ! -f /etc/combsense-web/env ]; then
  echo "!! /etc/combsense-web/env missing — copy deploy/web/env.template and fill in values"
  exit 1
fi

# Django migrate + collectstatic
cd "${INSTALL_DIR}/web"
sudo -u "${APP_USER}" env --file /etc/combsense-web/env \
  "${VENV_DIR}/bin/python" manage.py migrate --noinput

sudo -u "${APP_USER}" env --file /etc/combsense-web/env \
  "${VENV_DIR}/bin/python" manage.py collectstatic --noinput

# Install systemd unit
install -m 644 "${CHECKOUT_DIR}/deploy/web/combsense-web.service" /etc/systemd/system/
install -d /etc/systemd/system/combsense-web.service.d
install -m 644 "${CHECKOUT_DIR}/deploy/web/combsense-web.service.d/override.conf" \
        /etc/systemd/system/combsense-web.service.d/
systemctl daemon-reload
systemctl enable combsense-web.service

systemctl restart combsense-web.service
systemctl status combsense-web.service --no-pager
