"""Terminal screenshots of the RetroC prototype for the paper.

Runs the real `retroc` commands on the demo programs, captures their output,
and renders each session as a terminal-style PNG (300 dpi, one IEEE column
wide). Nothing is typed by hand: every line comes from the program.
Run from the repository root after building:
    python paper/make_screenshots.py
"""
import os
import subprocess
import textwrap

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "paper", "figures")
RETROC = os.path.join(ROOT, "build", "retroc.exe")
MSYS = r"C:\msys64\ucrt64\bin"

BG, FG, PROMPT, DIM, OK, BAR = "#14181f", "#d8dee9", "#7fb2f0", "#8a93a0", "#8fd694", "#232a35"
COLS = 68          # characters per line before wrapping (fits 3.4 in at FONT pt)
FONT = 5.6         # points


def run(args):
    env = dict(os.environ)
    env["PATH"] = MSYS + os.pathsep + env.get("PATH", "")
    proc = subprocess.run([RETROC] + args, cwd=ROOT, env=env, capture_output=True, text=True)
    out = (proc.stdout + proc.stderr).rstrip("\n")
    if proc.returncode != 0:
        raise SystemExit(f"retroc {' '.join(args)} failed ({proc.returncode}):\n{out}")
    return out.splitlines()


def wrap(line):
    if len(line) <= COLS:
        return [line]
    indent = len(line) - len(line.lstrip())
    return textwrap.wrap(line, COLS, subsequent_indent=" " * (indent + 4),
                         break_long_words=True, drop_whitespace=False)


def render(name, title, sessions, highlight=()):
    """sessions: list of (command line, output lines)."""
    lines = []
    for cmd, out in sessions:
        lines.append(("prompt", "$ " + cmd))
        for raw in out:
            for piece in wrap(raw):
                kind = "ok" if any(h in raw for h in highlight) else "out"
                lines.append((kind, piece))
        lines.append(("out", ""))
    lines = lines[:-1]

    line_h = FONT * 1.45 / 72          # inches
    width = 3.4
    height = 0.22 + len(lines) * line_h + 0.12
    fig = plt.figure(figsize=(width, height), dpi=300)
    ax = fig.add_axes([0, 0, 1, 1])
    ax.set_xlim(0, width)
    ax.set_ylim(0, height)
    ax.axis("off")
    ax.add_patch(FancyBboxPatch((0.01, 0.01), width - 0.02, height - 0.02,
                                boxstyle="round,pad=0,rounding_size=0.05", facecolor=BG, edgecolor="#3a4252",
                                linewidth=0.6))
    # title bar with window dots
    ax.add_patch(FancyBboxPatch((0.01, height - 0.19), width - 0.02, 0.18,
                                boxstyle="round,pad=0,rounding_size=0.05", facecolor=BAR, edgecolor="none"))
    for k, c in enumerate(["#ec6a5e", "#f4bf4f", "#61c554"]):
        ax.add_patch(plt.Circle((0.1 + k * 0.08, height - 0.10), 0.025, color=c))
    ax.text(width / 2, height - 0.10, title, color=DIM, fontsize=5.4, ha="center", va="center",
            family="DejaVu Sans")
    y = height - 0.22 - line_h * 0.7
    colors = {"prompt": PROMPT, "out": FG, "ok": OK}
    for kind, text in lines:
        ax.text(0.08, y, text, color=colors[kind], fontsize=FONT, family="DejaVu Sans Mono",
                ha="left", va="center", fontweight="bold" if kind == "prompt" else "normal")
        y -= line_h
    fig.savefig(os.path.join(OUT, name), dpi=300, facecolor="white")
    plt.close(fig)
    print("wrote", name, f"({len(lines)} lines)")


def main():
    os.makedirs(OUT, exist_ok=True)
    render("shot1_run_edit.png", "retroc: first run and four retroactive edits", [
        ("retroc run demo/pipeline.rc", run(["run", "demo/pipeline.rc"])),
        ("retroc edit demo/pipeline.rc demo/edits.rcs", run(["edit", "demo/pipeline.rc", "demo/edits.rcs"])),
    ], highlight=("Reuse: 80.0%",))
    render("shot2_verify_explain.png", "retroc: verification and provenance", [
        ("retroc verify demo/pipeline.rc demo/edits.rcs", run(["verify", "demo/pipeline.rc", "demo/edits.rcs"])),
        ("retroc explain demo/pipeline.rc demo/edits.rcs avg_temp_f",
         run(["explain", "demo/pipeline.rc", "demo/edits.rcs", "avg_temp_f"])),
    ], highlight=("Verification OK",))
    render("shot3_records_tree.png", "retroc: record insert/delete and tree_reduce", [
        ("retroc edit demo/pipeline.rc demo/record_edits.rcs",
         run(["edit", "demo/pipeline.rc", "demo/record_edits.rcs"])),
        ("retroc verify demo/pipeline.rc demo/record_edits.rcs",
         run(["verify", "demo/pipeline.rc", "demo/record_edits.rcs"])),
        ("retroc edit demo/tree_demo.rc demo/tree_demo.rcs", run(["edit", "demo/tree_demo.rc", "demo/tree_demo.rcs"])),
    ], highlight=("Verification OK", "3 / 8"))


if __name__ == "__main__":
    main()
