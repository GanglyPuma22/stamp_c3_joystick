"""PlatformIO extra script: Wi-Fi (OTA) uploads with the same command as cable uploads.

    pio run -e joystick_light -t upload --upload-port COM3          # USB cable
    pio run -e joystick_light -t upload --upload-port 10.0.0.50     # Wi-Fi

When the upload port is an IP address or a .local name, this selects the espota
uploader and passes it OTA_PASSWORD from include/credentials.h, so the password
lives in one place and never in platformio.ini.

Registered as both a pre: and a post: script. The protocol has to be chosen
before the platform configures the uploader; the password can only be added
after it has.
"""
import os
import re

Import("env")  # noqa: F821 - provided by PlatformIO

port = env.subst("$UPLOAD_PORT")  # noqa: F821
is_network = bool(re.match(r"^(\d{1,3}\.){3}\d{1,3}$|^[^\\/]+\.local$", port))

if is_network:
    env.Replace(UPLOAD_PROTOCOL="espota")  # noqa: F821
    if "espota" in str(env.get("UPLOADER", "")):  # noqa: F821 - post stage only
        credentials = os.path.join(env.subst("$PROJECT_DIR"), "include", "credentials.h")  # noqa: F821
        password = None
        if os.path.isfile(credentials):
            with open(credentials, encoding="utf-8") as handle:
                match = re.search(r'^\s*#define\s+OTA_PASSWORD\s+"((?:[^"\\]|\\.)*)"', handle.read(), re.M)
            password = match.group(1) if match else None
        if password:
            env.Append(UPLOADERFLAGS=["--auth=" + password])  # noqa: F821
        else:
            print("pio_ota: no OTA_PASSWORD found in include/credentials.h; the board will reject this upload.")
