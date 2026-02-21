# zclaw

Field Manual

- [Chapter 0 · Overview](https://zclaw.dev/index.html)
- [Chapter 1 · Getting Started](https://zclaw.dev/getting-started.html)
- [Chapter 2 · Tool Surface](https://zclaw.dev/tools.html)
- [Chapter 3 · Runtime Anatomy](https://zclaw.dev/architecture.html)
- [Chapter 4 · Security & Ops](https://zclaw.dev/security.html)
- [Chapter 5 · Build Your Own Tool](https://zclaw.dev/build-your-own-tool.html)

K&RDayDusk

[README (good for agents)](https://zclaw.dev/reference/README_COMPLETE.md) Shortcuts [GitHub Repository](https://github.com/tnm/zclaw)

zclaw docs

☰

K&RDayDusk

Keys

chapter 1

# Getting Started

Fast path from blank machine to a live ESP32 agent. Defaults are safe, and explicit flags keep runs repeatable.

## Basic Hardware

- Tested targets: **ESP32-C3**, **ESP32-S3**, and **ESP32-C6**.
- Recommended starter board: Seeed XIAO ESP32-C3 (USB-C, small footprint, low cost).
- Use a real data USB cable (not charge-only), then connect board to host.
- zclaw's setup scripts will generally find the right serial port without you having to do anything.

## One-Line Bootstrap

```
bash <(curl -fsSL https://raw.githubusercontent.com/tnm/zclaw/main/scripts/bootstrap.sh)
```

Bootstrap clones or updates the repo, then runs `./install.sh`.

- Installer remembers answers in `~/.config/zclaw/install.env` (disable with `--no-remember`).
- Interactive flashing defaults to standard mode; encrypted flashing is opt-in with `--flash-mode secure`.

Want non-interactive install? Use `-y` and explicit no/yes flags: `./install.sh -y --build --flash --provision --no-qemu --no-cjson`.

## Common Install Patterns

```
# Interactive default path
./install.sh

# Non-interactive provisioning path
./install.sh -y --build --flash --provision --monitor

# Explicit secure flash mode
./install.sh -y --build --flash --flash-mode secure
```

## First Boot Flow

1. Flash firmware (`./scripts/flash.sh` or `./scripts/flash-secure.sh`). Flash scripts auto-detect chip target and can prompt for `idf.py set-target <chip>` on mismatch.
2. Provision credentials (`./scripts/provision.sh --port <serial-port>`).
3. Enter WiFi SSID, LLM provider, and API key. Optional Telegram fields can be filled now or later.
4. Reboot and inspect logs with `./scripts/monitor.sh`.

## Telegram Path

1. Create bot via [@BotFather](https://t.me/botfather).
2. Get chat ID via [@userinfobot](https://t.me/userinfobot).
3. Set `--tg-token` and `--tg-chat-id` in provisioning.

Runtime accepts messages only from the configured chat ID.

## Web Relay Path

```
# Device connected
./scripts/web-relay.sh --serial-port /dev/cu.usbmodem1101 --host 0.0.0.0 --port 8787

# No hardware (mock agent)
./scripts/web-relay.sh --mock-agent --host 0.0.0.0 --port 8787
```

Only one process should own the serial port at a time.

```
# No-clone bootstrap path
bash <(curl -fsSL https://raw.githubusercontent.com/tnm/zclaw/main/scripts/bootstrap-web-relay.sh) -- --serial-port /dev/cu.usbmodem1101 --host 0.0.0.0 --port 8787
```

## Serial Port Conflicts

```
# Release ESP-IDF monitor/holders
./scripts/release-port.sh

# Relay helper can stop monitor holders automatically
./scripts/web-relay.sh --serial-port /dev/cu.usbmodem1101 --kill-monitor --host 0.0.0.0 --port 8787
```

## Advanced Board Config

```
source ~/esp/esp-idf/export.sh
idf.py menuconfig
```

Adjust GPIO safety range/allowlist under `zclaw Configuration -> GPIO Tool Safety`.

## If Something Breaks

```
# ESP-IDF repair
cd ~/esp/esp-idf
./install.sh esp32c3,esp32s3

# Build/test routines
./scripts/build.sh
./scripts/test.sh host
```

Next chapter: [tool surface and schedule grammar](https://zclaw.dev/tools.html).