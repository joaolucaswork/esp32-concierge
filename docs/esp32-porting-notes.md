# Porting zclaw to ESP32 (base/classic)

Notes on adapting the firmware — originally targeting ESP32-C3/S3/C6 — to run on the classic ESP32-D0WD-V3.

## Changes Required

### 1. UART channel instead of USB Serial/JTAG

The classic ESP32 lacks a native USB Serial/JTAG peripheral. The local serial channel must use UART0.

Set in `sdkconfig.defaults`:

```
CONFIG_ZCLAW_CHANNEL_UART=y
```

### 2. TLS memory allocation failure (`mbedtls_ssl_setup returned -0x7F00`)

The ESP32 has ~320 KB of usable RAM (vs ~400 KB on C3/S3). With static TLS buffers (16 KB in + 4 KB out), the heap runs out during HTTPS handshake — especially when WiFi, Telegram polling, and LLM requests run concurrently.

Fix: enable dynamic mbedTLS buffers so memory is allocated on-demand and freed after each connection:

```
CONFIG_MBEDTLS_DYNAMIC_BUFFER=y
CONFIG_MBEDTLS_DYNAMIC_FREE_CONFIG_DATA=y
CONFIG_MBEDTLS_DYNAMIC_FREE_CA_CERT=y
```

### 3. API key buffer too small

OpenAI project API keys (`sk-proj-...`) can be ~164 characters, exceeding the original 128-byte `s_api_key` buffer in `llm.c`. The `auth_header` buffer for `"Bearer <key>"` was also too small.

Changes in `main/llm.c`:
- `s_api_key`: 128 → 256 bytes
- `auth_header`: 150 → 270 bytes

### 4. `PRId64` broken under nano newlib (`CONFIG_NEWLIB_NANO_FORMAT=y`)

This was the most subtle bug. The nano newlib printf (enabled for firmware size reduction) does **not** support `%lld` / `PRId64`. All `snprintf` calls formatting `int64_t` values silently produce garbage output.

This affected Telegram critically:
- `getUpdates?offset=<garbage>` — every poll returned ALL pending messages
- The bot reprocessed the same messages in an infinite loop
- Log output for update IDs and chat IDs showed corrupted values

Fix: added `i64_to_str()` helper in `telegram.c` that converts `int64_t` to string via manual digit extraction, then used `%s` with this helper instead of `PRId64` in all `snprintf` and `ESP_LOGI` calls.

**Why not just disable nano format?** It saves ~20 KB of firmware, which matters for the 888 KB budget.

### 5. Telegram message replay on reboot

On every boot, `s_last_update_id` starts at 0, so `getUpdates?offset=1` returns all unconfirmed messages. Combined with bug #4, this caused an infinite loop of replaying old `/start` and user messages.

Fix: added `telegram_flush_pending()` that runs once at startup before the polling loop:
1. Calls `getUpdates?offset=-1&limit=1` to get the last pending update ID
2. Calls `getUpdates?offset=<last_id+1>` to confirm/acknowledge all pending updates
3. Sets `s_last_update_id` so the polling loop starts fresh

### 6. Boot guard / safe mode loop

The firmware increments `boot_count` in NVS on each boot and enters safe mode after 3 consecutive failures (`MAX_BOOT_FAILURES`). The counter is cleared after 30 seconds of stable operation (`BOOT_SUCCESS_DELAY_MS`).

During development, repeated flash cycles where the device crashed before the 30s window caused `boot_count` to accumulate. The device would enter safe mode on the next boot, requiring manual NVS reset.

Workaround: reset `boot_count` to 0 in NVS after flashing during development. The `scripts/add-telegram.sh` script and direct NVS writes via `parttool.py` can handle this.

## Summary of Modified Files

| File | Change |
|------|--------|
| `sdkconfig.defaults` | Added `ZCLAW_CHANNEL_UART`, dynamic mbedTLS buffers |
| `main/llm.c` | Increased `s_api_key` (256) and `auth_header` (270) buffers |
| `main/telegram.c` | Added `i64_to_str()` helper, replaced all `PRId64` usage, added `telegram_flush_pending()` startup flush |
| `scripts/add-telegram.sh` | New script to add Telegram credentials without full re-provisioning |
