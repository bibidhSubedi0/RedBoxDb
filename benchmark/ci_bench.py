#!/usr/bin/env python3
"""
CI Benchmark Script for RedBoxDb.

Modes:
  commit  - Run benchmarks, update history, regenerate REPORT.md, git commit
  pr      - Run benchmarks, compare with last main, print PR comment to stdout
"""

import json
import subprocess
import sys
import os
import re
from datetime import datetime, timezone

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BENCH_DIR = os.path.join(REPO_ROOT, "benchmarks")
HISTORY_FILE = os.path.join(BENCH_DIR, "data.json")
REPORT_FILE = os.path.join(BENCH_DIR, "REPORT.md")
MAX_HISTORY = 100

# Benchmarks to track with labels
METRICS = [
    ("hnsw_qps_1t",        "HNSW QPS (1T)",       "{:,.0f}", "higher"),
    ("hnsw_qps_nt",        "HNSW QPS ({nt}T)",     "{:,.0f}", "higher"),
    ("ivf_qps_1t",         "IVF QPS (1T)",         "{:,.0f}", "higher"),
    ("ivf_qps_nt",         "IVF QPS ({nt}T)",      "{:,.0f}", "higher"),
    ("hnsw_insert_per_sec","HNSW Insert/sec",      "{:,.0f}", "higher"),
    ("ivf_insert_per_sec", "IVF Insert/sec",       "{:,.0f}", "higher"),
    ("hnsw_recall_100",    "Recall@100",           "{:.1%}",  "neutral"),
]


def run_benchmark():
    """Run CiBench and return parsed JSON dict."""
    exe = os.path.join(REPO_ROOT, "build", "benchmark", "CiBench")
    if not os.path.exists(exe):
        print(f"ERROR: {exe} not found. Build CiBench first.", file=sys.stderr)
        sys.exit(1)
    result = subprocess.run(
        [exe],
        capture_output=True, text=True, timeout=600
    )
    if result.returncode != 0:
        print(f"ERROR: CiBench failed:\n{result.stderr}", file=sys.stderr)
        sys.exit(1)
    # Parse the last non-empty line (stdout may have spdlog noise)
    lines = [l for l in result.stdout.strip().split("\n") if l.strip()]
    if not lines:
        print("ERROR: No output from CiBench", file=sys.stderr)
        sys.exit(1)
    return json.loads(lines[-1])


def get_git_info():
    """Get current commit hash and message."""
    sha = subprocess.check_output(
        ["git", "rev-parse", "--short", "HEAD"],
        cwd=REPO_ROOT, text=True
    ).strip()
    msg = subprocess.check_output(
        ["git", "log", "-1", "--pretty=%s"],
        cwd=REPO_ROOT, text=True
    ).strip()
    return sha, msg


def load_history():
    if os.path.exists(HISTORY_FILE):
        with open(HISTORY_FILE) as f:
            return json.load(f)
    return {"results": []}


def save_history(history):
    os.makedirs(BENCH_DIR, exist_ok=True)
    history["results"] = history["results"][-MAX_HISTORY:]
    with open(HISTORY_FILE, "w") as f:
        json.dump(history, f, indent=2)


def trend_arrow(curr, prev, direction):
    """Return a trend indicator."""
    if prev is None or prev == 0:
        return "-"
    pct = (curr - prev) / prev * 100
    if direction == "neutral":
        return "→"
    if pct > 1.0:
        return f"+{pct:.1f}% ↑"
    elif pct < -1.0:
        return f"{pct:.1f}% ↓"
    else:
        return "→"


def format_value(key, value, threads=12):
    for k, label, fmt, _ in METRICS:
        if k == key:
            label = label.replace("{nt}", str(threads))
            return fmt.format(value), label
    return str(value), key


def sparkline(values, width=40):
    """Generate ASCII sparkline."""
    if not values:
        return ""
    blocks = " ▁▂▃▄▅▆▇█"
    lo, hi = min(values), max(values)
    rng = hi - lo if hi != lo else 1
    # Truncate to width
    vals = values[-width:]
    return "".join(blocks[min(int((v - lo) / rng * 8) + 1, 8)] for v in vals)


def mermaid_chart(title, labels, values, y_max=None):
    """Generate Mermaid xychart-beta block."""
    if len(values) < 2:
        return ""
    if y_max is None:
        y_max = int(max(values) * 1.15 / 1000) * 1000 + 1000
        if y_max == 0:
            y_max = 1000
    # Truncate to last 30 for readability
    labels = labels[-30:]
    values = values[-30:]
    x_labels = ", ".join(f'"{l}"' for l in labels)
    y_vals = ", ".join(str(int(v)) for v in values)
    return (
        f"```mermaid\n"
        f"xychart-beta\n"
        f'  title "{title}"\n'
        f"  x-axis [{x_labels}]\n"
        f'  y-axis "QPS" 0 --> {y_max}\n'
        f"  line [{y_vals}]\n"
        f"```"
    )


def generate_report(history, threads=12):
    """Generate the full markdown report."""
    results = history["results"]
    if not results:
        return "# RedBoxDb Performance Dashboard\n\nNo data yet.\n"

    latest = results[-1]
    prev = results[-2] if len(results) >= 2 else None

    lines = []
    lines.append("# RedBoxDb Performance Dashboard\n")
    lines.append(f"> Auto-generated on every commit to main. Last updated: {latest['date']}\n")

    # --- Latest results table ---
    lines.append(f"## Latest Results (`{latest['commit']}`)\n")
    lines.append("| Metric | Value | vs Previous |")
    lines.append("|--------|-------|-------------|")
    for key, label, fmt, direction in METRICS:
        label = label.replace("{nt}", str(threads))
        val_str = fmt.format(latest[key])
        if prev and key in prev:
            arrow = trend_arrow(latest[key], prev[key], direction)
        else:
            arrow = "-"
        lines.append(f"| {label} | {val_str} | {arrow} |")

    # --- Mermaid charts ---
    charts = [
        ("hnsw_qps_1t", "HNSW 1-NN QPS (1 thread)"),
        ("hnsw_qps_nt", f"HNSW 1-NN QPS ({threads} threads)"),
        ("ivf_qps_1t", "IVF 1-NN QPS (1 thread)"),
        ("ivf_qps_nt", f"IVF 1-NN QPS ({threads} threads)"),
    ]

    lines.append("")
    for key, title in charts:
        values = [r[key] for r in results if key in r]
        commit_labels = [r["commit"] for r in results if key in r]
        if len(values) >= 2:
            lines.append(f"## {title}\n")
            lines.append(mermaid_chart(title, commit_labels, values))
            lines.append("")

    # --- Sparklines ---
    lines.append("## Quick Trends\n")
    lines.append("```")
    for key, label, fmt, _ in METRICS:
        label = label.replace("{nt}", str(threads))
        values = [r[key] for r in results if key in r]
        if values:
            spark = sparkline(values)
            latest_val = fmt.format(values[-1])
            lines.append(f"{label:>22s}  {latest_val:>12s}  {spark}")
    lines.append("```\n")

    # --- Full history table ---
    lines.append("## Full History\n")
    lines.append("| # | Commit | Date | HNSW 1T | HNSW NT | IVF 1T | IVF NT | HNSW Ins | IVF Ins | Recall |")
    lines.append("|---|--------|------|---------|---------|--------|--------|----------|---------|--------|")
    for i, r in enumerate(reversed(results)):
        idx = len(results) - i
        lines.append(
            f"| {idx} "
            f"| `{r['commit']}` "
            f"| {r['date']} "
            f"| {r.get('hnsw_qps_1t', 0):,.0f} "
            f"| {r.get('hnsw_qps_nt', 0):,.0f} "
            f"| {r.get('ivf_qps_1t', 0):,.0f} "
            f"| {r.get('ivf_qps_nt', 0):,.0f} "
            f"| {r.get('hnsw_insert_per_sec', 0):,.0f} "
            f"| {r.get('ivf_insert_per_sec', 0):,.0f} "
            f"| {r.get('hnsw_recall_100', 0):.1%} |"
        )

    return "\n".join(lines) + "\n"


def generate_pr_comment(current, prev, threads=12):
    """Generate a PR comparison comment."""
    lines = []
    lines.append("## Benchmark Results\n")
    lines.append("| Metric | This PR | Main | Change |")
    lines.append("|--------|---------|------|--------|")
    for key, label, fmt, direction in METRICS:
        label = label.replace("{nt}", str(threads))
        curr_val = fmt.format(current[key])
        if prev and key in prev:
            prev_val = fmt.format(prev[key])
            arrow = trend_arrow(current[key], prev[key], direction)
        else:
            prev_val = "-"
            arrow = "-"
        lines.append(f"| {label} | {curr_val} | {prev_val} | {arrow} |")

    # Add a note about CI variance
    lines.append("")
    lines.append("> Note: CI runners have ~5-10% performance variance. Changes within this range are not significant.")

    return "\n".join(lines)


def git_commit(message):
    """Stage and commit benchmark files."""
    subprocess.run(
        ["git", "add", HISTORY_FILE, REPORT_FILE],
        cwd=REPO_ROOT, check=True
    )
    # Check if there are changes to commit
    result = subprocess.run(
        ["git", "diff", "--cached", "--quiet"],
        cwd=REPO_ROOT
    )
    if result.returncode == 0:
        print("No changes to commit.", file=sys.stderr)
        return
    subprocess.run(
        ["git", "commit", "-m", message],
        cwd=REPO_ROOT, check=True
    )
    print("Committed benchmark results.", file=sys.stderr)


def main():
    if len(sys.argv) < 2:
        print("Usage: ci_bench.py [commit|pr]", file=sys.stderr)
        sys.exit(1)

    mode = sys.argv[1]
    threads = int(sys.argv[2]) if len(sys.argv) > 2 else 12

    # Run benchmark
    print("Running CiBench...", file=sys.stderr)
    results = run_benchmark()
    print(f"Results: {json.dumps(results, indent=2)}", file=sys.stderr)

    if mode == "commit":
        sha, msg = get_git_info()
        today = datetime.now(timezone.utc).strftime("%Y-%m-%d")

        entry = {
            "commit": sha,
            "date": today,
            "message": msg,
            **results
        }

        history = load_history()
        history["results"].append(entry)
        save_history(history)

        report = generate_report(history, threads)
        os.makedirs(BENCH_DIR, exist_ok=True)
        with open(REPORT_FILE, "w") as f:
            f.write(report)

        git_commit(f"bench: update performance report [{sha}] [skip ci]")
        print(f"\nReport updated. {len(history['results'])} entries in history.", file=sys.stderr)

    elif mode == "pr":
        history = load_history()
        prev = history["results"][-1] if history["results"] else None
        comment = generate_pr_comment(results, prev, threads)
        # Print to stdout for the workflow to capture
        print(comment)

    else:
        print(f"Unknown mode: {mode}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
