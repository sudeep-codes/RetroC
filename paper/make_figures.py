"""Figures for the RetroC IEEE paper.

Reads the published benchmark results and writes 300-dpi PNGs sized for one
IEEE column (3.4 in). Run from the repository root:
    python paper/make_figures.py
"""
import json
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RESULTS = os.environ.get("RETROC_RESULTS") or os.path.join(
    ROOT, "bench", "published", "2026-10-29", "benchmark_results.json")
OUT = os.path.join(ROOT, "paper", "figures")
os.makedirs(OUT, exist_ok=True)

# Validated categorical slots (dataviz reference palette, light mode); fixed
# entity order: B0 full rerun, B1 stage-level, B2 RetroC.
B0, B1, B2 = "#2a78d6", "#eb6834", "#1baf7a"
INK, INK2, GRID = "#0b0b0b", "#52514e", "#d9d8d4"
COL_W = 3.4

plt.rcParams.update({
    "font.family": "serif",
    "font.serif": ["STIXGeneral", "DejaVu Serif"],
    "mathtext.fontset": "stix",
    "font.size": 7.5,
    "axes.edgecolor": INK2,
    "axes.labelcolor": INK,
    "axes.linewidth": 0.6,
    "xtick.color": INK2,
    "ytick.color": INK2,
    "xtick.labelsize": 7,
    "ytick.labelsize": 7,
    "legend.fontsize": 7,
    "legend.frameon": False,
    "hatch.linewidth": 0.5,
})

with open(RESULTS, encoding="utf-8") as f:
    data = json.load(f)
rows = {(s["workload"], s["edit"]): s for s in data["scenarios"]}


def ms(row, system):
    t = row[system]
    return None if t is None else t["median_ms"]


def ratio(workload, edit, num="b0", den="b2"):
    r = rows[(workload, edit)]
    a, b = ms(r, num), ms(r, den)
    return None if a is None or b is None else a / b


def style_axes(ax):
    ax.spines["top"].set_visible(False)
    ax.spines["right"].set_visible(False)
    ax.yaxis.grid(True, color=GRID, linewidth=0.5)
    ax.set_axisbelow(True)


def save(fig, name):
    fig.savefig(os.path.join(OUT, name), dpi=300, bbox_inches="tight", facecolor="white")
    plt.close(fig)


def label_bars(ax, bars, values, fmt="{:.1f}×"):
    for b, v in zip(bars, values):
        ax.annotate(fmt.format(v), (b.get_x() + b.get_width() / 2, b.get_height()),
                    xytext=(0, 1.5), textcoords="offset points",
                    ha="center", va="bottom", fontsize=6, color=INK,
                    bbox=dict(boxstyle="square,pad=0.08", facecolor="white", edgecolor="none"))


# --- Fig. 1: architecture ----------------------------------------------------
def fig_architecture():
    W, H = 10.0, 13.6
    fig, ax = plt.subplots(figsize=(COL_W, COL_W * H / W))
    ax.set_xlim(0, W)
    ax.set_ylim(0, H)
    ax.axis("off")
    FRONT, CORE, EDIT, OUTC = "#eef4fc", "#fdf0ea", "#e8f7f1", "#f3f2ee"

    def box(x, y, w, h, title, sub=None, fill=FRONT, bold=False):
        ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0,rounding_size=0.12",
                                    linewidth=0.7, edgecolor=INK2, facecolor=fill))
        if sub:
            ax.text(x + w / 2, y + h * 0.66, title, ha="center", va="center", fontsize=6.4, color=INK,
                    fontweight="bold" if bold else "normal")
            ax.text(x + w / 2, y + h * 0.30, sub, ha="center", va="center", fontsize=5.5, color=INK2,
                    linespacing=1.1)
        else:
            ax.text(x + w / 2, y + h / 2, title, ha="center", va="center", fontsize=6.4, color=INK,
                    fontweight="bold" if bold else "normal")

    def arrow(x1, y1, x2, y2, style="-|>", dashed=False):
        ax.annotate("", xy=(x2, y2), xytext=(x1, y1),
                    arrowprops=dict(arrowstyle=style, lw=0.7, color=INK2, mutation_scale=7,
                                    linestyle=(0, (3, 2)) if dashed else "-", shrinkA=0, shrinkB=0))

    def lane(y, h, label):
        ax.add_patch(FancyBboxPatch((0.05, y), W - 0.1, h, boxstyle="round,pad=0,rounding_size=0.15",
                                    linewidth=0.5, edgecolor=GRID, facecolor="none", linestyle=(0, (3, 2))))
        ax.text(W - 0.25, y + h - 0.25, label, ha="right", va="center", fontsize=6, color=INK2,
                style="italic")

    # lane 1: front end
    lane(10.3, 3.2, "compile (once)")
    ax.text(0.3, 12.95, "program.rc", fontsize=5.8, color=INK2, family="DejaVu Sans Mono")
    names = [("Lexer", "tokens"), ("Parser", "AST"), ("Sema", "types, purity"), ("IR builder", "versioned DAG")]
    for i, (t, sub) in enumerate(names):
        box(0.3 + i * 2.38, 10.9, 2.0, 1.6, t, sub)
        if i:
            arrow(0.3 + i * 2.38 - 0.38, 11.7, 0.3 + i * 2.38, 11.7)

    # lane 2: core
    lane(5.9, 4.1, "run")
    box(2.4, 8.2, 5.2, 1.45, "Retroactive IR", "nodes, reference inputs,\nversions, hashes, memo tables",
        fill=CORE, bold=True)
    arrow(8.4, 10.9, 6.8, 9.65)
    box(0.3, 6.2, 4.4, 1.5, "Bytecode compiler + VM", "lambdas to stack code")
    box(5.3, 6.2, 4.4, 1.5, "Graph executor", "first run, memo fill,\nhash join, sum terms")
    arrow(3.5, 8.2, 2.5, 7.7)
    arrow(6.5, 8.2, 7.5, 7.7)
    arrow(4.7, 6.95, 5.3, 6.95)

    # lane 3: edits
    lane(1.95, 3.65, "edit (repeat)")
    ax.text(1.25, 5.15, "edits.rcs", fontsize=5.8, color=INK2, family="DejaVu Sans Mono")
    box(0.3, 2.45, 4.4, 2.45, "Edit engine",
        "modify record\ninsert / delete stage\ninsert / delete record\ntype-check, undo on error",
        fill=EDIT)
    box(5.3, 2.45, 4.4, 2.45, "Propagator",
        "work set of record IDs\ncost model: patch or\nfull pass, early cutoff,\nO(1) collection hash",
        fill=EDIT, bold=True)
    arrow(4.7, 3.67, 5.3, 3.67)
    arrow(7.5, 6.2, 7.5, 4.9)
    # edits rewrite the IR: routed along the left margin, clear of the VM box
    ax.plot([0.9, 0.9, 0.17, 0.17, 1.6], [4.9, 5.35, 5.35, 8.92, 8.92], color=INK2, lw=0.7,
            linestyle=(0, (3, 2)))
    arrow(1.6, 8.92, 2.4, 8.92, dashed=True)
    ax.text(0.3, 8.6, "rewrite", fontsize=5.4, color=INK2, style="italic", ha="left")

    # outputs
    outs = [("Updated outputs", None), ("Rebuild plan", "+ explain"), ("Verifier", "full rerun, compare")]
    for i, (t, sub) in enumerate(outs):
        x = 0.3 + i * 3.25
        box(x, 0.15, 2.95, 1.35, t, sub, fill=OUTC)
        arrow(7.5, 2.45, x + 1.47, 1.5)
    save(fig, "fig1_architecture.png")


# --- Fig. 2: single-record edit speedup by workload --------------------------
def fig_single_edit():
    labels = ["W1\n10k", "W1\n100k", "W1\n1M", "W3\n100k", "W4\n1M", "W2\njoin"]
    keys = ["W1-10k", "W1-100k", "W1-1M", "W3", "W4", "W2"]
    vals = [ratio(k, "E-A") for k in keys]
    fig, ax = plt.subplots(figsize=(COL_W, 1.9))
    bars = ax.bar(range(len(vals)), vals, width=0.6, color=B2, edgecolor="white", linewidth=0.8)
    ax.set_yscale("log")
    for b, v in zip(bars, vals):
        ax.text(b.get_x() + b.get_width() / 2, v * 1.25, f"{v:,.0f}×", ha="center", va="bottom",
                fontsize=6.3, color=INK)
    ax.set_xticks(range(len(vals)))
    ax.set_xticklabels(labels)
    ax.set_ylabel("Speedup over full rerun (B0/B2)")
    ax.set_ylim(1, max(vals) * 6)
    style_axes(ax)
    ax.yaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(lambda v, _: f"{v:,.0f}×"))
    save(fig, "fig2_single_edit.png")


# --- Fig. 3 and 5: per-scenario ratios, W1-100k vs W3 -------------------------
EDITS = ["E-A", "E-B", "E-C", "E-D", "E-E", "E-F"]
EDIT_NAMES = ["modify\n1", "modify\nbatch", "insert\nstart", "insert\nend", "delete\nmiddle", "insert\nidentity"]


def fig_scenarios(num, den, ylabel, name):
    import numpy as np
    w1 = [ratio("W1-100k", e, num, den) for e in EDITS]
    w3 = [ratio("W3", e, num, den) for e in EDITS]
    x = np.arange(len(EDITS))
    fig, ax = plt.subplots(figsize=(COL_W, 2.1))
    b1 = ax.bar(x - 0.19, w1, width=0.36, color=B0, edgecolor="white", linewidth=0.8, label="W1 (100k records)")
    b3 = ax.bar(x + 0.19, w3, width=0.36, color=B2, edgecolor="white", linewidth=0.8, hatch="////",
                label="W3 (12 stages, 100k)")
    ax.set_yscale("log")
    ax.axhline(1.0, color=INK2, linewidth=0.7, linestyle="--")
    label_bars(ax, b1, w1, "{:.2g}")
    label_bars(ax, b3, w3, "{:.2g}")
    ax.set_xticks(x)
    ax.set_xticklabels(EDIT_NAMES, fontsize=6.3)
    ax.set_ylabel(ylabel)
    lo = min(w1 + w3)
    ax.set_ylim(lo / 1.8, max(w1 + w3) * 2.2)
    ax.legend(loc="upper right", ncol=2, handlelength=1.2, columnspacing=0.8)
    style_axes(ax)
    ax.yaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(lambda v, _: f"{v:g}×"))
    save(fig, name)


# --- Fig. 4: scaling of a single-record edit ---------------------------------
def fig_scaling():
    sizes = [1e4, 1e5, 1e6]
    keys = ["W1-10k", "W1-100k", "W1-1M"]
    fig, ax = plt.subplots(figsize=(COL_W, 2.1))
    series = [("b0", "B0 full rerun", B0, "o", "-"),
              ("b1", "B1 stage-level", B1, "s", "--"),
              ("b2", "B2 RetroC", B2, "^", "-")]
    for sys_key, name, color, marker, ls in series:
        ys = [ms(rows[(k, "E-A")], sys_key) for k in keys]
        ax.plot(sizes, ys, color=color, linewidth=1.4, linestyle=ls, marker=marker, markersize=4,
                markeredgecolor="white", markeredgewidth=0.6, label=name)
        dy = {"b0": 5, "b1": -5}.get(sys_key, 0)
        lbl = f"{ys[-1]:,.0f} ms" if ys[-1] >= 1 else f"{ys[-1]:.2f} ms"
        ax.annotate(lbl, (sizes[-1], ys[-1]), xytext=(5, dy), textcoords="offset points",
                    va="center", fontsize=6, color=INK)
    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.set_xlabel("Records in input")
    ax.set_ylabel("Time per edit (ms, median of 11)")
    ax.set_xlim(7e3, 3.2e6)
    ax.legend(loc="upper left", handlelength=2.2)
    style_axes(ax)
    ax.xaxis.grid(True, color=GRID, linewidth=0.5)
    save(fig, "fig4_scaling.png")


# --- Fig. 6: before vs after the propagation rewrite --------------------------
def fig_before_after():
    import numpy as np
    # W1-100k, same unpinned 1 warm-up + median-of-3 protocol for both
    # (MEMORY.md, 28 Oct 2026); "after" is the mean of the two runs.
    edits = ["modify\n1", "modify\nbatch", "insert\nstart", "insert\nend", "delete\nmiddle"]
    before = [1.28, 0.29, 0.30, 1.31, 0.86]
    after = [(5.78 + 5.06) / 2, (1.26 + 1.30) / 2, (1.01 + 1.03) / 2, (4.90 + 5.51) / 2, (2.15 + 2.24) / 2]
    x = np.arange(len(edits))
    fig, ax = plt.subplots(figsize=(COL_W, 1.95))
    bb = ax.bar(x - 0.19, before, width=0.36, color="#bdbcb6", edgecolor="white", linewidth=0.8,
                label="before (full rescan)")
    ba = ax.bar(x + 0.19, after, width=0.36, color=B2, edgecolor="white", linewidth=0.8, hatch="////",
                label="after (record work set)")
    ax.axhline(1.0, color=INK2, linewidth=0.7, linestyle="--")
    label_bars(ax, bb, before, "{:.2f}")
    label_bars(ax, ba, after, "{:.2f}")
    ax.set_xticks(x)
    ax.set_xticklabels(edits, fontsize=6.3)
    ax.set_ylabel("Speedup over full rerun (B0/B2)")
    ax.set_ylim(0, 7.8)
    ax.legend(loc="upper center", ncol=2, handlelength=1.2, columnspacing=1.0)
    style_axes(ax)
    save(fig, "fig6_before_after.png")


# --- Fig. 2: end-to-end flowchart -------------------------------------------
def fig_flowchart():
    W, H = 10.0, 26.0
    fig, ax = plt.subplots(figsize=(COL_W, COL_W * H / W))
    ax.set_xlim(0, W)
    ax.axis("off")

    PROC, DEC, TERM, IO, SIDE = "#eef4fc", "#fdf0ea", "#e8f7f1", "#f3f2ee", "#ffffff"
    CX, BW = 4.0, 5.2             # main column centre and box width
    SX, SW = 8.35, 2.95           # side column centre and box width
    GAP = 0.34
    FS, FS_SIDE = 6.0, 5.5

    def arrow(x1, y1, x2, y2, label=None, lx=0.0, ly=0.0):
        ax.annotate("", xy=(x2, y2), xytext=(x1, y1),
                    arrowprops=dict(arrowstyle="-|>", lw=0.6, color=INK2, mutation_scale=6,
                                    shrinkA=0, shrinkB=0))
        if label:
            ax.text((x1 + x2) / 2 + lx, (y1 + y2) / 2 + ly, label, fontsize=5.6, color=INK2,
                    style="italic", ha="center", va="center")

    def path(points, label=None, lpos=None):
        xs, ys = zip(*points[:-1])
        ax.plot(xs, ys, color=INK2, lw=0.6, solid_capstyle="butt")
        arrow(points[-2][0], points[-2][1], points[-1][0], points[-1][1])
        if label:
            ax.text(lpos[0], lpos[1], label, fontsize=5.6, color=INK2, style="italic",
                    ha="center", va="center")

    def number(n, top, left):
        # step badge on the box's top-left corner, clear of the loop arrows
        ax.add_patch(matplotlib.patches.Circle((left, top), 0.21, facecolor=INK,
                                               edgecolor="white", linewidth=0.6, zorder=3))
        ax.text(left, top, str(n), fontsize=5.0, color="white", ha="center",
                va="center", fontweight="bold", zorder=4)

    def shape(kind, x, yc, w, h, text, fill, fs=FS):
        if kind == "proc":
            ax.add_patch(FancyBboxPatch((x - w / 2, yc - h / 2), w, h,
                                        boxstyle="round,pad=0,rounding_size=0.08",
                                        linewidth=0.6, edgecolor=INK2, facecolor=fill))
        elif kind == "term":
            ax.add_patch(FancyBboxPatch((x - w / 2, yc - h / 2), w, h,
                                        boxstyle=f"round,pad=0,rounding_size={h / 2}",
                                        linewidth=0.6, edgecolor=INK2, facecolor=fill))
        elif kind == "io":
            s = 0.25
            ax.add_patch(matplotlib.patches.Polygon(
                [(x - w / 2 + s, yc + h / 2), (x + w / 2 + s, yc + h / 2),
                 (x + w / 2 - s, yc - h / 2), (x - w / 2 - s, yc - h / 2)],
                closed=True, linewidth=0.6, edgecolor=INK2, facecolor=fill))
        elif kind == "dec":
            ax.add_patch(matplotlib.patches.Polygon(
                [(x, yc + h / 2), (x + w / 2, yc), (x, yc - h / 2), (x - w / 2, yc)],
                closed=True, linewidth=0.6, edgecolor=INK2, facecolor=fill))
        ax.text(x, yc, text, fontsize=fs, color=INK, ha="center", va="center", linespacing=1.12)

    cur = [H - 0.35]
    nodes = {}

    def main(key, kind, text, h, n=None, link=True, label=None):
        top = cur[0]
        yc = top - h / 2
        w = {"dec": 3.6, "term": 3.0, "io": BW - 0.6}.get(kind, BW)
        fill = {"proc": PROC, "dec": DEC, "term": TERM, "io": IO}[kind]
        shape(kind, CX, yc, w, h, text, fill)
        if n is not None:
            number(n, top, CX - w / 2 + (0.25 if kind == "io" else 0.0) - 0.08)
        if link and nodes:
            prev = nodes[list(nodes)[-1]]
            arrow(CX, prev["bot"], CX, top, label, lx=0.35)
        nodes[key] = dict(yc=yc, top=top, bot=top - h, w=w, h=h)
        cur[0] = top - h - GAP

    def side(key, text, yc, h):
        shape("proc", SX, yc, SW, h, text, SIDE, fs=FS_SIDE)
        nodes[key] = dict(yc=yc, top=yc + h / 2, bot=yc - h / 2, w=SW, h=h, side=True)

    def side_from(dec_key, side_key, text, h, label):
        d = nodes[dec_key]
        side(side_key, text, d["yc"], h)
        arrow(CX + d["w"] / 2, d["yc"], SX - SW / 2, d["yc"], label, ly=0.17)

    def band(title, top, bot):
        ax.add_patch(FancyBboxPatch((0.08, bot), W - 0.16, top - bot,
                                    boxstyle="round,pad=0,rounding_size=0.18", linewidth=0.6,
                                    edgecolor=GRID, facecolor="none", linestyle=(0, (3, 2))))
        ax.text(W - 0.3, top - 0.28, title, fontsize=6.2, color=INK, fontweight="bold",
                ha="right", va="center")

    # ---- A. first run ----
    a_top = cur[0]
    cur[0] -= 0.55
    main("start", "term", "Start: program.rc", 0.55, link=False)
    main("parse", "proc", "Lex and parse into a syntax tree", 0.55, 1)
    main("sema", "proc", "Type, scope and purity checks", 0.55, 2)
    main("err", "dec", "Errors?", 0.8, label=None)
    side_from("err", "stop", "Report code and\nline:col; stop", 0.6, "yes")
    main("ir", "proc", "Build the Retroactive IR\n(versioned DAG, topological order)", 0.75, 3, label="no")
    main("bc", "proc", "Compile lambda bodies to bytecode", 0.55, 4)
    main("run", "proc", "Run stages in order; fill per-record\nmemo tables and collection hashes", 0.75, 5)
    main("out", "io", "Initial outputs", 0.55)
    a_bot = nodes["out"]["bot"] - 0.2
    band("A.  First run (once)", a_top, a_bot)

    # ---- B. each edit ----
    cur[0] = a_bot - 0.3
    b_top = cur[0]
    cur[0] -= 0.55
    main("read", "io", "Read the next edit (modify, or\ninsert/delete a stage or record)", 0.75, 6, link=False)
    arrow(CX, nodes["out"]["bot"], CX, nodes["read"]["top"])
    main("apply", "proc", "Snapshot graph; apply the edit;\nre-sort stages (Kahn); type-check",
         0.75, 7)
    main("valid", "dec", "Edit valid?", 0.8)
    side_from("valid", "rollback", "Roll back to the\nsnapshot; report E601", 0.6, "no")
    main("seed", "proc", "Seed W with changed record IDs;\nstages read by name: full scan", 0.75, 8,
         label="yes")
    main("visit", "proc", "Visit the next dirty stage v\nin topological order", 0.75, 9)
    main("big", "dec", "Cost model:\npatch cheaper?", 1.05)
    side_from("big", "full", "One full pass; memo\noff if lookups miss", 0.6, "no")
    main("patch", "proc", "Patch only changed records (binary\nsearch; int sums swap one term)",
         0.75, 10, label="yes")
    main("hash", "proc", "Update collection hash in O(1)", 0.55, 11)
    f = nodes["full"]
    path([(SX, f["bot"]), (SX, nodes["hash"]["yc"]), (CX + BW / 2, nodes["hash"]["yc"])])
    main("chg", "dec", "Output hash\nchanged?", 1.05)
    side_from("chg", "cut", "Early cutoff:\nconsumers untouched", 0.6, "no")
    main("fwd", "proc", "Bump version; mark consumers\ndirty; forward changed IDs", 0.75, 12,
         label="yes")
    main("more", "dec", "More dirty\nstages?", 1.05)
    c, m = nodes["cut"], nodes["more"]
    path([(SX, c["bot"]), (SX, m["yc"]), (CX + m["w"] / 2, m["yc"])])
    # loop back to step 9
    xl = 0.85
    path([(CX - m["w"] / 2, m["yc"]), (xl, m["yc"]), (xl, nodes["visit"]["yc"]),
          (CX - BW / 2, nodes["visit"]["yc"])], "yes", (xl + 0.42, m["yc"] + 0.2))
    main("plan", "proc", "Refresh changed outputs;\nprint the rebuild plan", 0.75, 13, label="no")
    main("edits", "dec", "More edits?", 0.8)
    e = nodes["edits"]
    xl2 = 0.42
    path([(CX - e["w"] / 2, e["yc"]), (xl2, e["yc"]), (xl2, nodes["read"]["yc"]),
          (CX - (BW - 0.6) / 2 - 0.2, nodes["read"]["yc"])], "yes", (xl2 + 0.42, e["yc"] + 0.2))
    main("verify", "proc", "Optional verify: rewrite source,\nrerun from scratch, compare",
         0.75, 14, label="no")
    main("end", "term", "End", 0.5)
    b_bot = nodes["end"]["bot"] - 0.2
    band("B.  Each retroactive edit", b_top, b_bot)
    lo = b_bot - 0.1
    ax.set_ylim(lo, H)
    fig.set_size_inches(COL_W, COL_W * (H - lo) / W)
    save(fig, "fig2_flowchart.png")


# --- Fig. 3: performance matrix ------------------------------------------------
def fig_matrix():
    import numpy as np
    from matplotlib.colors import LinearSegmentedColormap, TwoSlopeNorm
    work = [("W1-10k", "W1 10k"), ("W1-100k", "W1 100k"), ("W1-1M", "W1 1M"),
            ("W2", "W2 join"), ("W3", "W3 12 st."), ("W4", "W4 1M")]
    edits = ["E-A", "E-B", "E-C", "E-D", "E-E", "E-F"]
    heads = ["modify\n1", "modify\nbatch", "insert\nstart", "insert\nend", "delete\nmiddle", "insert\nidentity"]
    # light grey at parity, deepening green with speedup; slowdowns would be orange
    cmap = LinearSegmentedColormap.from_list("speed", ["#eb6834", "#f6f5f1", "#1baf7a", "#0d6b4a"],
                                             N=256)
    # colour by log2(speedup): parity (1x) is the neutral midpoint
    norm = TwoSlopeNorm(vmin=-2.0, vcenter=0.0, vmax=6.0)
    fig, axes = plt.subplots(2, 1, figsize=(COL_W, 4.1))
    for ax, (num, title) in zip(axes, [("b0", "Speedup over full rerun (B0 / B2)"),
                                       ("b1", "Speedup over stage-level reuse (B1 / B2)")]):
        m = np.full((len(work), len(edits)), np.nan)
        for i, (w, _) in enumerate(work):
            for j, e in enumerate(edits):
                if (w, e) in rows:
                    v = ratio(w, e, num, "b2")
                    if v is not None:
                        m[i, j] = v
        ax.imshow(np.log2(np.where(np.isnan(m), 1.0, m)), cmap=cmap, norm=norm, aspect="auto")
        for i in range(len(work)):
            for j in range(len(edits)):
                v = m[i, j]
                if np.isnan(v):
                    ax.add_patch(plt.Rectangle((j - 0.5, i - 0.5), 1, 1, facecolor="#e4e3df",
                                               edgecolor="white", hatch="////", linewidth=0))
                    ax.text(j, i, "n/a", ha="center", va="center", fontsize=5.6, color=INK2)
                    continue
                txt = f"{v:.0f}×" if v >= 100 else (f"{v:.1f}×" if v >= 10 else f"{v:.2f}×")
                dark = v >= 12
                ax.text(j, i, txt, ha="center", va="center", fontsize=5.8,
                        color="white" if dark else INK, fontweight="bold" if v >= 1.0 else "normal")
        ax.set_xticks(range(len(edits)))
        ax.set_xticklabels(heads, fontsize=5.6)
        ax.set_yticks(range(len(work)))
        ax.set_yticklabels([lbl for _, lbl in work], fontsize=6)
        ax.tick_params(length=0)
        ax.set_xticks(np.arange(-0.5, len(edits)), minor=True)
        ax.set_yticks(np.arange(-0.5, len(work)), minor=True)
        ax.grid(which="minor", color="white", linewidth=1.2)
        ax.tick_params(which="minor", length=0)
        for sp in ax.spines.values():
            sp.set_visible(False)
        ax.set_title(title, fontsize=6.8, pad=3)
        ax.xaxis.tick_top()
    fig.tight_layout(h_pad=1.0)
    save(fig, "fig3_matrix.png")


fig_flowchart()
fig_matrix()
fig_architecture()
fig_single_edit()
fig_scenarios("b0", "b2", "Speedup over full rerun", "fig3_scenarios_b0.png")
fig_scaling()
fig_scenarios("b1", "b2", "Speedup over stage-level (B1/B2)", "fig5_scenarios_b1.png")
fig_before_after()
print("figures written to", OUT)
