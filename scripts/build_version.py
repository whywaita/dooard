import os

Import("env")

version = os.environ.get("DOOARD_FIRMWARE_VERSION", "0.1.0")
version = version.replace("\\", "").replace('"', "")
env.Append(CPPDEFINES=[("DOOARD_FIRMWARE_VERSION", f'\\"{version}\\"')])
