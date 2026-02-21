#!/usr/bin/env python3
"""Unit tests for web_relay helpers."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path


TEST_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = TEST_DIR.parent.parent
sys.path.insert(0, str(PROJECT_ROOT / "scripts"))

from web_relay import (  # noqa: E402
    MockAgentBridge,
    SerialAgentBridge,
    create_agent_bridge,
    describe_serial_exception,
    is_probable_serial_exception,
    is_probable_esp_log_line,
    is_request_authorized,
    normalize_api_key,
    resolve_serial_port,
)


class WebRelayTests(unittest.TestCase):
    def test_normalize_api_key(self) -> None:
        self.assertIsNone(normalize_api_key(None))
        self.assertIsNone(normalize_api_key("   "))
        self.assertEqual(normalize_api_key("  abc  "), "abc")

    def test_is_request_authorized(self) -> None:
        self.assertTrue(is_request_authorized(None, None))
        self.assertFalse(is_request_authorized(None, "secret"))
        self.assertFalse(is_request_authorized("bad", "secret"))
        self.assertTrue(is_request_authorized("secret", "secret"))

    def test_log_line_classifier(self) -> None:
        self.assertTrue(is_probable_esp_log_line("I (12) main: hello"))
        self.assertTrue(is_probable_esp_log_line("ets Jun  8 2016 00:22:57"))
        self.assertFalse(is_probable_esp_log_line("assistant reply text"))

    def test_serial_error_classifier(self) -> None:
        self.assertTrue(
            is_probable_serial_exception(
                Exception(
                    "device reports readiness to read but returned no data "
                    "(device disconnected or multiple access on port?)"
                )
            )
        )
        self.assertFalse(is_probable_serial_exception(Exception("unexpected parsing failure")))

    def test_describe_serial_exception_busy(self) -> None:
        error = Exception("resource busy")
        text = describe_serial_exception("/dev/cu.usbmodem1101", error)
        self.assertIn("appears busy", text)
        self.assertIn("/dev/cu.usbmodem1101", text)

    def test_serial_bridge_wraps_serial_like_error(self) -> None:
        class FailingSerial:
            def reset_input_buffer(self) -> None:
                return

            def read(self, size: int) -> bytes:
                return b""

            def write(self, payload: bytes) -> int:
                raise Exception(
                    "device reports readiness to read but returned no data "
                    "(device disconnected or multiple access on port?)"
                )

            def flush(self) -> None:
                return

        bridge = SerialAgentBridge(
            port="/dev/cu.usbmodem1101",
            baudrate=115200,
            serial_timeout_s=0.1,
            response_timeout_s=1.0,
            idle_timeout_s=0.2,
            log_serial=False,
        )
        bridge._serial = FailingSerial()

        with self.assertRaises(RuntimeError) as ctx:
            bridge.ask("hello")
        self.assertIn("appears busy", str(ctx.exception))

    def test_mock_bridge_commands(self) -> None:
        bridge = MockAgentBridge(latency_s=0.0)
        self.assertEqual(bridge.ask("ping"), "pong")
        status = bridge.ask("status")
        self.assertIn("mock-agent online", status)

    def test_create_agent_bridge_mock(self) -> None:
        class Args:
            mock_agent = True
            mock_latency = 0.0
            serial_port = None
            baud = 115200
            serial_timeout = 0.15
            response_timeout = 90.0
            idle_timeout = 1.2
            log_serial = False

        bridge, target = create_agent_bridge(Args())
        self.assertIsInstance(bridge, MockAgentBridge)
        self.assertEqual(target, "mock-agent")

    def test_resolve_serial_port_returns_explicit(self) -> None:
        self.assertEqual(resolve_serial_port("/dev/ttyTEST0"), "/dev/ttyTEST0")


if __name__ == "__main__":
    unittest.main()
