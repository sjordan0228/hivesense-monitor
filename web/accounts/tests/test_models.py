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
