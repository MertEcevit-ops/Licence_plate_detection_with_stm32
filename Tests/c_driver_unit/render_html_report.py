#!/usr/bin/env python3
"""Render the C driver unit test console output as a compact HTML report."""

from __future__ import annotations

import html
import re
import sys
from datetime import datetime
from pathlib import Path


SUMMARY_RE = re.compile(r"(?P<passed>\d+)/(?P<total>\d+)\s+C driver unit tests passed")


def parse_results(raw_output: str) -> tuple[int, int, list[str], list[str]]:
    summary = SUMMARY_RE.search(raw_output)
    if summary:
        passed = int(summary.group("passed"))
        total = int(summary.group("total"))
    else:
        passed = len(re.findall(r"^\[PASS\]", raw_output, re.MULTILINE))
        total = passed + len(re.findall(r"^\[FAIL\]", raw_output, re.MULTILINE))

    passed_tests = [
        line.removeprefix("[PASS] ").strip()
        for line in raw_output.splitlines()
        if line.startswith("[PASS] ")
    ]
    failures = [
        line.strip()
        for line in raw_output.splitlines()
        if line.startswith("[FAIL] ")
    ]
    return passed, total, passed_tests, failures


def render_report(raw_output: str) -> str:
    passed, total, passed_tests, failures = parse_results(raw_output)
    failed = max(total - passed, 0)
    pass_rate = (passed / total * 100.0) if total else 0.0
    status = "PASSED" if total and failed == 0 else "FAILED"
    status_class = "passed" if status == "PASSED" else "failed"
    generated_at = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    passed_items = "\n".join(
        f"<li>{html.escape(name)}</li>" for name in passed_tests
    ) or "<li>No passed tests found in output.</li>"
    failure_items = "\n".join(
        f"<li>{html.escape(line)}</li>" for line in failures
    ) or "<li>No failure lines found.</li>"

    return f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>C Driver Unit Test Report</title>
  <style>
    :root {{
      color-scheme: light;
      --ink: #17202a;
      --muted: #5d6975;
      --line: #d8dee6;
      --panel: #f7f9fb;
      --pass: #138a43;
      --pass-bg: #dff5e8;
      --fail: #b42318;
      --fail-bg: #fde3df;
      --accent: #2563eb;
    }}
    body {{
      margin: 0;
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
      color: var(--ink);
      background: #ffffff;
    }}
    main {{
      max-width: 960px;
      margin: 0 auto;
      padding: 32px 20px 44px;
    }}
    header {{
      border-bottom: 1px solid var(--line);
      padding-bottom: 18px;
      margin-bottom: 22px;
    }}
    h1 {{
      margin: 0 0 6px;
      font-size: 28px;
      letter-spacing: 0;
    }}
    h2 {{
      margin: 28px 0 12px;
      font-size: 18px;
      letter-spacing: 0;
    }}
    .meta {{
      color: var(--muted);
      font-size: 14px;
    }}
    .summary {{
      display: grid;
      grid-template-columns: repeat(4, minmax(0, 1fr));
      gap: 12px;
      margin: 18px 0;
    }}
    .metric {{
      border: 1px solid var(--line);
      background: var(--panel);
      padding: 14px;
      border-radius: 8px;
    }}
    .metric strong {{
      display: block;
      font-size: 24px;
      margin-top: 4px;
    }}
    .label {{
      color: var(--muted);
      font-size: 13px;
    }}
    .status {{
      display: inline-block;
      border-radius: 999px;
      padding: 5px 10px;
      font-weight: 700;
      font-size: 13px;
    }}
    .status.passed {{
      color: var(--pass);
      background: var(--pass-bg);
    }}
    .status.failed {{
      color: var(--fail);
      background: var(--fail-bg);
    }}
    .bar {{
      width: 100%;
      height: 18px;
      border: 1px solid var(--line);
      border-radius: 999px;
      background: #eef2f6;
      overflow: hidden;
    }}
    .fill {{
      width: {pass_rate:.2f}%;
      height: 100%;
      background: var(--pass);
    }}
    ul {{
      margin: 0;
      padding-left: 22px;
    }}
    li {{
      margin: 6px 0;
    }}
    pre {{
      white-space: pre-wrap;
      overflow: auto;
      border: 1px solid var(--line);
      background: #111827;
      color: #f9fafb;
      border-radius: 8px;
      padding: 16px;
      font-size: 13px;
      line-height: 1.45;
    }}
    @media (max-width: 720px) {{
      .summary {{
        grid-template-columns: repeat(2, minmax(0, 1fr));
      }}
    }}
  </style>
</head>
<body>
  <main>
    <header>
      <h1>C Driver Unit Test Report</h1>
      <div class="meta">Generated at {html.escape(generated_at)}</div>
    </header>

    <section class="summary" aria-label="Test summary">
      <div class="metric"><span class="label">Status</span><strong><span class="status {status_class}">{status}</span></strong></div>
      <div class="metric"><span class="label">Pass Rate</span><strong>{pass_rate:.1f}%</strong></div>
      <div class="metric"><span class="label">Passed</span><strong>{passed}/{total}</strong></div>
      <div class="metric"><span class="label">Failed</span><strong>{failed}</strong></div>
    </section>

    <div class="bar" aria-label="Pass percentage">
      <div class="fill"></div>
    </div>

    <h2>Passed Tests</h2>
    <ul>{passed_items}</ul>

    <h2>Failures</h2>
    <ul>{failure_items}</ul>

    <h2>Raw Output</h2>
    <pre>{html.escape(raw_output)}</pre>
  </main>
</body>
</html>
"""


def main() -> int:
    if len(sys.argv) != 3:
      print("usage: render_html_report.py <results.txt> <report.html>", file=sys.stderr)
      return 2

    results_path = Path(sys.argv[1])
    report_path = Path(sys.argv[2])
    raw_output = results_path.read_text(encoding="utf-8")
    report_path.write_text(render_report(raw_output), encoding="utf-8")
    print(f"Wrote {report_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
