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

chapter 3

# Runtime Anatomy

The firmware runs as cooperating FreeRTOS tasks with queue handoff between input channels, agent loop, and schedule subsystem.

## Task Layout

```
channel_read_task  -> input_queue -> agent_task -> channel/telegram output queues
telegram_poll_task -> input_queue
cron_task          -> input_queue
```

The agent loop is the decision engine and currently processes one inbound message at a time.

## Message Lifecycle

1. Inbound text arrives from serial, Telegram, or cron action.
2. Agent appends user message to rolling history buffer.
3. Agent builds request JSON and calls LLM backend.
4. If model returns tool call, firmware executes tool handler and loops for next model step.
5. Final assistant text is queued to channel and optionally Telegram.

## Practical Constraints

- Bounded buffers for request/response and tool results.
- Retry with backoff on transient LLM failures.
- Queue depth limits can drop work under sustained backlog.
- Scheduler checks every minute by default.

## Architecture Sketch

```
┌─────────────────────────────────────────────────┐
│ app_main                                        │
│ WiFi/NTP init + task startup                    │
└─────────────────────────────────────────────────┘
      │                  │                   │
      ▼                  ▼                   ▼
┌───────────────┐  ┌───────────────┐  ┌───────────────┐
│ channel.c     │  │ telegram.c    │  │ cron.c        │
│ serial IO     │  │ bot polling   │  │ schedule fire │
└───────┬───────┘  └───────┬───────┘  └───────┬───────┘
        └──────────────┬───┴───────────────┬──┘
                       ▼                   ▼
                    input queue        time/NVS
                       │
                       ▼
                 ┌───────────────┐
                 │ agent.c       │
                 │ tool loop     │
                 └───────┬───────┘
                         ▼
                 ┌───────────────┐
                 │ llm.c + tools │
                 └───────────────┘
```

## Where To Dig Next

- `main/agent.c` for conversation loop and retry behavior.
- `main/llm.c` for backend transport paths.
- `main/cron.c` for periodic/daily/once scheduling logic.

Next chapter: [credential handling and flash encryption modes](https://zclaw.dev/security.html).