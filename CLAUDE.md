# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**zclaw** — an AI personal assistant for ESP32 microcontrollers written in C. Runs on ESP-IDF v5.4 with FreeRTOS. Firmware budget: **≤ 888 KB**. Targets ESP32-C3, ESP32-S3, ESP32-C6.

Supports Anthropic, OpenAI, and OpenRouter LLM backends. Input via Telegram, serial/USB, or web relay.

## Build & Development Commands

```bash
# Build
./scripts/build.sh                    # Standard build
./scripts/build.sh --pad-to-888kb     # Build padded to 888 KB budget

# Flash & Monitor
./scripts/flash.sh                    # Flash to device
./scripts/monitor.sh                  # Serial monitor

# Test
./scripts/test.sh all                 # All tests (host + device simulation)
./scripts/test.sh host                # Host tests only (ASAN enabled)
ASAN=0 ./scripts/test.sh host         # Host tests without AddressSanitizer

# Setup & Provisioning
./install.sh                          # Full interactive setup
./scripts/provision.sh                # Configure WiFi/LLM credentials via NVS

# Utilities
./scripts/size.sh                     # Check binary size
./scripts/clean.sh                    # Clean build artifacts
./scripts/emulate.sh                  # Run in QEMU emulator
./scripts/web-relay.sh                # Start web relay chat UI
```

## Architecture

All source lives in `main/`. Key modules:

- **agent.c** — Core AI reasoning loop. Receives messages from input queue, builds LLM requests, executes tool calls in a loop (max 5 iterations), sends responses to output queues.
- **llm.c** — HTTP transport abstraction across Anthropic/OpenAI/OpenRouter. Handles vendor-specific JSON formats, retry with backoff.
- **tools.c / tools_*.c** — Tool registry and handlers (GPIO, I2C, memory, cron, system, user-defined tools). Each tool follows the `tool_execute_fn` pattern returning bool with result written to a fixed buffer.
- **channel.c** — Serial/USB input/output via FreeRTOS queues.
- **telegram.c** — Telegram bot polling over HTTPS.
- **cron.c / cron_utils.c** — Scheduled task engine (periodic, daily, one-shot).
- **memory.c / memory_keys.c** — NVS flash storage abstraction.
- **config.h** — All compile-time constants (buffer sizes, task priorities, rate limits, LLM defaults).
- **Kconfig.projbuild** — ESP-IDF menuconfig options (stub modes for QEMU, GPIO safety config).

### Task Flow (FreeRTOS)

```
channel_read_task ──→ input_queue → agent_task → output queues → channel/telegram
telegram_poll_task ─↗                          ↗
cron_task ──────────↗
```

### Tool Pattern

```c
typedef bool (*tool_execute_fn)(const cJSON *input, char *result, size_t result_len);
```

Tools return bool success, write result string to a 512-byte buffer. All tools are registered in `tools.c`.

## Resource Constraints

This is an embedded project with hard limits:

- **Firmware size**: ≤ 888 KB (enforced by CI)
- **LLM request buffer**: 12 KB, response buffer: 16 KB
- **Tool result buffer**: 512 bytes
- **Conversation history**: 12 turns max, 1024 bytes per message
- **Task stacks**: Agent 8 KB, Channel 4 KB, Cron 4 KB
- **Rate limits**: 30 req/hour, 200 req/day (configurable in config.h)

Prefer static allocation. No dynamic malloc in hot paths. Size optimization (`-Os`) is on.

## NVS Key Namespaces

- `u_*` — User-defined memory (settable via tools)
- `tc_*` — Telegram config
- `cc_*` — LLM/Claude config
- `cron_*` — Scheduled tasks
- `tz_*` — Timezone config

NVS keys have a 15-byte limit.

## GPIO Safety

GPIO operations are bounded by configurable min/max pin range (default 2–10) with optional allowlist override via CSV. All GPIO tool calls validate against these bounds.

## Testing

Host tests live in `test/host/` with mocks (`mock_llm.c`, `mock_tools.c`, `mock_esp.c`). AddressSanitizer is enabled by default. CI runs firmware size guard, stack usage guard, target matrix builds, and host tests.
