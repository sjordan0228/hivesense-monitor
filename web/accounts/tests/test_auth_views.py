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


def test_login_redirects_to_next_after_success(client, django_user_model):
    django_user_model.objects.create_user(email="bob@example.com", password="pw12345678")
    target = "/some-protected/"
    response = client.post(
        f"/accounts/login/?next={target}",
        {"username": "bob@example.com", "password": "pw12345678", "next": target},
    )
    assert response.status_code == 302
    assert response.url == target
