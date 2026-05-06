# dooard security provisioning

This directory documents Flash Encryption and Secure Boot v2 provisioning for
release devices. Key material generated here must not be committed. The
repository `.gitignore` excludes `*.bin`, `*.pem`, `*.key`, encrypted outputs,
and Secure Boot digests under this directory.

## Stored credentials

Runtime credentials are stored in ESP32 NVS through `CredentialStore`.

- NVS namespace: `dooard-creds`
- NVS keys: `wifi_ssid`, `wifi_password`, `api_key`, `device_id`,
  `endpoint_url`, `configured`, `schema_version`, `checksum`
- Current schema version: `1`
- Checksum: FNV-1a over schema, configured flag, and every credential field

Flash Encryption protects the whole NVS partition at rest. The existing
`default_8MB.csv` partition table already includes the 24 KB `nvs` partition at
`0x9000`, so no partition change is required for this ticket.

## Key files

Use these paths when provisioning a release device:

```text
security/flash_encryption_key.bin
security/secure_boot_key.pem
security/secure_boot_digest.bin
```

Back up `flash_encryption_key.bin` and `secure_boot_key.pem` before burning any
eFuse. Losing the Flash Encryption key prevents host-side encrypted reflashing
for that device. Losing the Secure Boot private key prevents signing firmware
that the device will accept.

## Generate keys

```sh
pio pkg exec --package "platformio/tool-esptoolpy" -- \
  espsecure.py generate_flash_encryption_key security/flash_encryption_key.bin

pio pkg exec --package "platformio/tool-esptoolpy" -- \
  espsecure.py generate_signing_key --version 2 --scheme rsa3072 \
  security/secure_boot_key.pem

pio pkg exec --package "platformio/tool-esptoolpy" -- \
  espsecure.py digest_sbv2_public_key \
  --keyfile security/secure_boot_key.pem \
  --output security/secure_boot_digest.bin
```

## Release provisioning

Set the target port first:

```sh
export PORT=/dev/ttyUSB0
```

Burn the Flash Encryption key and release-mode eFuses:

```sh
pio pkg exec --package "platformio/tool-esptoolpy" -- \
  espefuse.py --port "$PORT" --chip esp32 \
  burn_key flash_encryption security/flash_encryption_key.bin

pio pkg exec --package "platformio/tool-esptoolpy" -- \
  espefuse.py --port "$PORT" --chip esp32 burn_efuse FLASH_CRYPT_CNT 127

pio pkg exec --package "platformio/tool-esptoolpy" -- \
  espefuse.py --port "$PORT" --chip esp32 burn_efuse FLASH_CRYPT_CONFIG 0xF
```

Burn the Secure Boot v2 digest and enable Secure Boot:

```sh
pio pkg exec --package "platformio/tool-esptoolpy" -- \
  espefuse.py --port "$PORT" --chip esp32 \
  burn_key secure_boot_v2 security/secure_boot_digest.bin

pio pkg exec --package "platformio/tool-esptoolpy" -- \
  espefuse.py --port "$PORT" --chip esp32 burn_efuse ABS_DONE_1
```

For production, burn the remaining lock-down eFuses only after a full recovery
test on a sacrificial device:

```sh
pio pkg exec --package "platformio/tool-esptoolpy" -- \
  espefuse.py --port "$PORT" --chip esp32 burn_efuse DISABLE_DL_ENCRYPT 1
pio pkg exec --package "platformio/tool-esptoolpy" -- \
  espefuse.py --port "$PORT" --chip esp32 burn_efuse DISABLE_DL_DECRYPT 1
pio pkg exec --package "platformio/tool-esptoolpy" -- \
  espefuse.py --port "$PORT" --chip esp32 burn_efuse DISABLE_DL_CACHE 1
pio pkg exec --package "platformio/tool-esptoolpy" -- \
  espefuse.py --port "$PORT" --chip esp32 burn_efuse JTAG_DISABLE 1
```

Do not burn `UART_DOWNLOAD_DIS` until all other eFuse operations are complete.

## Encrypted flashing

Build the secure release environment:

```sh
pio run -e core2_secure
```

After Flash Encryption is enabled, serial flashing must write encrypted data.
Use PlatformIO's encrypted target when available:

```sh
pio run -e core2_secure -t encrypt -t upload
```

If using `esptool.py` directly, pass the built artifacts and `--encrypt`:

```sh
pio pkg exec --package "platformio/tool-esptoolpy" -- \
  esptool.py --chip esp32 --port "$PORT" write_flash --encrypt \
  0x1000 .pio/build/core2_secure/bootloader.bin \
  0x8000 .pio/build/core2_secure/partitions.bin \
  0x10000 .pio/build/core2_secure/firmware.bin
```

OTA updates remain the preferred update path after release provisioning because
the running firmware writes the new app partition through the ESP32 flash
encryption path.

## Readout validation

Capture encrypted and unencrypted comparison dumps from devices with the same
serial setup values:

```sh
pio pkg exec --package "platformio/tool-esptoolpy" -- \
  esptool.py --chip esp32 --port "$PORT" read_flash 0x9000 0x6000 \
  security/nvs-encrypted-readback.bin
```

On an unencrypted development device, the same command should show recognizable
NVS strings such as the test SSID when inspected with `strings`. On a release
device with Flash Encryption enabled, `strings security/nvs-encrypted-readback.bin`
must not reveal the SSID, password, API key, device ID, or endpoint URL.

## References

- Espressif Flash Encryption guide:
  <https://docs.espressif.com/projects/esp-idf/en/latest/esp32/security/flash-encryption.html>
- Espressif Secure Boot v2 guide:
  <https://docs.espressif.com/projects/esp-idf/en/latest/esp32/security/secure-boot-v2.html>
- PlatformIO ESP-IDF security features:
  <https://docs.platformio.org/en/latest/frameworks/espidf.html#security-features>
