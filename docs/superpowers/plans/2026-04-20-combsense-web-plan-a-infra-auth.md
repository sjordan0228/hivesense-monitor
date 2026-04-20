# CombSense Web — Plan A: Infrastructure + Django Base + Auth

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Some tasks run on the Proxmox host / new LXC, others in this working directory. Paths are absolute where they cross systems.

**Goal:** Stand up the `combsense-web` LXC, lay down the Django project skeleton with a custom email-based User model, and ship working login/logout/password-reset so you can access Django admin over LAN.

**Architecture:** New Proxmox LXC (`combsense-web`) running Debian 12 unprivileged. Django project in this repo under `web/`, Postgres 16 and Redis on the LXC, dev workflow uses local Postgres so you can iterate on a Mac. Auth uses a custom `User` model (`accounts.User`) subclassing `AbstractUser` with email as `USERNAME_FIELD` and a `role` field (`admin` / `beekeeper`).

**Tech Stack:** Debian 12 (LXC), Python 3.11, Django 5.2 LTS, Postgres 15 (Debian 12 default), Redis 7, gunicorn 21, paho-mqtt (later plan), pytest-django 4.8, python-dotenv.

**Reference spec:** [docs/superpowers/specs/2026-04-20-combsense-web-dashboard-design.md](../specs/2026-04-20-combsense-web-dashboard-design.md)

---

## Scope notes

- **LXC host:** new Debian 12 unprivileged LXC on Proxmox. Suggested hostname `combsense-web`, static LAN IP (record in Task 1). Sized 2 vCPU / 4 GB RAM / 20 GB LVM — room for Postgres growth.
- **Credentials convention:** matches `combsense-tsdb` — tokens in `/root/.combsense-web-creds` (mode 600).
- **No nginx or TLS in this plan.** Plan D handles it. Dev-time access to Django runs on `:8000` directly; that's fine behind the LAN.
- **No Celery or MQTT subscriber in this plan.** Plan B covers ingest; Plan D covers alerts/Celery.
- **Local dev:** develop on Mac (or wherever), run `manage.py runserver` against local Postgres, deploy via `git pull && systemctl restart`.

---

## File/Component Structure

**On the LXC (`combsense-web`):**
- `/etc/combsense-web/env` — dotenv file, mode 600, owned by app user
- `/var/lib/combsense-web/` — runtime data dir (later plans add firmware blobs)
- `/opt/combsense-web/` — git checkout + venv
- `/etc/systemd/system/combsense-web.service` — gunicorn unit
- `/etc/systemd/system/combsense-web.service.d/override.conf` — sandboxing drop-in (unprivileged-LXC workaround)

**In this repo (`hivesense-monitor`):**
- `web/` — **NEW.** Django project root.
  - `manage.py` — Django CLI entry
  - `requirements.txt` — pinned dependencies
  - `pytest.ini` — pytest-django config
  - `conftest.py` — pytest fixtures
  - `.env.example` — dotenv template (real `.env` stays out of git)
  - `combsense/` — Django project config
    - `__init__.py`
    - `settings.py` — single-file settings reading from env
    - `urls.py` — URL router
    - `wsgi.py`, `asgi.py`
  - `accounts/` — custom User model + auth views
    - `__init__.py`, `apps.py`
    - `models.py` — `User` extending `AbstractUser`, email as USERNAME_FIELD
    - `managers.py` — custom `UserManager` for email-based auth
    - `admin.py` — Django admin registration
    - `forms.py` — login/password-reset forms
    - `urls.py`, `views.py` — login/logout/password-reset views (Django's built-ins, customized)
    - `migrations/` — generated migrations
    - `tests/` — `test_models.py`, `test_views.py`
  - `core/` — shared chrome
    - `__init__.py`, `apps.py`
    - `views.py` — logged-in home (`/`) placeholder
    - `urls.py`
    - `templates/` — `base.html`, `registration/login.html`, `registration/password_reset*.html`
    - `tests/test_views.py`
- `deploy/web/` — **NEW.** Deployment assets.
  - `combsense-web.service` — systemd gunicorn unit template
  - `combsense-web.service.d/override.conf` — sandboxing drop-in
  - `provision.sh` — first-time LXC bootstrap (documented, idempotent)
  - `README.md` — operator runbook for this LXC
- `.mex/context/conventions.md` — update with web-app conventions (after implementation)
- `.mex/ROUTER.md` — update routing table entry for web work

---

## Task 1: Provision `combsense-web` LXC

**Files/Targets:**
- Proxmox host shell (`pct` CLI) or Proxmox web UI
- New LXC container `combsense-web` (Debian 12 unprivileged)

- [ ] **Step 1.1: Create the LXC on Proxmox**

From the Proxmox host shell (replace `<CTID>` with next free container ID, e.g. `125`):

```bash
pct create <CTID> local:vztmpl/debian-12-standard_12.12-1_amd64.tar.zst \
  --hostname combsense-web \
  --cores 2 \
  --memory 4096 \
  --swap 1024 \
  --rootfs NFS:20 \
  --net0 name=eth0,bridge=vmbr0,ip=dhcp \
  --unprivileged 1 \
  --features nesting=1 \
  --onboot 1 \
  --start 1
```

Note the assigned IP from `pct list` / DHCP lease. Pin it in the router's DHCP reservation table, or switch to static IP with:

```bash
pct set <CTID> --net0 name=eth0,bridge=vmbr0,ip=192.168.1.20/24,gw=192.168.1.1
pct reboot <CTID>
```

Record chosen IP here: **`192.168.1.X` → `combsense-web`**.

- [ ] **Step 1.2: Enter the container and update packages**

```bash
pct enter <CTID>
apt update && apt -y full-upgrade
apt -y install curl ca-certificates gnupg lsb-release sudo git vim ufw
```

- [ ] **Step 1.3: Create the app user**

```bash
adduser --system --group --home /opt/combsense-web --shell /bin/bash combsense
mkdir -p /opt/combsense-web /var/lib/combsense-web /etc/combsense-web
chown combsense:combsense /opt/combsense-web /var/lib/combsense-web
chown root:combsense /etc/combsense-web
chmod 750 /etc/combsense-web
```

- [ ] **Step 1.4: Install Postgres 16**

```bash
apt -y install postgresql-15
systemctl enable --now postgresql
sudo -u postgres createuser --pwprompt combsense        # set and record password
sudo -u postgres createdb --owner=combsense combsense
```

Record the Postgres password in `/root/.combsense-web-creds` (mode 600):

```bash
install -m 600 /dev/null /root/.combsense-web-creds
cat > /root/.combsense-web-creds <<EOF
postgres_user=combsense
postgres_pass=<the password you set>
postgres_db=combsense
EOF
```

- [ ] **Step 1.5: Install Redis**

```bash
apt -y install redis-server
# Default config listens on 127.0.0.1 only — fine for us
systemctl enable --now redis-server
redis-cli ping     # expect: PONG
```

- [ ] **Step 1.6: Install Python 3.11 + build tools**

```bash
apt -y install python3 python3-venv python3-dev libpq-dev build-essential
python3 --version    # expect: Python 3.11.x
```

- [ ] **Step 1.7: Smoke test from the LAN**

From your Mac:

```bash
ssh root@<combsense-web-ip> "hostname && systemctl is-active postgresql redis-server"
# expect:
# combsense-web
# active
# active
```

- [ ] **Step 1.8: Commit nothing yet** — this task is infra, not code. Proceed to Task 2.

---

## Task 2: Repo layout + Django project scaffold

**Files:**
- Create: `web/` (new top-level dir)
- Create: `web/requirements.txt`
- Create: `web/.env.example`
- Create: `web/pytest.ini`
- Create: `web/conftest.py`
- Create: `web/manage.py` (via `django-admin`)
- Create: `web/combsense/settings.py`, `urls.py`, `wsgi.py`, `asgi.py`
- Create: `.gitignore` additions (repo root)

- [ ] **Step 2.1: Add web/ to `.gitignore` for generated files**

Open `/Users/sjordan/Code/hivesense-monitor/.gitignore` and append:

```gitignore

# Django web app
web/.env
web/.venv/
web/staticfiles/
web/**/__pycache__/
web/**/*.pyc
web/.pytest_cache/
web/htmlcov/
web/.coverage
```

- [ ] **Step 2.2: Create `web/requirements.txt`**

```
Django==5.0.9
psycopg[binary]==3.2.3
python-dotenv==1.0.1
gunicorn==22.0.0
pytest==8.3.3
pytest-django==4.9.0
```

- [ ] **Step 2.3: Create `web/.env.example`**

```
# Secrets — copy to web/.env and fill in locally; deploy fills /etc/combsense-web/env
DJANGO_SECRET_KEY=change-me-to-a-50-char-random-string
DJANGO_DEBUG=1
DJANGO_ALLOWED_HOSTS=localhost,127.0.0.1

# Postgres — local dev defaults
POSTGRES_DSN=postgres://combsense:combsense@localhost:5432/combsense

# Redis (placeholder — used later)
REDIS_URL=redis://localhost:6379/0
```

- [ ] **Step 2.4: Create local venv + install deps**

From `web/`:

```bash
cd /Users/sjordan/Code/hivesense-monitor/web
python3 -m venv .venv
.venv/bin/pip install --upgrade pip
.venv/bin/pip install -r requirements.txt
```

- [ ] **Step 2.5: Scaffold Django project with `combsense` as the project name**

```bash
cd /Users/sjordan/Code/hivesense-monitor/web
.venv/bin/django-admin startproject combsense .
```

After this, you should have `web/manage.py`, `web/combsense/settings.py`, etc.

- [ ] **Step 2.6: Replace `web/combsense/settings.py` with an env-driven version**

Overwrite the generated `settings.py` with:

```python
"""
Django settings for combsense project.

All secrets and environment-specific values read from env (dotenv in dev,
systemd Environment= or EnvironmentFile= in prod).
"""
from pathlib import Path
import os
from urllib.parse import urlparse

from dotenv import load_dotenv

BASE_DIR = Path(__file__).resolve().parent.parent
load_dotenv(BASE_DIR / ".env")

SECRET_KEY = os.environ["DJANGO_SECRET_KEY"]
DEBUG = os.environ.get("DJANGO_DEBUG", "0") == "1"
ALLOWED_HOSTS = [h.strip() for h in os.environ.get("DJANGO_ALLOWED_HOSTS", "").split(",") if h.strip()]


def _parse_dsn(dsn: str) -> dict:
    """Parse postgres://user:pass@host:port/db into Django DATABASES keys."""
    p = urlparse(dsn)
    return {
        "NAME": (p.path or "/").lstrip("/"),
        "USER": p.username or "",
        "PASSWORD": p.password or "",
        "HOST": p.hostname or "",
        "PORT": str(p.port) if p.port else "",
    }


INSTALLED_APPS = [
    "django.contrib.admin",
    "django.contrib.auth",
    "django.contrib.contenttypes",
    "django.contrib.sessions",
    "django.contrib.messages",
    "django.contrib.staticfiles",
    # local
    "accounts",
    "core",
]

MIDDLEWARE = [
    "django.middleware.security.SecurityMiddleware",
    "django.contrib.sessions.middleware.SessionMiddleware",
    "django.middleware.common.CommonMiddleware",
    "django.middleware.csrf.CsrfViewMiddleware",
    "django.contrib.auth.middleware.AuthenticationMiddleware",
    "django.contrib.messages.middleware.MessageMiddleware",
    "django.middleware.clickjacking.XFrameOptionsMiddleware",
]

ROOT_URLCONF = "combsense.urls"

TEMPLATES = [
    {
        "BACKEND": "django.template.backends.django.DjangoTemplates",
        "DIRS": [BASE_DIR / "templates"],
        "APP_DIRS": True,
        "OPTIONS": {
            "context_processors": [
                "django.template.context_processors.request",
                "django.contrib.auth.context_processors.auth",
                "django.contrib.messages.context_processors.messages",
            ],
        },
    },
]

WSGI_APPLICATION = "combsense.wsgi.application"

DATABASES = {
    "default": {
        "ENGINE": "django.db.backends.postgresql",
        "CONN_MAX_AGE": 60,
        **_parse_dsn(os.environ["POSTGRES_DSN"]),
    }
}

AUTH_USER_MODEL = "accounts.User"

AUTH_PASSWORD_VALIDATORS = [
    {"NAME": "django.contrib.auth.password_validation.UserAttributeSimilarityValidator"},
    {"NAME": "django.contrib.auth.password_validation.MinimumLengthValidator"},
    {"NAME": "django.contrib.auth.password_validation.CommonPasswordValidator"},
    {"NAME": "django.contrib.auth.password_validation.NumericPasswordValidator"},
]

LANGUAGE_CODE = "en-us"
TIME_ZONE = "UTC"
USE_I18N = True
USE_TZ = True

STATIC_URL = "static/"
STATIC_ROOT = BASE_DIR / "staticfiles"

DEFAULT_AUTO_FIELD = "django.db.models.BigAutoField"

LOGIN_URL = "accounts:login"
LOGIN_REDIRECT_URL = "core:home"
LOGOUT_REDIRECT_URL = "accounts:login"

SESSION_COOKIE_AGE = 60 * 60 * 24 * 14  # 14 days
SESSION_EXPIRE_AT_BROWSER_CLOSE = False
```

- [ ] **Step 2.7: Create `web/.env` from the example and fill in a dev secret**

```bash
cd /Users/sjordan/Code/hivesense-monitor/web
cp .env.example .env
python -c "import secrets; print(secrets.token_urlsafe(50))"
# paste output into DJANGO_SECRET_KEY= in .env
```

For the DSN, either start local Postgres on your Mac (`brew services start postgresql@16 && createuser -s combsense && createdb combsense`) or point at the LXC's Postgres for initial smoke test.

- [ ] **Step 2.8: Configure `pytest.ini`**

Create `web/pytest.ini`:

```ini
[pytest]
DJANGO_SETTINGS_MODULE = combsense.settings
python_files = test_*.py
testpaths = accounts core
addopts = --tb=short -ra
```

- [ ] **Step 2.9: Create `web/conftest.py` with a baseline fixture**

```python
import pytest


@pytest.fixture
def user_credentials():
    return {"email": "test@example.com", "password": "s3cret-pw-12345"}
```

- [ ] **Step 2.10: Create empty app skeletons for `accounts` and `core`**

So `INSTALLED_APPS` resolves on the first `manage.py check`:

```bash
mkdir -p web/accounts web/core
touch web/accounts/__init__.py web/core/__init__.py
```

Create `web/accounts/apps.py`:

```python
from django.apps import AppConfig


class AccountsConfig(AppConfig):
    default_auto_field = "django.db.models.BigAutoField"
    name = "accounts"
```

Create `web/core/apps.py`:

```python
from django.apps import AppConfig


class CoreConfig(AppConfig):
    default_auto_field = "django.db.models.BigAutoField"
    name = "core"
```

- [ ] **Step 2.11: Verify scaffold parses**

From `web/`:

```bash
.venv/bin/python manage.py check
```

Expected output:

```
System check identified no issues (0 silenced).
```

If Postgres connection fails (because local Postgres isn't running on your Mac), it's safe to skip this step — run `manage.py check --database default` against the LXC via an SSH tunnel, or just start local Postgres per Step 2.7.

- [ ] **Step 2.12: Commit**

```bash
cd /Users/sjordan/Code/hivesense-monitor
git add .gitignore web/
git commit -m "feat(web): scaffold Django project combsense with env-driven settings"
```

---

## Task 3: Create `accounts` app with custom User model (TDD)

**Files:**
- Create: `web/accounts/models.py`
- Create: `web/accounts/managers.py`
- Create: `web/accounts/admin.py`
- Create: `web/accounts/tests/__init__.py`
- Create: `web/accounts/tests/test_models.py`
- Note: `web/accounts/__init__.py` and `web/accounts/apps.py` already exist from Task 2.

- [ ] **Step 3.1: Write the failing test for the custom User model**

Create `web/accounts/tests/__init__.py` (empty) and `web/accounts/tests/test_models.py`:

```python
import pytest
from django.contrib.auth import get_user_model

pytestmark = pytest.mark.django_db


def test_user_uses_email_as_username():
    User = get_user_model()
    user = User.objects.create_user(email="alice@example.com", password="pw12345678")
    assert user.email == "alice@example.com"
    assert user.check_password("pw12345678")
    assert user.role == "beekeeper"  # default role


def test_user_email_is_required():
    User = get_user_model()
    with pytest.raises(ValueError):
        User.objects.create_user(email="", password="pw")


def test_superuser_has_admin_role():
    User = get_user_model()
    admin = User.objects.create_superuser(email="root@example.com", password="pw12345678")
    assert admin.is_superuser
    assert admin.is_staff
    assert admin.role == "admin"


def test_email_is_normalized():
    User = get_user_model()
    user = User.objects.create_user(email="Alice@EXAMPLE.com", password="pw12345678")
    assert user.email == "Alice@example.com"
```

- [ ] **Step 3.2: Run the tests to verify they fail**

From `web/`:

```bash
.venv/bin/pytest accounts/tests/test_models.py -v
```

Expected: failures because `User` model doesn't exist yet. Error will be a `LookupError` for the `accounts.User` model referenced in `AUTH_USER_MODEL`, or a NameError inside tests.

- [ ] **Step 3.3: Implement the custom UserManager**

Create `web/accounts/managers.py`:

```python
from django.contrib.auth.base_user import BaseUserManager


class UserManager(BaseUserManager):
    """Manager for a User model that uses email as the unique identifier."""

    use_in_migrations = True

    def _create_user(self, email: str, password: str | None, **extra_fields):
        if not email:
            raise ValueError("User must have an email address")
        email = self.normalize_email(email)
        user = self.model(email=email, **extra_fields)
        user.set_password(password)
        user.save(using=self._db)
        return user

    def create_user(self, email: str, password: str | None = None, **extra_fields):
        extra_fields.setdefault("is_staff", False)
        extra_fields.setdefault("is_superuser", False)
        extra_fields.setdefault("role", "beekeeper")
        return self._create_user(email, password, **extra_fields)

    def create_superuser(self, email: str, password: str | None = None, **extra_fields):
        extra_fields.setdefault("is_staff", True)
        extra_fields.setdefault("is_superuser", True)
        extra_fields.setdefault("role", "admin")
        if extra_fields.get("is_staff") is not True:
            raise ValueError("Superuser must have is_staff=True")
        if extra_fields.get("is_superuser") is not True:
            raise ValueError("Superuser must have is_superuser=True")
        return self._create_user(email, password, **extra_fields)
```

- [ ] **Step 3.4: Implement the custom User model**

Create `web/accounts/models.py`:

```python
from django.contrib.auth.models import AbstractUser
from django.db import models

from .managers import UserManager


class User(AbstractUser):
    """CombSense user. Email is the unique identifier; `username` is unused."""

    ROLE_ADMIN = "admin"
    ROLE_BEEKEEPER = "beekeeper"
    ROLE_CHOICES = [
        (ROLE_ADMIN, "Admin"),
        (ROLE_BEEKEEPER, "Beekeeper"),
    ]

    username = None
    email = models.EmailField("email address", unique=True)
    display_name = models.CharField(max_length=120, blank=True)
    role = models.CharField(max_length=16, choices=ROLE_CHOICES, default=ROLE_BEEKEEPER)

    USERNAME_FIELD = "email"
    REQUIRED_FIELDS: list[str] = []

    objects = UserManager()

    def __str__(self):
        return self.email
```

- [ ] **Step 3.5: Generate initial migration**

From `web/`:

```bash
.venv/bin/python manage.py makemigrations accounts
```

Expected output:

```
Migrations for 'accounts':
  accounts/migrations/0001_initial.py
    - Create model User
```

- [ ] **Step 3.6: Apply migrations to local Postgres**

```bash
.venv/bin/python manage.py migrate
```

Expected: all default Django apps + `accounts` migrations apply cleanly.

- [ ] **Step 3.7: Run tests to verify they pass**

```bash
.venv/bin/pytest accounts/tests/test_models.py -v
```

Expected: all 4 tests pass.

- [ ] **Step 3.8: Register `User` in Django admin**

Create `web/accounts/admin.py`:

```python
from django.contrib import admin
from django.contrib.auth.admin import UserAdmin as DjangoUserAdmin

from .models import User


@admin.register(User)
class UserAdmin(DjangoUserAdmin):
    ordering = ("email",)
    list_display = ("email", "display_name", "role", "is_active", "is_staff", "last_login")
    list_filter = ("role", "is_active", "is_staff", "is_superuser")
    search_fields = ("email", "display_name")
    fieldsets = (
        (None, {"fields": ("email", "password")}),
        ("Profile", {"fields": ("display_name", "role")}),
        ("Permissions", {"fields": ("is_active", "is_staff", "is_superuser", "groups", "user_permissions")}),
        ("Important dates", {"fields": ("last_login", "date_joined")}),
    )
    add_fieldsets = (
        (None, {
            "classes": ("wide",),
            "fields": ("email", "password1", "password2", "display_name", "role"),
        }),
    )
```

- [ ] **Step 3.9: Create a superuser for smoke test**

```bash
.venv/bin/python manage.py createsuperuser
# email: shane@linuxgangster.org
# password: <pick one>
```

Expected: "Superuser created successfully."

- [ ] **Step 3.10: Commit**

```bash
cd /Users/sjordan/Code/hivesense-monitor
git add web/accounts/
git commit -m "feat(web): custom User model with email login and role field"
```

---

## Task 4: Auth views — login / logout (TDD)

**Files:**
- Create: `web/accounts/urls.py`
- Create: `web/accounts/views.py`
- Create: `web/accounts/forms.py`
- Create: `web/templates/registration/login.html`
- Modify: `web/combsense/urls.py`
- Create: `web/accounts/tests/test_auth_views.py`

- [ ] **Step 4.1: Write the failing tests for login/logout flow**

Create `web/accounts/tests/test_auth_views.py`:

```python
import pytest
from django.contrib.auth import get_user_model
from django.urls import reverse

pytestmark = pytest.mark.django_db


@pytest.fixture
def existing_user():
    User = get_user_model()
    return User.objects.create_user(email="alice@example.com", password="pw12345678")


def test_login_page_renders(client):
    response = client.get(reverse("accounts:login"))
    assert response.status_code == 200
    assert b"email" in response.content.lower()


def test_login_with_correct_credentials_redirects_to_home(client, existing_user):
    response = client.post(
        reverse("accounts:login"),
        {"username": "alice@example.com", "password": "pw12345678"},
    )
    assert response.status_code == 302
    assert response.url == reverse("core:home")


def test_login_with_wrong_password_shows_error(client, existing_user):
    response = client.post(
        reverse("accounts:login"),
        {"username": "alice@example.com", "password": "wrong"},
    )
    assert response.status_code == 200
    assert b"correct" in response.content.lower() or b"invalid" in response.content.lower()


def test_logout_redirects_to_login(client, existing_user):
    client.force_login(existing_user)
    response = client.post(reverse("accounts:logout"))
    assert response.status_code == 302
    assert response.url == reverse("accounts:login")
```

- [ ] **Step 4.2: Run tests to verify they fail**

From `web/`:

```bash
.venv/bin/pytest accounts/tests/test_auth_views.py -v
```

Expected: `NoReverseMatch` errors for `accounts:login` and `core:home`.

- [ ] **Step 4.3: Create login form that uses email as the username field**

Create `web/accounts/forms.py`:

```python
from django.contrib.auth.forms import AuthenticationForm
from django import forms


class EmailAuthenticationForm(AuthenticationForm):
    """Login form that accepts email in place of username."""

    username = forms.EmailField(
        label="Email",
        widget=forms.EmailInput(attrs={"autofocus": True, "autocomplete": "email"}),
    )
```

- [ ] **Step 4.4: Create auth views using Django built-ins + the email form**

Create `web/accounts/views.py`:

```python
from django.contrib.auth.views import LoginView, LogoutView

from .forms import EmailAuthenticationForm


class CombSenseLoginView(LoginView):
    authentication_form = EmailAuthenticationForm
    template_name = "registration/login.html"
    redirect_authenticated_user = True


class CombSenseLogoutView(LogoutView):
    http_method_names = ["post", "options"]  # POST only, no GET logout
```

- [ ] **Step 4.5: Wire up URLs for the accounts app**

Create `web/accounts/urls.py`:

```python
from django.urls import path

from . import views

app_name = "accounts"

urlpatterns = [
    path("login/", views.CombSenseLoginView.as_view(), name="login"),
    path("logout/", views.CombSenseLogoutView.as_view(), name="logout"),
]
```

- [ ] **Step 4.6: Wire the accounts urls into the project router**

Replace `web/combsense/urls.py`:

```python
from django.contrib import admin
from django.urls import path, include

urlpatterns = [
    path("admin/", admin.site.urls),
    path("accounts/", include("accounts.urls")),
    path("", include("core.urls")),
]
```

- [ ] **Step 4.7: Create the login template**

Create `web/templates/registration/login.html`:

```html
{% extends "base.html" %}

{% block title %}Sign in — CombSense{% endblock %}

{% block content %}
<div style="max-width: 360px; margin: 60px auto;">
  <h1>Sign in</h1>

  {% if form.non_field_errors %}
    <div role="alert" style="color: #c92a2a; margin-bottom: 12px">
      {{ form.non_field_errors|join:" " }}
    </div>
  {% endif %}

  <form method="post" novalidate>
    {% csrf_token %}

    <label for="id_username">Email</label>
    {{ form.username }}
    {% for err in form.username.errors %}<div style="color:#c92a2a">{{ err }}</div>{% endfor %}

    <label for="id_password" style="margin-top: 12px; display: block;">Password</label>
    {{ form.password }}
    {% for err in form.password.errors %}<div style="color:#c92a2a">{{ err }}</div>{% endfor %}

    <button type="submit" style="margin-top: 16px;">Sign in</button>
  </form>
</div>
{% endblock %}
```

- [ ] **Step 4.8: Stub `core:home` and the base template**

The `core` app skeleton exists from Task 2. Fill in its URL conf, view, and the base template.

Create `web/core/urls.py`:

```python
from django.urls import path

from . import views

app_name = "core"

urlpatterns = [
    path("", views.home, name="home"),
]
```

Create `web/core/views.py`:

```python
from django.contrib.auth.decorators import login_required
from django.http import HttpResponse


@login_required
def home(request):
    return HttpResponse(f"Hello, {request.user.email} — placeholder home")
```

Create `web/templates/base.html` (minimal):

```html
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>{% block title %}CombSense{% endblock %}</title>
  <style>
    body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", system-ui, sans-serif;
           margin: 0; padding: 0; color: #222; background: #fafafa; }
    input[type=email], input[type=password], input[type=text] {
      width: 100%; padding: 8px 10px; box-sizing: border-box;
      border: 1px solid #ccc; border-radius: 4px; font-size: 14px;
    }
    button { padding: 8px 16px; background: #4c6ef5; color: white; border: 0;
             border-radius: 4px; cursor: pointer; font-size: 14px; }
    button:hover { background: #3b5bdb; }
    label { font-size: 12px; color: #555; display: block; margin-bottom: 4px; }
  </style>
</head>
<body>
  {% block content %}{% endblock %}
</body>
</html>
```

- [ ] **Step 4.9: Run tests — should now pass**

From `web/`:

```bash
.venv/bin/pytest accounts/tests/test_auth_views.py -v
```

Expected: all 4 tests pass.

- [ ] **Step 4.10: Manual smoke test**

```bash
.venv/bin/python manage.py runserver
```

Open `http://localhost:8000/accounts/login/`, sign in with the superuser from Task 3, expect redirect to `/` with "Hello, shane@linuxgangster.org — placeholder home".

Also visit `http://localhost:8000/admin/` — you should see the Django admin with the User model.

- [ ] **Step 4.11: Commit**

```bash
cd /Users/sjordan/Code/hivesense-monitor
git add web/accounts/ web/core/ web/templates/ web/combsense/urls.py
git commit -m "feat(web): email login, logout, and placeholder home page"
```

---

## Task 5: Password reset flow (TDD)

**Files:**
- Modify: `web/accounts/urls.py` (add 4 URL patterns)
- Create: `web/templates/registration/password_reset_form.html`
- Create: `web/templates/registration/password_reset_done.html`
- Create: `web/templates/registration/password_reset_confirm.html`
- Create: `web/templates/registration/password_reset_complete.html`
- Create: `web/templates/registration/password_reset_email.html`
- Modify: `web/combsense/settings.py` (email backend)
- Create: `web/accounts/tests/test_password_reset.py`

- [ ] **Step 5.1: Write the failing test**

Create `web/accounts/tests/test_password_reset.py`:

```python
import pytest
from django.contrib.auth import get_user_model
from django.core import mail
from django.urls import reverse

pytestmark = pytest.mark.django_db


@pytest.fixture
def existing_user():
    User = get_user_model()
    return User.objects.create_user(email="alice@example.com", password="old-pw-12345")


def test_password_reset_form_renders(client):
    response = client.get(reverse("accounts:password_reset"))
    assert response.status_code == 200


def test_password_reset_sends_email(client, existing_user):
    response = client.post(
        reverse("accounts:password_reset"),
        {"email": "alice@example.com"},
    )
    assert response.status_code == 302
    assert len(mail.outbox) == 1
    assert "alice@example.com" in mail.outbox[0].to
    assert "reset" in mail.outbox[0].subject.lower()


def test_password_reset_silent_for_unknown_email(client):
    response = client.post(
        reverse("accounts:password_reset"),
        {"email": "nobody@example.com"},
    )
    # Django silently succeeds to prevent email enumeration
    assert response.status_code == 302
    assert len(mail.outbox) == 0
```

- [ ] **Step 5.2: Run tests and verify they fail**

From `web/`:

```bash
.venv/bin/pytest accounts/tests/test_password_reset.py -v
```

Expected: `NoReverseMatch` for `accounts:password_reset`.

- [ ] **Step 5.3: Set email backend to console for dev**

In `web/combsense/settings.py`, append near the bottom (after STATIC settings):

```python
# Email — console backend in dev; SMTP configured via env in deploy (Plan D)
EMAIL_BACKEND = os.environ.get(
    "DJANGO_EMAIL_BACKEND",
    "django.core.mail.backends.console.EmailBackend",
)
DEFAULT_FROM_EMAIL = os.environ.get("DJANGO_DEFAULT_FROM_EMAIL", "noreply@combsense.local")
```

- [ ] **Step 5.4: Add password-reset URLs**

Replace `web/accounts/urls.py`:

```python
from django.contrib.auth import views as auth_views
from django.urls import path, reverse_lazy

from . import views

app_name = "accounts"

urlpatterns = [
    path("login/", views.CombSenseLoginView.as_view(), name="login"),
    path("logout/", views.CombSenseLogoutView.as_view(), name="logout"),
    path(
        "password_reset/",
        auth_views.PasswordResetView.as_view(
            template_name="registration/password_reset_form.html",
            email_template_name="registration/password_reset_email.html",
            success_url=reverse_lazy("accounts:password_reset_done"),
        ),
        name="password_reset",
    ),
    path(
        "password_reset/done/",
        auth_views.PasswordResetDoneView.as_view(
            template_name="registration/password_reset_done.html",
        ),
        name="password_reset_done",
    ),
    path(
        "reset/<uidb64>/<token>/",
        auth_views.PasswordResetConfirmView.as_view(
            template_name="registration/password_reset_confirm.html",
            success_url=reverse_lazy("accounts:password_reset_complete"),
        ),
        name="password_reset_confirm",
    ),
    path(
        "reset/done/",
        auth_views.PasswordResetCompleteView.as_view(
            template_name="registration/password_reset_complete.html",
        ),
        name="password_reset_complete",
    ),
]
```

- [ ] **Step 5.5: Create password-reset templates**

Create `web/templates/registration/password_reset_form.html`:

```html
{% extends "base.html" %}
{% block title %}Reset password — CombSense{% endblock %}
{% block content %}
<div style="max-width: 360px; margin: 60px auto;">
  <h1>Reset password</h1>
  <p>Enter your email and we'll send a reset link.</p>
  <form method="post" novalidate>
    {% csrf_token %}
    <label for="id_email">Email</label>
    {{ form.email }}
    <button type="submit" style="margin-top: 16px">Send reset link</button>
  </form>
</div>
{% endblock %}
```

Create `web/templates/registration/password_reset_done.html`:

```html
{% extends "base.html" %}
{% block title %}Check your inbox — CombSense{% endblock %}
{% block content %}
<div style="max-width: 420px; margin: 60px auto;">
  <h1>Check your inbox</h1>
  <p>If that email matches an account, a reset link is on its way.</p>
  <p><a href="{% url 'accounts:login' %}">Back to sign in</a></p>
</div>
{% endblock %}
```

Create `web/templates/registration/password_reset_confirm.html`:

```html
{% extends "base.html" %}
{% block title %}Set a new password — CombSense{% endblock %}
{% block content %}
<div style="max-width: 360px; margin: 60px auto;">
  <h1>Set a new password</h1>
  {% if validlink %}
  <form method="post" novalidate>
    {% csrf_token %}
    <label for="id_new_password1">New password</label>
    {{ form.new_password1 }}
    {% for err in form.new_password1.errors %}<div style="color:#c92a2a">{{ err }}</div>{% endfor %}
    <label for="id_new_password2" style="margin-top:12px; display:block">Confirm new password</label>
    {{ form.new_password2 }}
    {% for err in form.new_password2.errors %}<div style="color:#c92a2a">{{ err }}</div>{% endfor %}
    <button type="submit" style="margin-top:16px">Update password</button>
  </form>
  {% else %}
    <p>This reset link is invalid or has expired. <a href="{% url 'accounts:password_reset' %}">Request a new one</a>.</p>
  {% endif %}
</div>
{% endblock %}
```

Create `web/templates/registration/password_reset_complete.html`:

```html
{% extends "base.html" %}
{% block title %}Password updated — CombSense{% endblock %}
{% block content %}
<div style="max-width: 420px; margin: 60px auto;">
  <h1>Password updated</h1>
  <p>Your password has been changed. <a href="{% url 'accounts:login' %}">Sign in</a>.</p>
</div>
{% endblock %}
```

Create `web/templates/registration/password_reset_email.html`:

```html
{% autoescape off %}Hi,

Someone (hopefully you) requested a password reset for {{ email }} on CombSense.

Follow this link to set a new password:

{{ protocol }}://{{ domain }}{% url 'accounts:password_reset_confirm' uidb64=uid token=token %}

If you didn't request this, ignore this email — your password won't change.
{% endautoescape %}
```

- [ ] **Step 5.6: Run tests to verify they pass**

From `web/`:

```bash
.venv/bin/pytest accounts/tests/test_password_reset.py -v
```

Expected: all 3 tests pass. (Django's built-in PasswordResetView silently ignores unknown emails by default.)

- [ ] **Step 5.7: Manual smoke test**

```bash
.venv/bin/python manage.py runserver
```

Visit `http://localhost:8000/accounts/password_reset/`, enter `shane@linuxgangster.org`, submit. In your terminal (console email backend), you should see the email body with a reset link printed. Visit the link, set a new password, confirm sign-in works with it.

- [ ] **Step 5.8: Commit**

```bash
cd /Users/sjordan/Code/hivesense-monitor
git add web/accounts/urls.py web/templates/registration/ web/combsense/settings.py
git commit -m "feat(web): password reset flow with console email backend"
```

---

## Task 6: Home view auth guard + logout button (TDD)

**Files:**
- Create: `web/core/tests/__init__.py`
- Create: `web/core/tests/test_views.py`
- Modify: `web/core/views.py`
- Create: `web/core/templates/core/home.html`

- [ ] **Step 6.1: Write the failing tests for the home view**

Create `web/core/tests/__init__.py` (empty) and `web/core/tests/test_views.py`:

```python
import pytest
from django.contrib.auth import get_user_model
from django.urls import reverse

pytestmark = pytest.mark.django_db


@pytest.fixture
def authed_client(client):
    User = get_user_model()
    user = User.objects.create_user(email="alice@example.com", password="pw12345678")
    client.force_login(user)
    return client


def test_home_redirects_anonymous_to_login(client):
    response = client.get(reverse("core:home"))
    assert response.status_code == 302
    assert "/accounts/login/" in response.url


def test_home_renders_for_authed_user(authed_client):
    response = authed_client.get(reverse("core:home"))
    assert response.status_code == 200
    assert b"alice@example.com" in response.content


def test_home_has_logout_link(authed_client):
    response = authed_client.get(reverse("core:home"))
    assert response.status_code == 200
    assert b"Sign out" in response.content or b"Log out" in response.content
```

- [ ] **Step 6.2: Run tests to verify they fail**

From `web/`:

```bash
.venv/bin/pytest core/tests/test_views.py -v
```

Expected: first test passes (already a `@login_required` from Task 4), second test passes (HttpResponse contains the email), third test fails (no logout link yet).

- [ ] **Step 6.3: Switch home view to use a template**

Replace `web/core/views.py`:

```python
from django.contrib.auth.decorators import login_required
from django.shortcuts import render


@login_required
def home(request):
    return render(request, "core/home.html", {})
```

- [ ] **Step 6.4: Create the home template**

Create `web/core/templates/core/home.html`:

```html
{% extends "base.html" %}

{% block title %}CombSense{% endblock %}

{% block content %}
<div style="max-width: 720px; margin: 40px auto; padding: 0 20px;">
  <header style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 24px;">
    <h1 style="margin: 0">CombSense Admin</h1>
    <form method="post" action="{% url 'accounts:logout' %}" style="display:inline">
      {% csrf_token %}
      <button type="submit" style="background:#888">Sign out</button>
    </form>
  </header>

  <p>Welcome, <strong>{{ request.user.email }}</strong> (role: {{ request.user.role }}).</p>
  <p>This is the placeholder home. Real dashboard lands in Plan C.</p>

  {% if request.user.is_staff %}
    <p><a href="{% url 'admin:index' %}">Open Django admin</a> for low-level CRUD.</p>
  {% endif %}
</div>
{% endblock %}
```

- [ ] **Step 6.5: Run tests to verify they pass**

From `web/`:

```bash
.venv/bin/pytest core/tests/test_views.py -v
```

Expected: all 3 tests pass.

- [ ] **Step 6.6: Run the full suite**

```bash
.venv/bin/pytest
```

Expected: all tests across `accounts` and `core` pass (models: 4, login/logout: 4, password reset: 3, home: 3 = 14 tests).

- [ ] **Step 6.7: Commit**

```bash
cd /Users/sjordan/Code/hivesense-monitor
git add web/core/
git commit -m "feat(web): templated home view with logout button and admin link"
```

---

## Task 7: Deploy artifacts — systemd unit + env file

**Files:**
- Create: `deploy/web/combsense-web.service`
- Create: `deploy/web/combsense-web.service.d/override.conf`
- Create: `deploy/web/env.template`
- Create: `deploy/web/provision.sh`
- Create: `deploy/web/README.md`

- [ ] **Step 7.1: Create the gunicorn systemd unit**

Create `deploy/web/combsense-web.service`:

```ini
[Unit]
Description=CombSense web — Django/gunicorn
After=network-online.target postgresql.service redis-server.service
Wants=network-online.target

[Service]
Type=notify
User=combsense
Group=combsense
WorkingDirectory=/opt/combsense-web
EnvironmentFile=/etc/combsense-web/env
ExecStart=/opt/combsense-web/.venv/bin/gunicorn \
  --bind 127.0.0.1:8000 \
  --workers 3 \
  --access-logfile - \
  --error-logfile - \
  combsense.wsgi:application
Restart=on-failure
RestartSec=5s

[Install]
WantedBy=multi-user.target
```

- [ ] **Step 7.2: Create the sandboxing drop-in for unprivileged LXC**

Per memory `Unprivileged LXC Sandboxing`, Protect* / Private* directives fail with `226/NAMESPACE` in unprivileged LXCs. Create `deploy/web/combsense-web.service.d/override.conf`:

```ini
[Service]
# Disable systemd sandboxing directives that fail 226/NAMESPACE in
# unprivileged LXCs. LXC itself provides the isolation.
PrivateTmp=no
PrivateDevices=no
ProtectSystem=no
ProtectHome=no
ProtectKernelTunables=no
ProtectKernelModules=no
ProtectControlGroups=no
RestrictAddressFamilies=
RestrictNamespaces=no
```

- [ ] **Step 7.3: Create the env template for the LXC**

Create `deploy/web/env.template`:

```
# /etc/combsense-web/env  (mode 600, owned root:combsense)
# Copy this template, fill in values, chmod 600, chown root:combsense.

DJANGO_SECRET_KEY=<generate with: python -c "import secrets; print(secrets.token_urlsafe(50))">
DJANGO_DEBUG=0
DJANGO_ALLOWED_HOSTS=combsense-web,192.168.1.X

POSTGRES_DSN=postgres://combsense:<password>@127.0.0.1:5432/combsense

REDIS_URL=redis://127.0.0.1:6379/0

# Email — console for first deploy; Plan D switches to SMTP
DJANGO_EMAIL_BACKEND=django.core.mail.backends.console.EmailBackend
DJANGO_DEFAULT_FROM_EMAIL=noreply@combsense.local
```

- [ ] **Step 7.4: Create a provision script (documentation + idempotent bootstrap)**

Create `deploy/web/provision.sh`:

```bash
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
ln -snf "${INSTALL_DIR}/web/manage.py" "${INSTALL_DIR}/manage.py"
ln -snf "${INSTALL_DIR}/web/combsense" "${INSTALL_DIR}/combsense"

# Ensure /etc/combsense-web/env exists — operator must fill in
if [ ! -f /etc/combsense-web/env ]; then
  echo "!! /etc/combsense-web/env missing — copy deploy/web/env.template and fill in values"
  exit 1
fi

# Django migrate + collectstatic
cd "${INSTALL_DIR}"
sudo -u "${APP_USER}" --preserve-env=PATH \
  env $(grep -v '^#' /etc/combsense-web/env | xargs) \
  "${VENV_DIR}/bin/python" manage.py migrate --noinput

sudo -u "${APP_USER}" --preserve-env=PATH \
  env $(grep -v '^#' /etc/combsense-web/env | xargs) \
  "${VENV_DIR}/bin/python" manage.py collectstatic --noinput

# Install systemd unit if not present
if [ ! -f /etc/systemd/system/combsense-web.service ]; then
  install -m 644 "${CHECKOUT_DIR}/deploy/web/combsense-web.service" /etc/systemd/system/
  install -d /etc/systemd/system/combsense-web.service.d
  install -m 644 "${CHECKOUT_DIR}/deploy/web/combsense-web.service.d/override.conf" \
          /etc/systemd/system/combsense-web.service.d/
  systemctl daemon-reload
  systemctl enable combsense-web.service
fi

systemctl restart combsense-web.service
systemctl status combsense-web.service --no-pager
```

Make executable:

```bash
chmod +x deploy/web/provision.sh
```

- [ ] **Step 7.5: Create operator runbook**

Create `deploy/web/README.md`:

```markdown
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
cd /opt/combsense-web
sudo -u combsense env $(grep -v '^#' /etc/combsense-web/env | xargs) \
  .venv/bin/python manage.py createsuperuser
```

## Access

- Django admin: `http://<lxc-ip>:8000/admin/` (gunicorn binds 127.0.0.1 — use SSH tunnel or nginx from Plan D)
- SSH tunnel from laptop: `ssh -L 8000:127.0.0.1:8000 root@<lxc-ip>`
- Then open `http://localhost:8000/admin/` in browser

## Logs

```
journalctl -u combsense-web.service -f
```
```

- [ ] **Step 7.6: Commit**

```bash
cd /Users/sjordan/Code/hivesense-monitor
git add deploy/web/
git commit -m "feat(web): deploy artifacts — systemd unit, env template, provision script"
```

---

## Task 8: Update `.mex/` context for this project

**Files:**
- Modify: `.mex/ROUTER.md` — add web routing entry, update project state
- Modify: `.mex/context/conventions.md` — add web-specific conventions (if present)

- [ ] **Step 8.1: Add routing entry to `.mex/ROUTER.md`**

Open `.mex/ROUTER.md`. In the Routing Table section, add:

```
| Working on the web dashboard (Django) | `web/` directory + `deploy/web/` |
| `combsense-web` LXC ops              | `deploy/web/README.md`          |
```

In the Infrastructure section, add (after the `combsense-tsdb LXC` block):

```markdown
- **combsense-web LXC:** 192.168.1.X — Proxmox LXC <CTID>, NFS-backed, Debian 12 (unprivileged)
  - **Postgres 16** on 127.0.0.1:5432 (db: `combsense`, user: `combsense`)
  - **Redis 7** on 127.0.0.1:6379 (for Celery later)
  - **Django (gunicorn)** on 127.0.0.1:8000 via `combsense-web.service`
  - Credentials at `/root/.combsense-web-creds` (mode 600)
  - Systemd drop-in at `/etc/systemd/system/combsense-web.service.d/override.conf` (unprivileged LXC workaround)
```

In the Current Project State section under `### Completed`, append:

```markdown
- **Web dashboard Plan A** (`combsense-web` LXC, `web/`, `deploy/web/`)
  - Django 5 project with Postgres backend
  - Custom `accounts.User` model (email login, `role` field)
  - Login / logout / password reset flows
  - Django admin wired for superuser CRUD
  - Systemd unit + provision script on new unprivileged LXC
```

Also update the `last_updated` field at the top of ROUTER.md to today's date.

- [ ] **Step 8.2: Run the full test suite one last time**

From `web/`:

```bash
.venv/bin/pytest
```

Expected: 14 tests pass, no warnings about deprecations you can act on.

- [ ] **Step 8.3: Commit**

```bash
cd /Users/sjordan/Code/hivesense-monitor
git add .mex/ROUTER.md
git commit -m "docs(mex): record combsense-web LXC and Plan A completion"
```

---

## Definition of Done for Plan A

- [ ] `combsense-web` LXC provisioned, Postgres + Redis running, IP recorded
- [ ] `web/` Django project committed with custom User model
- [ ] Login / logout / password reset flows tested and working via `manage.py runserver`
- [ ] Django admin accessible for superuser
- [ ] `deploy/web/provision.sh` deploys cleanly to the LXC; `systemctl status combsense-web` shows active
- [ ] SSH tunnel to LXC gunicorn lets you reach Django admin
- [ ] `pytest` green (14 tests across `accounts` and `core`)
- [ ] `.mex/ROUTER.md` updated with new infrastructure + completion note

---

## What's next

**Plan B — Ingest + hive readings.** Adds the MQTT subscriber service (auto-claim `Device` rows from unknown `sensor_id`s), the Influx query client with bucket resolution, and the hive detail Readings tab with Chart.js charts. Depends on:
- Data model from this plan extended with `Yard`, `Hive`, `Device`, `AuditEvent` tables
- A superuser + at least one `User` row exists in Postgres
- MQTT broker at `192.168.1.82:1883` accessible from the `combsense-web` LXC (same LAN — no config change)
