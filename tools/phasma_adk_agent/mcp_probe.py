#!/usr/bin/env python3
"""Probe PhasmaEditor's loopback MCP HTTP endpoint."""

from __future__ import annotations

import argparse
import ipaddress
import json
import sys
import urllib.error
import urllib.parse
import urllib.request

DEFAULT_MCP_URL = "http://127.0.0.1:8765/mcp"
PROTOCOL_VERSION = "2025-06-18"
PHASE1_TOOLS = ("query_scene", "take_screenshot", "get_console_log")
MUTATING_TOOLS = (
    "write_project_file",
    "patch_project_file",
    "execute_lua",
    "inject_mouse_input",
)


class McpProbeError(RuntimeError):
    """Raised when the probe cannot complete an MCP request."""


class NoRedirectHandler(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def validate_mcp_url(url: str) -> str:
    parsed = urllib.parse.urlsplit(url)
    try:
        host = parsed.hostname
        loopback = host is not None and ipaddress.ip_address(host).is_loopback
        parsed.port
    except ValueError as exc:
        raise ValueError(f"Invalid MCP URL: {url}") from exc
    if (
        parsed.scheme != "http"
        or not loopback
        or parsed.username is not None
        or parsed.password is not None
        or parsed.query
        or parsed.fragment
    ):
        raise ValueError("MCP URL must be an HTTP loopback endpoint without credentials, query, or fragment")
    return parsed.geturl()


class McpClient:
    def __init__(self, url: str, timeout: float) -> None:
        self.url = validate_mcp_url(url)
        self.timeout = timeout
        self._opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), NoRedirectHandler)
        self._next_id = 1
        self.protocol_version: str | None = None
        self.session_id: str | None = None

    def _headers(self) -> dict[str, str]:
        headers = {
            "Accept": "application/json, text/event-stream",
            "Content-Type": "application/json",
            "User-Agent": "phasma-mcp-probe/0.1",
        }
        if self.protocol_version:
            headers["MCP-Protocol-Version"] = self.protocol_version
        if self.session_id:
            headers["Mcp-Session-Id"] = self.session_id
        return headers

    @staticmethod
    def _header(response: urllib.response.addinfourl, name: str) -> str | None:
        headers = response.headers
        value = headers.get(name)
        if value:
            return value
        lowered = name.lower()
        for key, header_value in headers.items():
            if key.lower() == lowered:
                return header_value
        return None

    @staticmethod
    def _parse_sse_messages(body: str) -> list[dict]:
        messages: list[dict] = []
        data_lines: list[str] = []
        for raw_line in body.splitlines():
            line = raw_line.rstrip("\r")
            if not line:
                if data_lines:
                    data = "\n".join(data_lines)
                    data_lines = []
                    try:
                        messages.append(json.loads(data))
                    except json.JSONDecodeError:
                        continue
                continue
            if line.startswith("data:"):
                data_lines.append(line[5:].lstrip())
        if data_lines:
            try:
                messages.append(json.loads("\n".join(data_lines)))
            except json.JSONDecodeError:
                pass
        return messages

    def _decode_response(self, body: str, content_type: str, method: str, request_id: int | None) -> dict:
        if not body:
            return {}

        if content_type.split(";", 1)[0].strip().lower() == "text/event-stream":
            messages = self._parse_sse_messages(body)
            if request_id is not None:
                for message in messages:
                    if message.get("id") == request_id:
                        if "error" in message:
                            raise McpProbeError(f"{method} failed: {message['error']}")
                        return message.get("result", {})
            for message in reversed(messages):
                if "result" in message or "error" in message:
                    if "error" in message:
                        raise McpProbeError(f"{method} failed: {message['error']}")
                    return message.get("result", {})
            raise McpProbeError(f"No JSON-RPC response found in SSE stream for {method}")

        try:
            decoded = json.loads(body)
        except json.JSONDecodeError as exc:
            raise McpProbeError(f"Invalid JSON response for {method}: {body!r}") from exc

        if "error" in decoded:
            raise McpProbeError(f"{method} failed: {decoded['error']}")
        return decoded.get("result", {})

    def _post(self, payload: dict, request_id: int | None) -> dict:
        payload = {
            key: value
            for key, value in payload.items()
            if value is not None
        }

        body = json.dumps(payload).encode("utf-8")
        request = urllib.request.Request(
            self.url,
            data=body,
            headers=self._headers(),
            method="POST",
        )

        try:
            with self._opener.open(request, timeout=self.timeout) as response:
                response_body = response.read().decode("utf-8")
                session_id = self._header(response, "Mcp-Session-Id")
                if session_id:
                    self.session_id = session_id
                content_type = self._header(response, "Content-Type") or "application/json"
        except urllib.error.HTTPError as exc:
            error_body = exc.read().decode("utf-8", errors="replace")
            raise McpProbeError(f"HTTP {exc.code} from {self.url}: {error_body}") from exc
        except urllib.error.URLError as exc:
            raise McpProbeError(f"Cannot reach {self.url}: {exc.reason}") from exc

        return self._decode_response(response_body, content_type, payload.get("method", "unknown"), request_id)

    def request(self, method: str, params: dict | None = None) -> dict:
        request_id = self._next_id
        self._next_id += 1
        return self._post(
            {
                "jsonrpc": "2.0",
                "id": request_id,
                "method": method,
                "params": params,
            },
            request_id,
        )

    def notify(self, method: str, params: dict | None = None) -> None:
        self._post(
            {
                "jsonrpc": "2.0",
                "method": method,
                "params": params,
            },
            None,
        )

    def initialize(self) -> dict:
        result = self.request(
            "initialize",
            {
                "protocolVersion": PROTOCOL_VERSION,
                "capabilities": {},
                "clientInfo": {
                    "name": "phasma-mcp-probe",
                    "version": "0.1.0",
                },
            },
        )
        self.protocol_version = result.get("protocolVersion", PROTOCOL_VERSION)
        self.notify("notifications/initialized")
        return result

    def list_tools(self) -> list[dict]:
        result = self.request("tools/list")
        tools = result.get("tools", [])
        if not isinstance(tools, list):
            raise McpProbeError("tools/list returned a non-list tools field")
        return tools

    def call_tool(self, name: str, arguments: dict | None = None) -> dict:
        return self.request(
            "tools/call",
            {
                "name": name,
                "arguments": arguments or {},
            },
        )


def smoke_arguments(name: str, tools_by_name: dict[str, dict]) -> dict:
    if name != "get_console_log":
        return {}

    # Mirrors the current EditorToolCatalog schema, but checks tools/list first
    # so a future parameter rename degrades into a schema-visible smoke change.
    schema = tools_by_name.get(name, {}).get("inputSchema", {})
    properties = schema.get("properties", {}) if isinstance(schema, dict) else {}
    args: dict[str, object] = {}
    if "count" in properties:
        args["count"] = 20
    if "level" in properties:
        args["level"] = "all"
    return args


def summarize_tool_result(result: dict) -> dict:
    content = result.get("content", [])
    structured = result.get("structuredContent")
    return {
        "is_error": bool(result.get("isError", False)),
        "content_blocks": len(content) if isinstance(content, list) else 0,
        "structured_keys": sorted(structured.keys()) if isinstance(structured, dict) else [],
    }


def build_report(client: McpClient, run_smoke: bool, include_screenshot: bool) -> dict:
    initialize = client.initialize()
    tools = client.list_tools()
    tools_by_name = {tool.get("name", ""): tool for tool in tools if isinstance(tool, dict)}
    tool_names = sorted(tool.get("name", "") for tool in tools if isinstance(tool, dict))
    missing = [name for name in PHASE1_TOOLS if name not in tool_names]
    visible_mutating = [name for name in MUTATING_TOOLS if name in tool_names]

    report = {
        "url": client.url,
        "server": initialize.get("serverInfo", {}),
        "protocol_version": initialize.get("protocolVersion"),
        "session_id_provided": bool(client.session_id),
        "tool_count": len(tool_names),
        "phase1_tools": list(PHASE1_TOOLS),
        "missing_phase1_tools": missing,
        "visible_mutating_tools": visible_mutating,
        "smoke": {},
    }

    if run_smoke:
        smoke_tools = ["query_scene", "get_console_log"]
        if include_screenshot:
            smoke_tools.append("take_screenshot")
        for name in smoke_tools:
            args = smoke_arguments(name, tools_by_name)
            report["smoke"][name] = summarize_tool_result(client.call_tool(name, args))

    return report


def print_text_report(report: dict) -> None:
    server = report.get("server") or {}
    print(f"MCP URL: {report['url']}")
    print(f"Server: {server.get('name', 'unknown')} {server.get('version', '')}".rstrip())
    print(f"Protocol: {report.get('protocol_version', 'unknown')}")
    print(f"Session ID: {'provided' if report.get('session_id_provided') else 'not provided'}")
    print(f"Tools: {report['tool_count']}")

    missing = report["missing_phase1_tools"]
    if missing:
        print("Phase 1 tools missing: " + ", ".join(missing))
    else:
        print("Phase 1 tools available: " + ", ".join(report["phase1_tools"]))

    visible_mutating = report["visible_mutating_tools"]
    if visible_mutating:
        print("Server also exposes mutating tools: " + ", ".join(visible_mutating))

    smoke = report.get("smoke") or {}
    for name, result in smoke.items():
        status = "error" if result["is_error"] else "ok"
        keys = ", ".join(result["structured_keys"]) or "no structured keys"
        print(f"Smoke {name}: {status}, {result['content_blocks']} content blocks, {keys}")


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", default=DEFAULT_MCP_URL, help="MCP HTTP endpoint")
    parser.add_argument("--timeout", type=float, default=5.0, help="HTTP timeout in seconds")
    parser.add_argument("--smoke", action="store_true", help="Call query_scene and get_console_log")
    parser.add_argument(
        "--screenshot",
        action="store_true",
        help="Also call take_screenshot during --smoke",
    )
    parser.add_argument("--json", action="store_true", help="Print machine-readable JSON")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv or sys.argv[1:])
    client = McpClient(args.url, args.timeout)
    try:
        report = build_report(client, args.smoke or args.screenshot, args.screenshot)
    except McpProbeError as exc:
        print(f"probe failed: {exc}", file=sys.stderr)
        return 1

    if args.json:
        print(json.dumps(report, indent=2, sort_keys=True))
    else:
        print_text_report(report)

    if report["missing_phase1_tools"]:
        return 2
    smoke = report.get("smoke") or {}
    if any(result.get("is_error") for result in smoke.values()):
        return 3
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
