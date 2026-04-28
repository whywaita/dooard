from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_platformio_target():
    platformio_ini = (ROOT / "platformio.ini").read_text()
    assert "board = m5stack-core2" in platformio_ini
    assert "framework = arduino" in platformio_ini
    assert "m5stack/M5Unified" in platformio_ini


def test_secrets_example_has_required_keys():
    secrets = (ROOT / "include" / "secrets.example.h").read_text()
    assert "#define WIFI_SSID" in secrets
    assert "#define WIFI_PASSWORD" in secrets
    assert "#define WEATHER_LATITUDE" in secrets
    assert "#define WEATHER_LONGITUDE" in secrets
    assert "#define WEATHER_LABEL" in secrets
