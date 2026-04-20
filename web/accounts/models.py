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
