#!/usr/bin/env python3
"""Minimal Windows-friendly IPP probe/print utility.

This script helps validate whether a printer still accepts IPP requests,
without requiring Amiga-specific tooling.
"""
from __future__ import annotations

import argparse
import http.client
import os
import sys
import urllib.parse

IPP_VERSION = b"\x01\x01"
OP_GET_PRINTER_ATTRS = b"\x00\x0B"
OP_PRINT_JOB = b"\x00\x02"
TAG_OPERATION = b"\x01"
TAG_END = b"\x03"
TAG_CHARSET = b"\x47"
TAG_LANGUAGE = b"\x48"
TAG_URI = b"\x45"
TAG_KEYWORD = b"\x44"
TAG_NAME = b"\x42"


def _attr(tag: bytes, name: str, value: str) -> bytes:
    n = name.encode("ascii")
    v = value.encode("utf-8")
    return tag + len(n).to_bytes(2, "big") + n + len(v).to_bytes(2, "big") + v


def ipp_request(op: bytes, request_id: int, printer_uri: str, document_format: str | None = None, job_name: str = "Windows IPP Test") -> bytes:
    body = bytearray()
    body += IPP_VERSION
    body += op
    body += request_id.to_bytes(4, "big")
    body += TAG_OPERATION
    body += _attr(TAG_CHARSET, "attributes-charset", "utf-8")
    body += _attr(TAG_LANGUAGE, "attributes-natural-language", "en")
    body += _attr(TAG_URI, "printer-uri", printer_uri)
    if document_format:
        body += _attr(TAG_KEYWORD, "document-format", document_format)
        body += _attr(TAG_NAME, "requesting-user-name", os.getlogin())
        body += _attr(TAG_NAME, "job-name", job_name)
    body += TAG_END
    return bytes(body)


def send_ipp(url: str, body: bytes, payload: bytes = b"") -> tuple[int, bytes]:
    parsed = urllib.parse.urlparse(url)
    if parsed.scheme not in {"http", "https"}:
        raise ValueError("IPP URL must start with http:// or https://")

    conn_cls = http.client.HTTPSConnection if parsed.scheme == "https" else http.client.HTTPConnection
    port = parsed.port or (443 if parsed.scheme == "https" else 631)
    path = parsed.path or "/ipp/print"

    conn = conn_cls(parsed.hostname, port, timeout=15)
    data = body + payload
    conn.request("POST", path, body=data, headers={
        "Content-Type": "application/ipp",
        "Content-Length": str(len(data)),
    })
    resp = conn.getresponse()
    resp_bytes = resp.read()
    conn.close()
    return resp.status, resp_bytes


def parse_status_code(resp: bytes) -> int:
    if len(resp) < 4:
        return -1
    return int.from_bytes(resp[2:4], "big")


def main() -> int:
    p = argparse.ArgumentParser(description="IPP probe and print tool")
    p.add_argument("printer_url", help="Example: http://192.168.1.50:631/ipp/print")
    p.add_argument("--print-file", help="Optional file to submit as a print job")
    p.add_argument("--mime", default="image/jpeg", help="MIME type for --print-file")
    args = p.parse_args()

    printer_uri = args.printer_url

    req = ipp_request(OP_GET_PRINTER_ATTRS, 1, printer_uri)
    http_status, resp = send_ipp(args.printer_url, req)
    ipp_status = parse_status_code(resp)
    print(f"Get-Printer-Attributes HTTP={http_status} IPP=0x{ipp_status:04x}")

    if http_status >= 400 or ipp_status >= 0x0400:
        print("Printer rejected capability query. Check URI/path/TLS requirements.")
        return 2

    if args.print_file:
        with open(args.print_file, "rb") as f:
            payload = f.read()
        req = ipp_request(OP_PRINT_JOB, 2, printer_uri, args.mime, job_name=os.path.basename(args.print_file))
        http_status, resp = send_ipp(args.printer_url, req, payload=payload)
        ipp_status = parse_status_code(resp)
        print(f"Print-Job HTTP={http_status} IPP=0x{ipp_status:04x} bytes={len(payload)}")
        if http_status >= 400 or ipp_status >= 0x0400:
            print("Print job failed; compare this URI and format with Amiga tool settings.")
            return 3

    print("IPP communication succeeded.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
