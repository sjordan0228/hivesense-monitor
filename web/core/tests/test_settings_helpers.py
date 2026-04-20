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
