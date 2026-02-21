# zclaw

Field Manual

- [Chapter 0 · Overview](https://zclaw.dev/index.html)
- [Chapter 1 · Getting Started](https://zclaw.dev/getting-started.html)
- [Chapter 2 · Tool Surface](https://zclaw.dev/tools.html)
- [Chapter 3 · Runtime Anatomy](https://zclaw.dev/architecture.html)
- [Chapter 4 · Security & Ops](https://zclaw.dev/security.html)
- [Chapter 5 · Build Your Own Tool](https://zclaw.dev/build-your-own-tool.html)

Practical notes for building, flashing, and operating zclaw in the field.

K&RDayDusk

[README (good for agents)](https://zclaw.dev/reference/README_COMPLETE.md) Shortcuts [GitHub Repository](https://github.com/tnm/zclaw)

zclaw docs

☰

K&RDayDusk

Keys

chapter 0

# The 888 KB Assistant

zclaw is an ESP32-resident AI agent written in C. It runs as a practical assistant over Telegram or host relay, with scheduling, GPIO control, memory, and a tight firmware budget.

### Enjoy with zclaw

- "Remind me in 20 minutes."
- "Water the plants every day at 8:15."
- "Set GPIO 5 high."
- "Remember that my office sensor is on GPIO 4."

You send plain language, zclaw maps to tool calls, firmware executes on silicon.

```
You: In 20 minutes, check the garage sensor
Agent: Created schedule #7: once in 20 min -> check the garage sensor
```

![Lobster soldering a Seeed Studio XIAO ESP32-C3](https://zclaw.dev/images/lobster_xiao_cropped_left.png)Tested targets: ESP32-C3, ESP32-S3, and ESP32-C6. Other ESP32 variants should work fine.

## Read This Manual In Order

[**Chapter 1 · Getting Started** Bootstrap install, flash, provision, and first successful boot.](https://zclaw.dev/getting-started.html) [**Chapter 2 · Tool Surface** Current built-in tools and scheduling behavior, including one-shot jobs.](https://zclaw.dev/tools.html) [**Chapter 3 · Runtime Anatomy** Task model, queues, LLM path, and practical constraints.](https://zclaw.dev/architecture.html) [**Chapter 4 · Security & Ops** Safety defaults, flash encryption, and production handling guidance.](https://zclaw.dev/security.html) [**Chapter 5 · Build Your Own Tool** Design, create, validate, and maintain custom natural-language tools.](https://zclaw.dev/build-your-own-tool.html)

## Project Character

- **Language/runtime:** C + ESP-IDF + FreeRTOS.
- **LLM backends:** Anthropic, OpenAI, OpenRouter.
- **Interface:** Telegram and optional host web relay.
- **Philosophy:** ship useful automation under strict resource bounds.

Source of truth remains code + root README. This site is the human-facing layer built for readability.