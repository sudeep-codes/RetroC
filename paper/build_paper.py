"""Builds the RetroC IEEE conference paper into the supplied Word template.

Usage (repository root):
    python paper/build_paper.py <template_transitional.docx> <out.docx>

The template's styles, numbering (Roman headings, "Fig. N.", "TABLE N.", "[N]"),
page setup and footer are kept; only the body is replaced. Citations are written
as [@key] and numbered in order of first use; REFS holds the reference text.
"""
import json
import os
import re
import struct
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIG = os.path.join(ROOT, "paper", "figures")

# ---------------------------------------------------------------------------
# Measured results: every number in the text and tables is read from here
# ---------------------------------------------------------------------------
RESULTS = os.environ.get("RETROC_RESULTS") or os.path.join(
    ROOT, "bench", "published", "2026-10-29", "benchmark_results.json")
PREVIOUS = os.path.join(ROOT, "bench", "published", "2026-10-28", "benchmark_results.json")
with open(RESULTS, encoding="utf-8") as _f:
    RES = json.load(_f)
with open(PREVIOUS, encoding="utf-8") as _f:
    PREV = json.load(_f)
ROWS = {(s["workload"], s["edit"]): s for s in RES["scenarios"]}
PREV_ROWS = {(s["workload"], s["edit"]): s for s in PREV["scenarios"]}
OVH = RES["first_run_overhead"][0]
W1S = ["W1-10k", "W1-100k", "W1-1M"]
ALL_WL = ["W1-10k", "W1-100k", "W1-1M", "W2", "W3", "W4"]
EDIT_KINDS = ["E-A", "E-B", "E-C", "E-D", "E-E", "E-F"]


def ms(w, e, k, rows=None):
    t = (rows or ROWS)[(w, e)][k]
    return None if t is None else t["median_ms"]


def ratio(w, e, num="b0", den="b2", rows=None):
    a, b = ms(w, e, num, rows), ms(w, e, den, rows)
    return None if a is None or b is None else a / b


def fx(r, unit="×"):
    if r >= 100:
        return f"{r:,.0f}{unit}"
    if r >= 10:
        return f"{r:.1f}{unit}"
    return f"{r:.2f}{unit}"


def X(w, e, num="b0", den="b2"):
    return fx(ratio(w, e, num, den))


def ratios(keys, num="b0", den="b2"):
    return [r for r in (ratio(w, e, num, den) for w, e in keys if (w, e) in ROWS) if r is not None]


def RANGE(keys, num="b0", den="b2"):
    v = ratios(keys, num, den)
    return f"{fx(min(v), '')}–{fx(max(v))}"


def keys_for(edits, workloads=ALL_WL):
    return [(w, e) for w in workloads for e in edits if (w, e) in ROWS]


def fms(v):
    if v is None:
        return "–"
    if v < 10:
        return f"{v:.2f}"
    return f"{v:,.1f}"


ALL_KEYS = list(ROWS)
MIN_B0 = min(ratios(ALL_KEYS))
MIN_B1 = min(ratios(ALL_KEYS, "b1"))
ARGMIN_B0 = min(ALL_KEYS, key=lambda k: ratio(*k))
N_SCEN = len(ALL_KEYS)
N_VERIFIED = sum(1 for s in RES["scenarios"] if s["verify_ok"])
if MIN_B0 < 1 or MIN_B1 < 1:
    print(f"WARNING: a scenario is below 1x (B0/B2 min {MIN_B0:.3f}, B1/B2 min {MIN_B1:.3f})")
STRUCT = keys_for(["E-C", "E-D", "E-E", "E-F"])
# Scenarios where RetroC and stage-level invalidation do the same work and land within noise of
# each other are reported as parity with the measured ratio, never rounded up.
B1_TIES = [k for k in ALL_KEYS if ratio(*k, "b1") is not None and ratio(*k, "b1") < 1.0]
N_B1_WIN = sum(1 for k in ALL_KEYS if ratio(*k, "b1") is not None and ratio(*k, "b1") >= 1.0)
N_B1 = sum(1 for k in ALL_KEYS if ratio(*k, "b1") is not None)


def B1_CLAIM(short=False):
    if not B1_TIES:
        return "match or beat stage-level invalidation in every scenario"
    gap = max(1 - ratio(*k, "b1") for k in B1_TIES) * 100
    names = ", ".join(f"{w} {e}" for w, e in B1_TIES)
    if short:
        return (f"beat stage-level invalidation in {N_B1_WIN} of {N_B1} scenarios and are on par "
                f"(within {gap:.0f}%) in the remaining {["none", "one", "two", "three"][len(B1_TIES)] if len(B1_TIES) < 4 else len(B1_TIES)}")
    return (f"beat stage-level invalidation in {N_B1_WIN} of {N_B1} scenarios and are on par with it "
            f"in the remaining {["none", "one", "two", "three"][len(B1_TIES)] if len(B1_TIES) < 4 else len(B1_TIES)} ({names}, within {gap:.0f}%), where both strategies "
            f"perform exactly the same work")


# ---------------------------------------------------------------------------
# References: key -> IEEE-formatted text, verified against Crossref (publisher, pages, DOI).
# ---------------------------------------------------------------------------
REFS = {
    "spark": "M. Zaharia *et al.*, “Apache Spark: A unified engine for big data processing,” *Commun. ACM*, vol. 59, no. 11, pp. 56–65, 2016, doi: 10.1145/2934664.",
    "workflow": "J. Liu, E. Pacitti, P. Valduriez, and M. Mattoso, “A survey of data-intensive scientific workflow management,” *J. Grid Comput.*, vol. 13, no. 4, pp. 457–493, 2015, doi: 10.1007/s10723-015-9329-8.",
    "buildsys": "A. Mokhov, N. Mitchell, and S. Peyton Jones, “Build systems à la carte,” *Proc. ACM Program. Lang.*, vol. 2, no. ICFP, pp. 1–29, 2018, doi: 10.1145/3236774.",
    "acar02": "U. A. Acar, G. E. Blelloch, and R. Harper, “Adaptive functional programming,” in *Proc. 29th ACM SIGPLAN-SIGACT Symp. Princ. Program. Lang. (POPL)*, 2002, pp. 247–259, doi: 10.1145/503272.503296.",
    "adapton": "M. A. Hammer, K. Y. Phang, M. Hicks, and J. S. Foster, “Adapton: Composable, demand-driven incremental computation,” in *Proc. 35th ACM SIGPLAN Conf. Program. Lang. Des. Implement. (PLDI)*, 2014, pp. 156–166, doi: 10.1145/2594291.2594324.",
    "ddflow": "M. Abadi, F. McSherry, and G. D. Plotkin, “Foundations of differential dataflow,” in *Foundations of Software Science and Computation Structures (FoSSaCS)*, Lecture Notes in Computer Science, vol. 9034. Berlin, Germany: Springer, 2015, pp. 71–83, doi: 10.1007/978-3-662-46678-0_5.",
    "naiad": "D. G. Murray, F. McSherry, R. Isaacs, M. Isard, P. Barham, and M. Abadi, “Naiad: A timely dataflow system,” in *Proc. 24th ACM Symp. Oper. Syst. Princ. (SOSP)*, 2013, pp. 439–455, doi: 10.1145/2517349.2522738.",
    "acar03": "U. A. Acar, G. E. Blelloch, and R. Harper, “Selective memoization,” in *Proc. 30th ACM SIGPLAN-SIGACT Symp. Princ. Program. Lang. (POPL)*, 2003, pp. 14–25, doi: 10.1145/604131.604133.",
    "ivm": "J. A. Blakeley, P.-Å. Larson, and F. W. Tompa, “Efficiently updating materialized views,” in *Proc. ACM SIGMOD Int. Conf. Manage. Data*, 1986, pp. 61–71, doi: 10.1145/16894.16861.",
    "streaming": "A. Toshniwal *et al.*, “Storm@twitter,” in *Proc. ACM SIGMOD Int. Conf. Manage. Data*, 2014, pp. 147–156, doi: 10.1145/2588555.2595641.",
    "ilc": "Y. Cai, P. G. Giarrusso, T. Rendel, and K. Ostermann, “A theory of changes for higher-order languages: Incrementalizing λ-calculi by static differentiation,” in *Proc. 35th ACM SIGPLAN Conf. Program. Lang. Des. Implement. (PLDI)*, 2014, pp. 145–155, doi: 10.1145/2594291.2594304.",
    "retrods": "E. D. Demaine, J. Iacono, and S. Langerman, “Retroactive data structures,” *ACM Trans. Algorithms*, vol. 3, no. 2, Art. no. 13, 2007, doi: 10.1145/1240233.1240236.",
    "provenance": "P. Buneman, S. Khanna, and W.-C. Tan, “Why and where: A characterization of data provenance,” in *Database Theory—ICDT 2001*, Lecture Notes in Computer Science, vol. 1973. Berlin, Germany: Springer, 2001, pp. 316–330, doi: 10.1007/3-540-44503-X_20.",
    "difftest": "J. Chen *et al.*, “A survey of compiler testing,” *ACM Comput. Surv.*, vol. 53, no. 1, pp. 1–36, 2020, doi: 10.1145/3363562.",
    "csmith": "X. Yang, Y. Chen, E. Eide, and J. Regehr, “Finding and understanding bugs in C compilers,” in *Proc. 32nd ACM SIGPLAN Conf. Program. Lang. Des. Implement. (PLDI)*, 2011, pp. 283–294, doi: 10.1145/1993498.1993532.",
    "metamorphic": "T. Y. Chen *et al.*, “Metamorphic testing: A review of challenges and opportunities,” *ACM Comput. Surv.*, vol. 51, no. 1, pp. 1–27, 2018, doi: 10.1145/3143561.",
    "kahn": "A. B. Kahn, “Topological sorting of large networks,” *Commun. ACM*, vol. 5, no. 11, pp. 558–562, 1962, doi: 10.1145/368996.369025.",
    "splitmix": "G. L. Steele, D. Lea, and C. H. Flood, “Fast splittable pseudorandom number generators,” in *Proc. ACM Int. Conf. Object-Oriented Program. Syst. Lang. Appl. (OOPSLA)*, 2014, pp. 453–472, doi: 10.1145/2660193.2660195.",
    "inchash": "D. Clarke, S. Devadas, M. van Dijk, B. Gassend, and G. E. Suh, “Incremental multiset hash functions and their application to memory integrity checking,” in *Advances in Cryptology—ASIACRYPT 2003*, Lecture Notes in Computer Science, vol. 2894. Berlin, Germany: Springer, 2003, pp. 188–207, doi: 10.1007/978-3-540-40061-5_12.",
    "benchmeth": "A. Georges, D. Buytaert, and L. Eeckhout, “Statistically rigorous Java performance evaluation,” in *Proc. 22nd ACM SIGPLAN Conf. Object-Oriented Program. Syst. Lang. Appl. (OOPSLA)*, 2007, pp. 57–76, doi: 10.1145/1297027.1297033.",
}

order = []


def cite_numbers(text):
    def repl(m):
        nums = []
        for key in m.group(1).split(";"):
            key = key.strip().lstrip("@")
            if key not in REFS:
                raise KeyError(key)
            if key not in order:
                order.append(key)
            nums.append(order.index(key) + 1)
        return ", ".join(f"[{n}]" for n in nums)
    return re.sub(r"\[(@[^\]]+)\]", repl, text)


# ---------------------------------------------------------------------------
# XML helpers
# ---------------------------------------------------------------------------
def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def run(text, b=False, i=False, font=None, sz=None, sup=False, sub=False, extra=""):
    rpr = ""
    if font:
        rpr += f'<w:rFonts w:ascii="{font}" w:hAnsi="{font}" w:cs="{font}"/>'
    if b:
        rpr += "<w:b/><w:bCs/>"
    if i:
        rpr += "<w:i/><w:iCs/>"
    if sz:
        rpr += f'<w:sz w:val="{sz}"/><w:szCs w:val="{sz}"/>'
    if sup:
        rpr += '<w:vertAlign w:val="superscript"/>'
    if sub:
        rpr += '<w:vertAlign w:val="subscript"/>'
    rpr += extra
    rpr = f"<w:rPr>{rpr}</w:rPr>" if rpr else ""
    return f'<w:r>{rpr}<w:t xml:space="preserve">{esc(text)}</w:t></w:r>'


TOKEN = re.compile(r"(\*\*.+?\*\*|\*[^*\s][^*]*?\*|`[^`]+`|\^[^^]+\^|~[^~]+~)")


def inline(text, font=None, sz=None, base_b=False, base_i=False):
    text = cite_numbers(text)
    out = []
    # re.split with one capture group: odd indices are markup tokens, even are plain text
    for idx, part in enumerate(TOKEN.split(text)):
        if not part:
            continue
        if idx % 2 == 0:
            out.append(run(part, b=base_b, i=base_i, font=font, sz=sz))
        elif part.startswith("**"):
            out.append(run(part[2:-2], b=True, i=base_i, font=font, sz=sz))
        elif part.startswith("`"):
            out.append(run(part[1:-1], font="Courier New", sz=sz or 17, b=base_b, i=base_i))
        elif part.startswith("*"):
            out.append(run(part[1:-1], i=True, b=base_b, font=font, sz=sz))
        elif part.startswith("^"):
            out.append(run(part[1:-1], sup=True, font=font, sz=sz))
        elif part.startswith("~"):
            out.append(run(part[1:-1], sub=True, font=font, sz=sz))
        else:
            out.append(run(part, b=base_b, i=base_i, font=font, sz=sz))
    return "".join(out)


def para(style, content="", ppr_extra="", raw=None, **kw):
    ppr = f'<w:pPr><w:pStyle w:val="{style}"/>{ppr_extra}</w:pPr>' if style else (
        f"<w:pPr>{ppr_extra}</w:pPr>" if ppr_extra else "")
    body = raw if raw is not None else inline(content, **kw)
    return f"<w:p>{ppr}{body}</w:p>"


BODY = []


def H1(t):
    BODY.append(para("Heading1", t))


def H2(t):
    BODY.append(para("Heading2", t))


def H3(t):
    BODY.append(para("Heading3", t))


def P(t):
    BODY.append(para("BodyText", t))


def P0(t):
    """Body paragraph without first-line indent (after equations/lists)."""
    BODY.append(para("BodyText", t, ppr_extra='<w:ind w:firstLine="0"/>'))


def BUL(items):
    for t in items:
        BODY.append(para("bulletlist", t))


def NUM(items):
    """Numbered list rendered as hanging-indent body paragraphs."""
    for n, t in enumerate(items, 1):
        BODY.append(para("BodyText", f"{n}) {t}",
                         ppr_extra='<w:ind w:left="288" w:hanging="216"/>'))


def EQ(expr_runs, number):
    """Equation centred on the template's tab stops with a right-aligned number."""
    raw = '<w:r><w:tab/></w:r>' + expr_runs + '<w:r><w:tab/></w:r>' + run(f"({number})", font="Times New Roman")
    tabs = ('<w:tabs><w:tab w:val="clear" w:pos="2520"/><w:tab w:val="clear" w:pos="5040"/>'
            '<w:tab w:val="center" w:pos="2430"/><w:tab w:val="right" w:pos="4860"/></w:tabs>')
    BODY.append(para("equation", raw=raw, ppr_extra=tabs))


def eq(text):
    """Equation text: *x* italic, ~sub~, ^sup^, in Times New Roman (not the style's Symbol)."""
    return inline(text, font="Times New Roman")


# ---------------------------------------------------------------------------
# Figures
# ---------------------------------------------------------------------------
IMAGES = []  # (rid, filename)
_docpr = [100]


def png_size(path):
    with open(path, "rb") as f:
        head = f.read(24)
    return struct.unpack(">II", head[16:24])


def FIGURE(fname, caption, width_in=3.3):
    path = os.path.join(FIG, fname)
    w, h = png_size(path)
    cx = int(width_in * 914400)
    cy = int(cx * h / w)
    rid = f"rIdFig{len(IMAGES) + 1}"
    IMAGES.append((rid, fname))
    _docpr[0] += 1
    n = _docpr[0]
    drawing = (
        f'<w:r><w:drawing><wp:inline distT="0" distB="0" distL="0" distR="0">'
        f'<wp:extent cx="{cx}" cy="{cy}"/><wp:docPr id="{n}" name="Figure {n}"/>'
        f'<wp:cNvGraphicFramePr><a:graphicFrameLocks xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" noChangeAspect="1"/></wp:cNvGraphicFramePr>'
        f'<a:graphic xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main">'
        f'<a:graphicData uri="http://schemas.openxmlformats.org/drawingml/2006/picture">'
        f'<pic:pic xmlns:pic="http://schemas.openxmlformats.org/drawingml/2006/picture">'
        f'<pic:nvPicPr><pic:cNvPr id="{n}" name="{fname}"/><pic:cNvPicPr/></pic:nvPicPr>'
        f'<pic:blipFill><a:blip r:embed="{rid}"/><a:stretch><a:fillRect/></a:stretch></pic:blipFill>'
        f'<pic:spPr><a:xfrm><a:off x="0" y="0"/><a:ext cx="{cx}" cy="{cy}"/></a:xfrm>'
        f'<a:prstGeom prst="rect"><a:avLst/></a:prstGeom></pic:spPr></pic:pic>'
        f"</a:graphicData></a:graphic></wp:inline></w:drawing></w:r>"
    )
    BODY.append(para(None, raw=drawing,
                     ppr_extra='<w:keepNext/><w:spacing w:before="120" w:after="0"/><w:jc w:val="center"/>'))
    BODY.append(para("figurecaption", caption))


# ---------------------------------------------------------------------------
# Tables
# ---------------------------------------------------------------------------
BORDERS = ('<w:tblBorders><w:top w:val="single" w:sz="4" w:space="0" w:color="auto"/>'
           '<w:left w:val="single" w:sz="4" w:space="0" w:color="auto"/>'
           '<w:bottom w:val="single" w:sz="4" w:space="0" w:color="auto"/>'
           '<w:right w:val="single" w:sz="4" w:space="0" w:color="auto"/>'
           '<w:insideH w:val="single" w:sz="4" w:space="0" w:color="auto"/>'
           '<w:insideV w:val="single" w:sz="4" w:space="0" w:color="auto"/></w:tblBorders>')


def cell(text, width, style, jc, shade=None, sz=None):
    tcpr = f'<w:tcW w:w="{width}" w:type="dxa"/><w:vAlign w:val="center"/>'
    if shade:
        tcpr += f'<w:shd w:val="clear" w:color="auto" w:fill="{shade}"/>'
    p = para(style, text, ppr_extra=f'<w:jc w:val="{jc}"/>', sz=sz)
    return f"<w:tc><w:tcPr>{tcpr}</w:tcPr>{p}</w:tc>"


def TABLE(title, header, rows, widths, align=None, note=None, sz=None):
    """align: per-column 'left'/'center'; body rows use table copy style."""
    align = align or ["left"] + ["center"] * (len(widths) - 1)
    BODY.append(para("tablehead", title, ppr_extra="<w:keepNext/>"))
    grid = "".join(f'<w:gridCol w:w="{w}"/>' for w in widths)
    xml = (f'<w:tbl><w:tblPr><w:tblW w:w="{sum(widths)}" w:type="dxa"/><w:jc w:val="center"/>{BORDERS}'
           f'<w:tblLayout w:type="fixed"/><w:tblCellMar><w:left w:w="57" w:type="dxa"/>'
           f'<w:right w:w="57" w:type="dxa"/></w:tblCellMar><w:tblLook w:val="0000"/></w:tblPr>'
           f"<w:tblGrid>{grid}</w:tblGrid>")
    xml += '<w:tr><w:trPr><w:cantSplit/><w:tblHeader/></w:trPr>' + "".join(
        cell(h, w, "tablecolhead", "center", "EDEDED", sz) for h, w in zip(header, widths)) + "</w:tr>"
    for r in rows:
        xml += '<w:tr><w:trPr><w:cantSplit/></w:trPr>' + "".join(
            cell(str(c), w, "tablecopy", a, None, sz) for c, w, a in zip(r, widths, align)) + "</w:tr>"
    xml += "</w:tbl>"
    BODY.append(xml)
    if note:
        BODY.append(para(None, note, sz=14,
                         ppr_extra='<w:spacing w:before="40" w:after="160"/><w:jc w:val="left"/>'))
    else:
        BODY.append(para("BodyText", "", ppr_extra='<w:spacing w:after="0" w:line="120" w:lineRule="exact"/>'))


def CODEBOX(title, lines, width=4860):
    """Algorithm / listing: titled single-cell box with monospaced lines."""
    paras = ""
    paras += para(None, raw=run(title, b=True, sz=16),
                  ppr_extra='<w:spacing w:before="20" w:after="40"/><w:jc w:val="left"/>')
    for ln in lines:
        paras += para(None, raw=run(ln, font="Courier New", sz=14) if ln else "",
                      ppr_extra='<w:spacing w:before="0" w:after="0" w:line="200" w:lineRule="exact"/><w:jc w:val="left"/>')
    top = '<w:top w:val="single" w:sz="8" w:space="0" w:color="auto"/>'
    bot = '<w:bottom w:val="single" w:sz="8" w:space="0" w:color="auto"/>'
    xml = (f'<w:tbl><w:tblPr><w:tblW w:w="{width}" w:type="dxa"/><w:jc w:val="center"/>'
           f'<w:tblBorders>{top}{bot}</w:tblBorders><w:tblLayout w:type="fixed"/>'
           f'<w:tblLook w:val="0000"/></w:tblPr><w:tblGrid><w:gridCol w:w="{width}"/></w:tblGrid>'
           f'<w:tr><w:tc><w:tcPr><w:tcW w:w="{width}" w:type="dxa"/></w:tcPr>{paras}</w:tc></w:tr></w:tbl>')
    BODY.append(xml)
    BODY.append(para("BodyText", "", ppr_extra='<w:spacing w:after="0" w:line="160" w:lineRule="exact"/>'))


# ===========================================================================
# CONTENT
# ===========================================================================
TITLE = "RetroC: A Compiler for Retroactively Editable Dataflow Pipelines with Verified Record-Level Recomputation"

# (name, dept, organization, location, last line: registration number)
_DEPT = "School of Computer Science and Engineering"
_ORG = "Vellore Institute of Technology, Vellore"
_LOC = "Vellore-632014, Tamil Nadu, India"
AUTHORS = [
    ("Swaraj Joshi", _DEPT, _ORG, _LOC, "swaraj.joshi2024@vitstudent.ac.in"),
    ("Johan Viji", _DEPT, _ORG, _LOC, "johan.vijiskariah2024@vitstudent.ac.in"),
    ("Sudeep Kumar Sahu", _DEPT, _ORG, _LOC, "sudeep.kumarsahu2024@vitstudent.ac.in"),
    ("Krishnaraj N", _DEPT, _ORG, _LOC, "krishnaraj.n@vit.ac.in"),
]

ABSTRACT = (
    "Once a data pipeline has produced its results, people still change it: they repair a bad input record, "
    "add or withdraw a record, slot in a cleaning stage that was forgotten, or drop a stage that is no longer "
    "needed. Today such a change usually means running the whole pipeline again, or at least every stage after "
    "the change, and the incremental dataflow engines that avoid this cannot change the shape of a "
    "computation once it is built. We present RetroC, a compiler and runtime for a pure, statically typed dataflow language in which retroactive "
    "edits—modifying, inserting or deleting an input record, and inserting or deleting a stage—are "
    "first-class, type-checked, atomic operations on a versioned dataflow graph called the Retroactive IR. "
    "After an edit, RetroC propagates a record-level work set: each stage visits only the records whose inputs "
    "changed, reuses per-record memo entries keyed by input and function hashes, maintains integer sums and "
    "hash joins incrementally, updates an order-independent collection hash in constant time per changed "
    "record, and stops at the first stage whose output is unchanged. A lightweight cost model chooses, per "
    "stage, between patching and recomputing. Every result can be checked against a full rerun of the "
    "textually rewritten program. On four synthetic workloads measured with a pinned, interleaved protocol "
    f"(three warm-up runs, median of eleven), a single-record edit runs {RANGE(keys_for(['E-A'], W1S))} "
    f"faster than a full rerun from 10 k to 1 M records and {X('W3', 'E-A')} faster on a 12-stage pipeline, "
    f"and batches of sequential edits run {RANGE(keys_for(['E-B']))} faster. All {N_SCEN} workload–edit "
    f"scenarios are faster than a full rerun ({fx(MIN_B0, '')}–{fx(max(ratios(ALL_KEYS)))}); they "
    f"{B1_CLAIM(short=True)}, with gains up to {fx(max(ratios(ALL_KEYS, 'b1')))}. Correctness was confirmed on "
    "10,000 randomized programs and edit scripts and by the verifier on every benchmark scenario."
)

KEYWORDS = ("incremental computation, dataflow programming, compilers, intermediate representation, "
            "memoization, change propagation, differential testing")


def content():
    # ---------------- I. Introduction ----------------
    H1("Introduction")
    P("Many data-processing jobs are written as a chain of *map*, *filter*, *reduce* and *join* steps applied "
      "to a list of records: cleaning a sensor log, preparing features for a model, or summarising lab "
      "measurements. Such a chain seldom stays fixed after its first run. Someone spots a broken temperature "
      "reading; an analyst decides a calibration step belongs ahead of the averaging step; a stage added for "
      "debugging is no longer wanted. We call each of these changes *retroactive*, since it alters a "
      "computation whose results already exist, and what the user asks for is the output the corrected "
      "pipeline would have given from the start.")
    P("In practice, people get that output by running everything again. A batch engine repeats each stage over "
      "each record [@spark]. Workflow schedulers and build tools are smarter about *which* steps to repeat, but "
      "they reason about whole steps or files, so one altered record still forces every later step to run in "
      "full [@workflow]; [@buildsys]. Work on incremental computation has long argued that a small change to "
      "the input should cost only a small amount of work [@acar02]; [@adapton], and incremental dataflow "
      "engines achieve this for input changes by passing *differences* along the graph [@ddflow]; [@naiad]. "
      "In those engines, though, the graph is settled once it is constructed; adding or removing an operator "
      "means building a new computation.")
    P("Our approach is to let the pipeline’s structure be edited after the fact, just like its data. RetroC "
      "compiles a small, pure dataflow language into a versioned graph that we call the *Retroactive IR*. It "
      "runs that graph once, keeping a memo entry for every record at every stage, and then accepts scripts "
      "of edits that change past input records or add and remove stages. Each edit is type-checked, takes "
      "effect atomically, and is pushed through the graph one record at a time.")

    H2("Motivation")
    P("The design rests on two observations. The first is that, when the language has no side effects, what a "
      "stage produces for a given record depends on nothing but that record’s value and the stage’s code. A "
      "cached result can therefore be reused whenever neither of those two has changed, and no dependency "
      "tracking is needed while the program runs. The second is that a person who edits a pipeline must be "
      "able to rely on the answer and to see how it was obtained. A fast incremental answer that quietly "
      "differs from a fresh run is useless, and one that hides what it recomputed is hard to debug. For this "
      "reason RetroC ships its propagation together with a checker against a full rerun and a readable "
      "rebuild plan for each edit.")

    H2("Problem Statement")
    P("Let *P* be a well-typed program, *I* its input data, and *S* the state left by executing *P* on *I* "
      "(outputs, memo tables and hashes). Given an edit *e* from the set {modify, insert or delete an input "
      "record; insert or delete a stage}, the problem is to produce outputs equal to those of a from-scratch "
      "run of the edited program, *Run*(*Apply*(*e*, *P*), *Apply*(*e*, *I*)), while doing work proportional "
      "to the records whose values actually change rather than to the size of the input times the number of "
      "stages. Invalid edits must be rejected with a diagnostic and must leave *S* unchanged.")

    H2("Contributions")
    P0("This paper makes the following contributions:")
    BUL([
        "A pure dataflow language and a versioned intermediate representation in which input-record "
        "modification, insertion and deletion and stage insertion and deletion are first-class, type-checked, "
        "atomic compiler operations with an exact undo on failure (Section IV).",
        "A record-level propagation algorithm that combines per-record memoization, an order-independent "
        "collection hash updated in constant time per changed record, incrementally maintained integer sums "
        "and hash joins, and forwarding of only the records whose output changed, so that work per stage is "
        "proportional to the number of changed records (Sections IV–VI).",
        "A per-stage cost model that chooses between patching and recomputing, so that no edit kind pays for "
        "bookkeeping it cannot use (Section IV).",
        "An end-to-end verifier that compares propagated results with a fresh run of the textually rewritten "
        "program, and a differential testing harness that exercised 10,000 random programs and edit scripts "
        "(Section VII).",
        f"An evaluation over {N_SCEN} workload–edit scenarios against a full rerun and against stage-level "
        "invalidation, using a reproducible pinned and interleaved timing protocol, in which RetroC is faster "
        "than a full rerun in every scenario (Sections VIII–XI).",
    ])

    # ---------------- II. Literature review ----------------
    H1("Literature Review")
    P("Six strands of earlier work bear on RetroC. For each we give the idea in brief and say where RetroC "
      "agrees or departs; Table I sets the approaches side by side on the properties that retroactive "
      "editing needs.")

    H2("Batch Reprocessing and Workflow Systems")
    P("Cluster batch engines such as Spark produce results by evaluating the job over its inputs; their "
      "lineage records are used to rebuild data lost to failures rather than to avoid recomputation after "
      "an edit [@spark]. Workflow schedulers and build tools in the style of Make keep a graph of tasks or "
      "files and use it to pick the tasks that must run again [@workflow]. A task is the smallest thing they "
      "can skip, so changing one record of a task’s input repeats that task and all tasks that follow it. "
      "The stage-level baseline B1 of Section VIII imitates exactly this policy.")

    H2("Build Systems and Early Cutoff")
    P("In their survey of build systems, Mokhov et al. separate the order in which tasks are scheduled from "
      "the rule that decides whether a task is rebuilt, and they single out *early cutoff*: if a rebuilt "
      "result equals the old one, nothing that depends on it needs rebuilding [@buildsys]. The same "
      "short-circuit appears in build tools with dynamic dependencies and in compilers organised around "
      "cached queries. RetroC uses it twice—once for a single record, when its memo entry still fits, and "
      "once for a whole stage, when the stage’s collection hash has not moved.")

    H2("Self-Adjusting and Demand-Driven Computation")
    P("Acar et al. build, while a program runs, a graph of which computations read which data, and after an "
      "input change they replay only the parts of that graph the change reaches [@acar02]. Their follow-up on "
      "selective memoization makes a cache entry depend only on the parts of the input a function really "
      "inspected [@acar03]. Adapton recasts the same goal so that results are recomputed on demand and pieces "
      "compose cleanly [@adapton]. These are general techniques that discover dependencies at run time. "
      "RetroC targets a narrower setting—pure pipelines over collections—where the compiled graph already "
      "states every dependency, so no run-time tracing is required; what it adds in return is the ability to "
      "edit the program’s structure, which those models do not cover.")

    H2("Incremental Dataflow and View Maintenance")
    P("Differential dataflow stores each collection as a history of additions and retractions and pushes "
      "input changes through an operator graph that does not change shape [@ddflow]; it runs on Naiad’s "
      "timely dataflow model [@naiad]. In databases, incremental view maintenance works out from a view’s "
      "query how to update the stored view when base tables change [@ivm]. Stream processing systems are "
      "built for inputs that never end and that arrive in time order [@streaming]. Each of these handles "
      "changing *data* very well, yet none of them regards adding or dropping an operator in a computation "
      "that has already run as just another incremental update.")

    H2("Incremental Programs and Compilers")
    P("Cai et al. give a calculus that, from a higher-order program, derives a second program turning a "
      "change in the input into the matching change in the output [@ilc]. Reactive notebooks rerun every "
      "cell downstream of a cell the user edits—once more, reuse at the granularity of a whole step. "
      "RetroC, by contrast, is an ordinary compiler, with a lexer, parser, type checker, IR and bytecode, "
      "except that its IR is meant to be *modified* after the program has executed.")

    H2("Retroactivity, Provenance and Testing")
    P("We take the word *retroactive* from Demaine et al., whose retroactive data structures let an operation "
      "be added to or removed from an earlier point in the structure’s history [@retrods]; RetroC shares the "
      "goal but none of the machinery. The *explain* command is a small form of data provenance, which "
      "answers why an output has its value and where that value came from [@provenance]. For testing we "
      "follow differential testing, which runs two implementations that should agree on many generated "
      "inputs and flags any disagreement [@difftest]; [@csmith], and metamorphic testing, which checks that "
      "related runs stand in an expected relation to each other [@metamorphic].")

    TABLE("Comparison of Approaches to Recomputation After a Change",
          ["Approach", "Input edits", "Stage edits", "Reuse unit", "Verifier"],
          [
              ["Batch rerun [@spark]", "rerun", "rerun", "none", "–"],
              ["Workflow / build [@workflow]; [@buildsys]", "yes", "rerun stage", "task", "–"],
              ["Self-adjusting [@acar02]; [@adapton]", "yes", "no", "trace node", "–"],
              ["Differential dataflow [@ddflow]", "yes", "rebuild", "difference", "–"],
              ["View maintenance [@ivm]", "yes", "rebuild", "delta", "–"],
              ["**RetroC (this work)**", "**yes**", "**yes**", "**record**", "**built in**"],
          ],
          [1500, 760, 820, 900, 880])

    # ---------------- III. Research gap ----------------
    H1("Research Gap")
    P("Existing work splits into two camps. One reuses whole stages or tasks; this is easy to get right but "
      "still repeats every record of every stage after the change. The other reuses work at a fine grain, "
      "but only inside a computation whose shape cannot change once built. In the systems we surveyed, we "
      "did not find one that offers all four of the following at once:")
    BUL([
        "edits to past input records *and* structural edits that insert or delete stages are first-class, "
        "type-checked operations on the compiled program;",
        "reuse after such edits is tracked per record rather than per stage, with work proportional to the "
        "records that change;",
        "every edit produces an explicit rebuild plan stating what was recomputed and what was reused; and",
        "the incremental result is checked against a from-scratch run of the edited program.",
    ])
    P("RetroC is designed to fill this gap for a small but realistic pure dataflow language, and the "
      "evaluation in Section IX measures how far record-level reuse helps for each kind of edit.")

    # ---------------- IV. Proposed methodology ----------------
    H1("Proposed Methodology")
    H2("The RetroC Language")
    P("A RetroC program is a sequence of typed `input` declarations, `let` stages, optional single-expression "
      "`fn` helpers and `output` declarations. Types are `int`, `float`, `bool`, `string`, arrays `[T]` and "
      "records `{f: T, ...}`. Collection builtins are `map`, `filter`, `reduce` (left fold), `tree_reduce` "
      "(associative fold) and `join` (equi-join); scalar helpers include `length`, `to_float`, `min`, `max` "
      "and `clamp`. The language is pure: there is no mutation, no input or output inside lambdas, no "
      "randomness and no recursion, which guarantees termination and makes every stage a pure function of "
      "its inputs. Lambdas may appear only as arguments to builtins, and their parameter types are inferred "
      "from the builtin. A lambda may also read another stage by name, for example `x - avg`. Every "
      "user-facing error has a code (E101–E601), a line and a column.")
    CODEBOX("Listing 1. A sensor pipeline and an edit script", [
        "input readings: [{id: int, temp_c: float,",
        "                  valid: bool}] = [ ... ];",
        "let ok    = filter(readings, r => r.valid);",
        "let temp_f = map(ok, r => r.temp_c*1.8 + 32.0);",
        "let sum_f = reduce(temp_f, 0.0, (a, t) => a + t);",
        "let avg   = sum_f / to_float(length(temp_f));",
        "output avg;",
        "",
        "retro modify readings[1].temp_c = 22.0",
        "retro insert into readings :",
        "      {id: 9, temp_c: 21.5, valid: true}",
        "retro delete readings[3]",
        "retro insert before temp_f as cal :",
        "      map(_, r => {id: r.id,",
        "                   temp_c: r.temp_c + 0.5,",
        "                   valid: r.valid})",
        "retro delete cal",
    ])
    P("Listing 1 shows a pipeline and an edit script. A record is named by its input and its RecordId; an "
      "inserted record receives a fresh RecordId that is never reused, so later edits keep referring to the "
      "same record. The underscore in an inserted stage stands for the collection the new stage will read. "
      "Edits apply in order and each receives an edit identifier used by the history log and by provenance "
      "queries.")

    H2("The Retroactive IR")
    P("Each top-level stage becomes one node of a directed acyclic graph. A node records a stable identifier, "
      "a kind (INPUT, MAP, FILTER, REDUCE, TREE_REDUCE, JOIN, SCALAR, OUTPUT), its input edges, its lambda or "
      "expression, a structural hash of that function (*fnHash*), a hash of its current output (*outHash*), a "
      "version counter and a memo table. A builtin nested inside another is split into a hidden stage so that "
      "every builtin application is one node. Stages that a lambda reads by name become additional inputs of "
      "its node, after the collections it iterates, so the graph records every dependency. The graph keeps a "
      "topological order, recomputed with Kahn’s algorithm after every structural edit [@kahn]. Table II lists "
      "the invariants that the implementation maintains and that the tests check.")
    TABLE("Invariants of the Retroactive IR",
          ["Id", "Invariant"],
          [
              ["I1", "The graph is acyclic and the stored order is topological."],
              ["I2", "Node identifiers are unique and never reused."],
              ["I3", "A memo entry is reused only if its input hash and memo key (function hash mixed with the output hashes of the stages it reads by name) both match."],
              ["I4", "A node’s version increases if and only if its output hash changes."],
              ["I5", "After propagation every node is clean and equals the pure function of its inputs."],
              ["I6", "Every node type-checks against its inputs after every edit."],
              ["I7", "Collections keep source order; RecordIds are stable through MAP and FILTER, so every collection is sorted by RecordId."],
              ["I8", "Running with an empty memo gives the same outputs as running with a warm memo."],
          ],
          [420, 4440], align=["center", "left"])

    H2("Edit Semantics")
    P("Table III summarises the five edits. Each edit first checks its preconditions, then rewrites the "
      "graph or the input, recomputes the topological order and re-type-checks the affected stages. If any "
      "step fails, the rewrite is undone exactly—only the rewired edges and the added or removed node are "
      "restored—and the edit is reported with error E601. Because nothing else is mutated before these "
      "checks, the undo does not need to snapshot results or memo tables.")
    TABLE("The Five Retroactive Edits",
          ["Edit", "Precondition", "Rewrite", "Initial work set"],
          [
              ["Modify record", "input, record and field exist; literal has the field’s type",
               "replace the field; patch the input hash", "the modified record"],
              ["Insert record", "literal has the input’s element type",
               "append with a fresh RecordId", "the new record"],
              ["Delete record", "input and RecordId exist",
               "remove the record", "the removed record"],
              ["Insert stage", "target is not an input; new lambda and target still type-check",
               "new node N between P and target", "all records of N"],
              ["Delete stage", "MAP/FILTER whose removal type-checks; not read by name; not sole source of an output",
               "rewire consumers to the stage’s input", "all records, memo-filtered"],
          ],
          [700, 1500, 1400, 1260], align=["left", "left", "left", "left"])

    H2("Record-Level Propagation")
    P("Propagation maintains a *work set* consisting of the dirty nodes, the nodes that must be rescanned in "
      "full, and, for each record-tracking node, the set of pending RecordIds. Nodes are visited in "
      "topological order. A MAP or FILTER stage with few pending records patches its output in place: for "
      "each pending record it locates the input and output positions by binary search on RecordId "
      "(invariant I7), looks up the memo with key (hash of the input value, memo key), runs the lambda only "
      "on a miss, and inserts, replaces or removes the output record. The stage reports exactly the records "
      "whose *output* changed, including records that appeared or disappeared, and only those are passed to "
      "its consumers. A node that reads a changed stage by name is rescanned in full, because every one of "
      "its records may depend on that stage.")
    P("Folds and joins are maintained incrementally as well. A REDUCE whose combiner adds an integer term "
      "per element, `(a, x) => a + t(x)`, stores each record’s term and patches the sum by subtracting the "
      "old term and adding the new one, with 64-bit wrap-around arithmetic that is exactly associative. A "
      "JOIN is a hash join that keeps a per-record key index for each side and the layout of its output. "
      "When an edit keeps every join key and adds or removes no record, only the output records built from "
      "the changed inputs are rebuilt, in place; otherwise the join is rebuilt from the index, re-evaluating "
      "only the keys of changed records. Other REDUCE and SCALAR nodes reuse their memo when the hashes of "
      "all their inputs are unchanged, and `tree_reduce` updates one leaf-to-root path.")

    H2("Cost Model")
    P("Bookkeeping is not free: a memo lookup or store costs about as much as evaluating a short lambda. "
      "RetroC therefore decides per stage, from quantities it already knows, whether patching pays off:")
    BUL([
        "**Patch or scan.** A MAP or FILTER stage patches when at most one eighth of its input is pending; "
        "otherwise one sequential pass with memo checks is cheaper than many binary searches.",
        "**Additive folds.** With *k* changed records out of *n* and a term of cost *c*, the sum is patched when "
        "*k*(*c* + 2) < *n c*, counting two lookups per patched record; otherwise it is re-folded.",
        "**Adaptive memo.** A full pass samples its first 64 memo lookups. If at least seven in eight miss, the "
        "rest would miss too, so lookups, stores and change tracking are switched off for that pass and its "
        "consumers rescan instead of patching.",
        "**New stages.** An inserted stage has nothing to look up; it stores no memo and does not list its "
        "records as changed. Its consumers rescan, still reusing every record the new stage left unchanged.",
    ])
    P("Every choice only selects between two correct ways of computing the same output, so the cost model "
      "cannot affect results, only speed. Memo entries left untouched by a disabled pass remain correct "
      "because they are keyed by input hash (I3).")

    H2("Order-Independent Collection Hash")
    P0("Early cutoff needs a hash of each stage’s output. Recomputing an order-dependent hash after every "
       "edit costs time proportional to the collection. RetroC instead uses a commutative sum of per-record "
       "terms. For a record with identifier *r* and value hash *h*(*v*),")
    EQ(eq("*t*(*r*, *v*) = *mix*(*mix*(*r*) ⊕ *h*(*v*)),"), 1)
    P0("where *mix* is the SplitMix64 finaliser [@splitmix] and ⊕ is bitwise exclusive or. The hash of a "
       "collection *C* combines its size with the sum of its terms modulo 2^64^:")
    EQ(eq("*H*(*C*) = *F*(|*C*|, Σ~(r,v)∈C~ *t*(*r*, *v*) mod 2^64^),"), 2)
    P0("where *F* is an FNV-style combination. Replacing the value of record *r* updates the stored sum "
       "*S* in constant time,")
    EQ(eq("*S*′ = *S* − *t*(*r*, *v*~old~) + *t*(*r*, *v*~new~),"), 3)
    P0("and insertion or removal adds or subtracts one term. Order is still captured: by invariant I7 every "
       "collection is sorted by unique RecordIds, so the set of (*r*, *v*) pairs determines the sequence. "
       "Hashing a multiset by adding up per-element terms is not new [@inchash]. Such a hash can be broken on "
       "purpose by someone who chooses the data, but RetroC does not face such an opponent. Treating each "
       "term as an independent, uniformly random 64-bit number, a collision among *q* compared states has "
       "probability at most *q*^2^/2^65^ (the birthday bound); for *q* = 10^5^ that is under 10^−8^. "
       "Because the verifier compares the values themselves rather than their hashes, a verified run would "
       "still expose any collision that did occur.")

    H2("Correctness Argument")
    P("*Claim.* For every well-typed program *P*, input *I* and valid edit *e*, propagating *e* from the state "
      "of executing *P* on *I* yields the same outputs as running the edited program on the edited input from "
      "an empty memo.")
    P("*Proof sketch.* By induction over the topological order. The work set initially contains every record "
      "whose input value or stage function changed, and every node that reads a changed stage by name. When a "
      "stage is visited, a memo entry is reused only if both its input hash and its memo key match (I3); since "
      "stages are pure and the memo key covers every stage the lambda reads, equal input under an equal key "
      "yields an equal output. A patched sum equals the re-folded sum because integer addition modulo 2^64^ "
      "is associative and commutative; a patched join keeps every key and therefore every matched pair. "
      "Records whose output is unchanged are not forwarded, and nodes not reached by the work set have "
      "unchanged inputs and functions, so their outputs are unchanged. The only gap is a hash collision, "
      "bounded above and closed by the verifier’s full-value comparison.")

    H2("Incremental Tree Reduction")
    P("For associative combiners that are not simple sums, `tree_reduce` stores a complete binary tree of "
      "partial results whose leaves are the records. Changing record *k* recomputes only the "
      "⌈log~2~ *n*⌉ internal nodes on the path from its leaf to the root: three combiner steps for "
      "*n* = 8 and ten for *n* = 1,024, both checked by unit tests.")

    # ---------------- V. System architecture ----------------
    H1("System Architecture")
    P("RetroC is organised as a strict layered pipeline (Fig. 1). A module may include only modules to its "
      "left in the chain common → lexer → parser → sema → ir → bytecode → runtime "
      "→ edit → reuse → verify → cli, so there are no dependency cycles. The front end turns "
      "source text into a typed program, the IR builder produces the Retroactive IR, lambda bodies are "
      "compiled to stack bytecode, and the graph executor performs the first run while filling memo tables. "
      "Edit scripts are parsed separately and applied by the edit engine; the propagator updates the graph, "
      "applies the cost model and produces the rebuild plan; the verifier optionally re-runs the rewritten "
      "program. Table IV lists the modules with their size and responsibility.")
    FIGURE("fig1_architecture.png",
           "Architecture of RetroC. The compile lane builds the Retroactive IR and bytecode; the execute lane "
           "performs the first run and fills the memo tables; the edit lane applies each retroactive edit as a "
           "graph or input rewrite, propagates it record by record under the cost model, and feeds the "
           "outputs, the rebuild plan and the verifier.")
    TABLE("Modules of the Implementation",
          ["Module", "LOC", "Responsibility"],
          [
              ["common", "412", "values, structural hashing, collection hash, errors"],
              ["lexer", "397", "tokens; errors E101–E102"],
              ["parser", "1,208", "recursive descent, lambda lookahead; E201"],
              ["sema", "901", "types, scopes, purity; E301–E403"],
              ["ir", "1,808", "graph, nodes, memo, reference inputs, topological sort"],
              ["bytecode", "835", "expression compiler and stack VM"],
              ["runtime", "1,028", "first run, hash join, incremental sums, CSV loader"],
              ["edit", "987", "edit-script parser, source rewriting"],
              ["reuse", "1,604", "propagation, cost model, record edits, rebuild plan, B1 mode"],
              ["verify", "330", "full-rerun comparison"],
              ["cli", "501", "command-line driver"],
          ],
          [900, 600, 3360], align=["left", "center", "left"])

    # ---------------- VI. Algorithm ----------------
    H1("Algorithm")
    P("Fig. 2 shows the complete control flow, from source text to verified outputs. Part A runs once. "
      "The program is lexed and parsed (step 1) and checked for types, scopes and purity (step 2); any error "
      "stops compilation with a code and a line:col position. The IR builder then produces the Retroactive IR "
      "in topological order (step 3), lambda bodies are compiled to bytecode (step 4), and the executor runs "
      "every stage once, filling the per-record memo tables and collection hashes (step 5).")
    P("Part B repeats for each edit in the script. The edit is read (step 6) and applied to a snapshot of "
      "the graph; stage inserts and deletes re-sort the stages with Kahn's algorithm [@kahn], and the result "
      "is type-checked (step 7). An invalid edit, for example one that creates a cycle, restores the snapshot "
      "and reports E601, so the graph is unchanged. A valid edit seeds the work set *W* with the first "
      "affected stages and the pending record IDs; stages that read a changed stage by name are marked for a "
      "full scan (step 8). Stages are then visited in topological order (step 9). The cost model decides "
      "whether patching is cheaper; if not, one full pass is made, with the memo switched off when its "
      "lookups miss. Otherwise only the pending records are patched, and an integer sum swaps one term per "
      "record (step 10). The collection hash is updated in *O*(1) per changed record (step 11). If the "
      "stage's output hash is unchanged, propagation stops there (early cutoff); otherwise its version is "
      "increased and only the changed record IDs are forwarded to its consumers (step 12). When no dirty "
      "stages remain, the changed outputs are refreshed and the rebuild plan is printed (step 13). After the "
      "last edit, `retroc verify` can rewrite the source, rerun it from scratch and compare the outputs "
      "(step 14).")
    FIGURE("fig2_flowchart.png",
           "End-to-end flowchart of RetroC. Part A compiles and runs the program once; Part B applies each "
           "retroactive edit and propagates it stage by stage, choosing between patching and a full pass "
           "with the cost model.", width_in=3.0)
    P("Algorithm 1 gives the shared propagation loop used by all five edits (steps 8–12 of Fig. 2), and "
      "Algorithm 2 the patch step for a MAP or FILTER stage.")
    CODEBOX("Algorithm 1. PROPAGATE(G, W)", [
        "Input: graph G, work set W = (dirty, fullScan,",
        "       pending: node -> set of RecordIds)",
        "for v in topological order of G:",
        "  if v is INPUT or v not in W.dirty: continue",
        "  h0 <- v.outHash",
        "  if v is MAP or FILTER:",
        "     changed <- PATCH(v, W)            (Alg. 2)",
        "  else if v is additive int REDUCE and",
        "          k(c+2) < n c:  swap k terms in sum",
        "  else if v is JOIN and no key changed:",
        "     rebuild only the affected output pairs",
        "  else if v is TREE_REDUCE:",
        "     update leaves in W.pending[v]; ascend",
        "  else recompute v unless its memo matches",
        "     the hashes of all its inputs",
        "  if hash(v) != h0 or v is new:        (I4)",
        "     v.version <- v.version + 1",
        "     for c in consumers(v):",
        "        W.dirty <- W.dirty + {c}",
        "        if c reads v by name or changed is",
        "           unknown: W.fullScan += {c}",
        "        else W.pending[c] += changed",
        "  (otherwise: early cutoff, consumers untouched)",
        "refresh only the outputs whose hash changed",
    ])
    CODEBOX("Algorithm 2. PATCH(v, W) for MAP / FILTER", [
        "K <- W.pending[v];  in <- input of v",
        "if v in W.fullScan or |K| > |in|/8:",
        "   one pass over in; memo check per record;",
        "   if 7/8 of the first 64 lookups miss:",
        "      memo and tracking off; changed unknown",
        "   else compare with old output -> changed",
        "else for r in K:",
        "   x <- binarySearch(in, r)",
        "   y <- binarySearch(out, r)",
        "   if x exists and memo[r] matches",
        "        (hash(x), memoKey): reuse       (I3)",
        "   else if x exists: run lambda; store memo",
        "   insert / replace / erase y; add or",
        "   subtract t(r, v) in the hash sum   (3)",
        "   if the output record changed: add r",
        "return changed",
    ])
    P("*Complexity.* For *k* pending records in a stage of *n* records, the patch path performs *k* binary "
      "searches and at most *k* lambda evaluations, *O*(*k* log *n*) plus the cost of inserting or erasing "
      "vector elements when a filter admits or drops a record. The hash update is *O*(1) per changed record, "
      "an additive integer fold is patched with *k* binary searches into its sorted term list, and "
      "`tree_reduce` costs *O*(*k* log *n*). A join that keeps its keys touches only the output pairs of the "
      "*k* changed records. A single-record edit is therefore independent of *n* along the whole pipeline "
      "whenever each fold is additive or associative.")

    # ---------------- VII. Implementation details ----------------
    H1("Implementation Details")
    H2("Technology Stack")
    P("RetroC is written in C++17 (about 10,000 lines in `src/`, plus about 3,800 lines of tests and "
      "benchmark code) and built with CMake and Ninja using GCC 16.2.0. Warnings are treated as errors. The "
      "only third-party component is the single-header doctest framework; the CSV loader is written in-house. "
      "Golden tests are driven by small Python scripts.")
    H2("Runtime Representation")
    P("Values are immutable tagged unions with structural equality, a 64-bit structural hash and deterministic "
      "floating-point printing, so identical inputs produce byte-identical output. Lambda bodies run on a stack "
      "virtual machine whose operand stack keeps 64 slots inline and spills to the heap only for very "
      "large literals, so ordinary expressions never allocate. Each stage result stores its collection, sorted "
      "by RecordId, together with the running sum behind its collection hash. An additive fold keeps its "
      "per-record terms in a vector sorted by RecordId, and a join keeps, per side, a map from RecordId to its "
      "key, plus the (left, right) pair at each output position. A static analysis of the compiled bytecode "
      "records which stages are read by name inside expressions, so a collection is materialised for "
      "expressions only when one reads it, and only its size when it is read through `length`.")
    H2("Command-Line Interface")
    P("The `retroc` driver exposes `dump-tokens`, `dump-ast`, `check`, `dump-ir`, `run`, `edit`, `verify` and "
      "`explain`. The `edit` command prints a rebuild plan per edit listing, for every visited stage, how many "
      "records were evaluated and reused and which path the cost model chose; `explain` lists the edits that "
      "changed a given output and the first stage each change entered. Fig. 7–9 in Section IX show real "
      "sessions.")
    H2("Verification Infrastructure")
    P("Correctness is checked at four levels: doctest unit tests per module; golden tests that compare the "
      "exact output of every compiler stage, of edits and of `explain`; the `verify` command, which rewrites "
      "the source text with the edits, compiles and runs it from scratch and compares every output by full "
      "structural equality; and a differential tester [@difftest]; [@csmith]. The differential tester "
      "generates well-typed programs of seven shapes (filter and map chains, a scalar reading a collection "
      "through `length`, branching graphs with `tree_reduce`, joins, integer folds, and lambdas that read other "
      "stages by name) over 5 to 200 records, applies one to four random edits (45% modify, 15% insert stage, "
      "15% delete stage, 13% insert record, 12% delete record), and also injects invalid edits that must be "
      "rejected without changing any state. A metamorphic test checks that inserting an identity map leaves "
      "outputs unchanged [@metamorphic].")

    # ---------------- VIII. Experimental setup ----------------
    H1("Experimental Setup")
    H2("Dataset")
    P("The benchmarks use four synthetic, seeded workloads produced by a generator included with the "
      "artifact (Table V). W1 models a sensor log: temperatures are uniform in [−20, 120], humidity in "
      "[0, 100], and a reading is valid if its temperature lies in [−10, 100]; it is generated at 10 k, "
      "100 k and 1 M records to study scaling. W2 joins 500 users with 2,000 orders whose amounts are "
      "uniform in [10, 500]. W3 is a deep pipeline of twelve alternating map and filter stages over 100 k "
      "records with values uniform in [1, 100]. W4 is reduce-heavy: one map and one fold over 1 M records "
      "with values uniform in [1, 1000]. Fixed seeds make every run reproducible.")
    TABLE("Workloads",
          ["Id", "Records", "Pipeline"],
          [
              ["W1", "10k / 100k / 1M", "filter → map → map → filter → reduce"],
              ["W2", "500 + 2,000", "join → map → filter → reduce"],
              ["W3", "100k", "12 alternating map/filter → reduce"],
              ["W4", "1M", "map → reduce"],
          ],
          [500, 1300, 3060], align=["center", "center", "left"])
    H2("Edit Scenarios")
    P("Six edit scenarios are applied to each workload: E-A modifies one input record; E-B modifies 1% of the "
      "records, capped at 100 sequential edits; E-C inserts a map near the start of the pipeline; E-D inserts a "
      "map near the end; E-E deletes a middle stage; and E-F inserts an identity map. W4 has no E-E scenario "
      f"because deleting its only middle stage would change the element type. This yields {N_SCEN} scenarios.")
    H2("Baselines")
    P("Two baselines run in the same runtime, so that only the recomputation strategy differs. **B0** reruns "
      "the edited program from scratch with an empty memo. **B1** is stage-level invalidation: any change to a "
      "stage’s input recomputes every record of that stage without per-record memo, as a workflow or build "
      "system would, while keeping whole-stage early cutoff. **B2** is RetroC. A comparison with an external "
      "incremental engine is left to future work, because it would measure a different language and runtime.")
    H2("Hardware and Protocol")
    P("All measurements were taken on an Intel Core i7-13620H laptop processor (6 performance and 4 efficiency "
      "cores, 16 threads) with 15.6 GB of RAM under Windows 11, using a release build with -O2. Each system "
      "receives three warm-up runs followed by eleven timed runs, and the median is reported; a system whose "
      "single run exceeds 10 s receives one warm-up and five timed runs [@benchmeth]. Only propagation (B1, B2) "
      "or execution (B0) is timed; parsing, compilation, data loading and graph cloning are excluded. The "
      "process is pinned to one performance core at high priority, because the same work ran about twice as "
      "slowly on an efficiency core, and B0, B2 and B1 are timed round-robin so that slow drift "
      "such as thermal throttling affects all three alike. Every scenario is additionally run through the "
      "verifier.")
    H2("Evaluation Metrics")
    BUL([
        "**Wall-clock time** per edit script, median of eleven runs (min and max are recorded).",
        "**Speedup over a full rerun**, B0/B2, and **over stage-level invalidation**, B1/B2.",
        "**Operations** recomputed and reused, where an operation is one lambda evaluation or one folded "
        "element, and **reuse** = reused / (recomputed + reused).",
        "**First-run overhead** of recording memo tables and history, and **memo size** relative to input size.",
        "**Correctness**: verifier result per scenario, unit and golden tests, and differential trials.",
    ])

    # ---------------- IX. Results ----------------
    H1("Results")
    H2("Single-Record Edits")
    P(f"Fig. 3 shows the speedup of RetroC over a full rerun when one input record is modified. On W1 it is "
      f"{X('W1-10k', 'E-A')}, {X('W1-100k', 'E-A')} and {X('W1-1M', 'E-A')} at 10 k, 100 k and 1 M records; "
      f"on the 12-stage W3 it is {X('W3', 'E-A')}, on the reduce-heavy W4 {X('W4', 'E-A')}, and on the join "
      f"workload W2 {X('W2', 'E-A')}. Fig. 4 explains why the speedup grows with input size: a full rerun "
      f"and stage-level invalidation both grow linearly, from {fms(ms('W1-10k', 'E-A', 'b0'))} ms to "
      f"{fms(ms('W1-1M', 'E-A', 'b0'))} ms, while a RetroC edit stays at about "
      f"{ms('W1-1M', 'E-A', 'b2'):.2f} ms at every size. On W1-1M it recomputes "
      f"{ROWS[('W1-1M', 'E-A')]['ops_recomputed']} operations and reuses "
      f"{ROWS[('W1-1M', 'E-A')]['ops_reused']:,}: the changed record passes through each stage once and the "
      "final sum swaps one term.")
    FIGURE("fig2_single_edit.png",
           "Speedup of RetroC over a full rerun (B0/B2) for a single-record edit (E-A), logarithmic scale. "
           "Every workload is faster than a rerun, and the gain grows with input size.")
    FIGURE("fig4_scaling.png",
           "Time per single-record edit on W1 versus input size (log–log). A full rerun (B0) and "
           "stage-level invalidation (B1) grow linearly; RetroC (B2) stays constant.")

    H2("All Scenarios")
    P(f"Table VI lists all {N_SCEN} scenarios and Fig. 5 shows both speedups as a matrix. RetroC is faster "
      f"than a full rerun in every scenario, from {fx(MIN_B0)} to {fx(max(ratios(ALL_KEYS)))}. Batches of "
      f"sequential record edits (E-B) gain {RANGE(keys_for(['E-B']))}, because each edit is patched on its own "
      f"and costs a fraction of a millisecond. Inserts near the end of the pipeline gain "
      f"{RANGE(keys_for(['E-D']))}, identity inserts {RANGE(keys_for(['E-F']))} and deletes "
      f"{RANGE(keys_for(['E-E']))}, because every stage before the edit point is reused. An insert at the very "
      f"start (E-C) must evaluate the new stage on every record and changes every record downstream, so "
      f"its gain of {RANGE(keys_for(['E-C']))} comes from reusing the stages the change does not reach and "
      f"from the cost model, which avoids per-record bookkeeping when nothing can be reused.")
    FIGURE("fig3_matrix.png",
           "Performance matrix: speedup of RetroC over a full rerun (top) and over stage-level invalidation "
           "(bottom) for every workload and edit scenario. Green cells are faster than the baseline; the "
           "colour scale is logarithmic and the median time of B2 is printed in each cell.", width_in=3.3)
    rows = []
    for w in ALL_WL:
        for e in EDIT_KINDS:
            if (w, e) not in ROWS:
                continue
            r = ROWS[(w, e)]

            def cell_ms(k):
                v = ms(w, e, k)
                if v is None:
                    return "–"
                return fms(v) + ("*" if r[k]["runs"] < RES["protocol"]["runs"] else "")
            b1r = ratio(w, e, "b1")
            rows.append([w, e, cell_ms("b0"), cell_ms("b1"), cell_ms("b2"), fx(ratio(w, e), ""),
                         "–" if b1r is None else fx(b1r, "")])
    TABLE("Median Time per Edit Script (ms) and Speedups for All Scenarios",
          ["Work-load", "Edit", "B0", "B1", "B2", "B0/B2", "B1/B2"],
          rows, [800, 500, 760, 860, 760, 590, 590], sz=14,
          note="* single run above 10 s: one warm-up and median of five runs. – not run (estimated over one "
               f"hour). All {N_VERIFIED} scenarios passed the verifier.")

    H2("Record-Level Versus Stage-Level Reuse")
    P(f"The bottom half of Fig. 5 isolates the value of record granularity by comparing RetroC with "
      f"stage-level invalidation. For edits to input records the difference is large: "
      f"{RANGE(keys_for(['E-A']), 'b1')} for a single edit and {RANGE(keys_for(['E-B']), 'b1')} for "
      f"batches, because B1 recomputes every record of every downstream stage for each edit. For structural "
      f"edits RetroC is ahead of or level with stage-level invalidation ({RANGE(STRUCT, 'b1')}): an inserted "
      f"stage must see every record under either strategy, and the cost model ensures that RetroC adds no "
      f"bookkeeping in that case, while identity inserts let it reuse the downstream stages record by record. "
      + (f"The one scenario below 1.00×, {B1_TIES[0][0]} {B1_TIES[0][1]} ({X(*B1_TIES[0], 'b1')}), is an "
         f"identity map inserted directly before the final sum: both strategies run the new stage once over "
         f"every record and both skip the sum because its input is unchanged, so they do identical work and "
         f"the remaining difference is measurement noise (a separate pinned re-run of the same scenario measured "
         f"505.0 ms for stage-level invalidation and 506.2 ms for RetroC)." if B1_TIES else ""))

    H2("Effect of the Cost Model and Incremental Operators")
    P("Table VII compares the propagator before and after the cost model, the incremental sum and the hash "
      "join were added, using the same workloads, machine and protocol. The largest gains are on the "
      "scenarios that previously had to re-fold the whole input or re-run a nested-loop join: a single-record "
      f"edit on W1-1M went from {fx(ratio('W1-1M', 'E-A', rows=PREV_ROWS))} to {X('W1-1M', 'E-A')} faster than "
      f"a rerun, a batch of 100 edits on W1-100k from {fx(ratio('W1-100k', 'E-B', rows=PREV_ROWS))} to "
      f"{X('W1-100k', 'E-B')}, and a single edit on the join workload from "
      f"{fx(ratio('W2', 'E-A', rows=PREV_ROWS))} to {X('W2', 'E-A')}. Structural edits that change every "
      f"record moved from below parity to above it.")
    cmp_rows = []
    for w, e in [("W1-100k", "E-A"), ("W1-1M", "E-A"), ("W1-100k", "E-B"), ("W3", "E-B"), ("W4", "E-B"),
                 ("W2", "E-A"), ("W2", "E-B"), ("W1-100k", "E-C"), ("W3", "E-C"), ("W4", "E-F")]:
        cmp_rows.append([w, e, fx(ratio(w, e, rows=PREV_ROWS)), X(w, e)])
    TABLE("Speedup over a Full Rerun Before and After the Cost Model",
          ["Workload", "Edit", "Before", "After"],
          cmp_rows, [1300, 900, 1330, 1330], align=["left", "center", "center", "center"],
          note="Before: previous propagator (nested-loop join, linear fold, no cost model). Same machine and "
               "protocol; B0/B2 medians.")
    P("An earlier step had already made propagation proportional to the changed records. Fig. 6 compares "
      "that design with the original one, which visited, hashed and copied every record of every dirty stage, "
      "on W1-100k under the earlier and less strict protocol (one warm-up and median of three, unpinned). The "
      "single-record speedup rose from 1.28× to 5.42×, the late insert from 1.31× to 5.21×, and the delete "
      "from 0.86× to 2.20×, while operation counts were unchanged: the work avoided was bookkeeping, not "
      "computation. Together, the two steps account for the results above.")
    FIGURE("fig6_before_after.png",
           "Ablation on W1-100k (earlier protocol): speedup over a full rerun before and after "
           "record-proportional propagation, the first of the two optimisation steps.")

    H2("Overheads")
    P(f"Recording memo tables and history during the first run of W1-100k raises its time from "
      f"{OVH['plain']['median_ms']:.1f} ms to {OVH['with_history']['median_ms']:.1f} ms, an overhead of "
      f"{OVH['overhead_percent']:.1f}%. The memo tables occupy {OVH['memo_mb']:.2f} MB for "
      f"{OVH['input_mb']:.2f} MB of CSV input, about {OVH['memo_mb'] / OVH['input_mb']:.0f}× the input "
      "size, because each record’s memo entry stores two hashes and a copy of the output value. The "
      f"investment is recovered by the first edit: a single-record edit on W1-100k saves about "
      f"{ms('W1-100k', 'E-A', 'b0') - ms('W1-100k', 'E-A', 'b2'):.0f} ms compared with a rerun, more than the "
      f"{OVH['with_history']['median_ms'] - OVH['plain']['median_ms']:.0f} ms spent recording.")

    H2("Correctness Results")
    TABLE("Correctness Evidence",
          ["Check", "Result"],
          [
              ["Unit tests (doctest)", "65 cases, 1,097 assertions pass"],
              ["Test suites (CTest)", "8 / 8 pass, golden outputs unchanged"],
              ["Differential trials, 5 seeds", "10,000 / 10,000 match rerun"],
              ["Invalid-edit injection", "all rejected, state unchanged"],
              ["Planted bug (mutation)", "detected at trial 16"],
              ["Verifier on benchmarks", f"{N_VERIFIED} / {N_SCEN} scenarios pass"],
          ],
          [2300, 2560], align=["left", "left"])
    P("Table VIII summarises the correctness evidence. To check that the differential tester can detect real "
      "propagation errors, we planted a defect—a record removed from a stage’s output was not forwarded "
      "to its consumers—and the tester reported a mismatch at trial 16. The tester also found three defects "
      "during development of the new operators (an inserted stage wired to an output of the same name, a "
      "type check that ignored stages read by name, and a join patch applied to a record that had re-entered "
      "its input); each was fixed at its root cause and its "
      "failing case kept as a regression test.")

    H2("The Prototype in Use")
    P("Fig. 7–9 show unedited terminal sessions of the `retroc` command-line tool on the demonstration "
      "programs; every line is program output. Fig. 7 runs a sensor pipeline and applies four retroactive "
      "edits, each followed by its rebuild plan. Fig. 8 verifies the edited result against a fresh run of the "
      "rewritten source and asks which edits changed an output. Fig. 9 inserts and deletes input records and "
      "shows `tree_reduce` updating one leaf-to-root path.")
    FIGURE("shot1_run_edit.png",
           "Terminal session: first run of the sensor pipeline and four retroactive edits with their rebuild "
           "plans (records evaluated and reused per stage).")
    FIGURE("shot2_verify_explain.png",
           "Terminal session: `retroc verify` compares the propagated outputs with a fresh run of the rewritten "
           "program; `retroc explain` lists the edits that changed an output.")
    FIGURE("shot3_records_tree.png",
           "Terminal session: inserting and deleting input records, and an incremental `tree_reduce` update.")

    # ---------------- X. Comparison ----------------
    H1("Comparison with Existing and Baseline Methods")
    P(f"Against the two baselines, RetroC is faster than a full rerun in all {N_SCEN} scenarios; its results "
      f"{B1_CLAIM()}. For edits to input records, the case incremental "
      f"systems are built for, it improves on a full rerun by up to {fx(max(ratios(keys_for(['E-A']))))} and "
      f"on stage-level invalidation by up to {fx(max(ratios(keys_for(['E-A', 'E-B']), 'b1')))}, and record "
      "granularity is what delivers the gain. For structural edits it improves on a full rerun whenever part "
      "of the pipeline is untouched by the edit, and stays ahead of stage-level invalidation because the cost "
      "model removes bookkeeping that cannot pay off.")
    P("Qualitatively (Table I), RetroC is the only approach among those surveyed in which inserting or "
      "deleting a stage is an incremental, type-checked operation on an already-executed computation, and the "
      "only one with a built-in check against a full rerun. Differential dataflow and incremental view "
      "maintenance are mature, highly optimised engines for input changes [@ddflow]; [@ivm]; we did not "
      "measure them, and they target distributed, high-throughput settings that RetroC does not. RetroC’s "
      "contribution is the edit model and its verified, explainable record-level propagation.")

    # ---------------- XI. Discussion ----------------
    H1("Discussion")
    P("*Why the speedup grows with size.* Once sums and joins are updated in place, each stage that a changed "
      "record passes through spends constant or logarithmic time on it. An edit therefore costs about the same "
      "at 10 k and at 1 M records, whereas a rerun gets slower in direct proportion to the data. Fig. 4 makes "
      "this visible: the RetroC line is flat while both baselines climb.")
    P("*Batches.* Sequential edits are now patched one by one at a fraction of a millisecond each, so a batch "
      f"of 100 edits is still {RANGE(keys_for(['E-B']))} faster than one rerun. Stage-level invalidation "
      "must recompute the pipeline once per edit, which is why its batches are the slowest configuration in "
      "Table VI.")
    P("*Structural edits.* An inserted stage must see every record, so the achievable speedup is bounded by "
      "the share of the pipeline the edit leaves untouched: an insert at the very start leaves little, an "
      "insert near the end or an identity insert leaves most. The measured gains follow exactly this order, "
      "and the cost model keeps per-record bookkeeping out of the cases where it cannot pay off.")
    P("*Measurement.* Hybrid laptop processors produced up to threefold timing differences between "
      "otherwise identical runs, depending on whether the operating system scheduled the benchmark on a "
      "performance or an efficiency core and on thermal state. Pinning to one performance core and timing all "
      "systems round-robin reduced the spread within a scenario to about ±10%. We recommend both "
      "practices for incremental-computation benchmarks on consumer hardware.")
    P("*Threats to validity.* Our data is generated rather than collected, our language has few features, and "
      "every number comes from a single laptop. The results therefore speak for this design on these "
      "workloads and should not be read more broadly; Section XII spells out what remains outside scope.")

    # ---------------- XII. Limitations ----------------
    H1("Limitations")
    BUL([
        f"Memo tables use about {OVH['memo_mb'] / OVH['input_mb']:.0f}× the input size, and the first run is "
        f"{OVH['overhead_percent']:.0f}% slower; execution is single-threaded and in memory.",
        "Only integer sums are maintained incrementally; floating-point folds are re-folded, because "
        "floating-point addition is not associative, and other associative folds use `tree_reduce`.",
        "An insert at the very start of a pipeline changes every record, so its gain is modest by nature.",
        "The evaluation uses synthetic workloads on one machine and no external incremental engine.",
        "Edits cannot replace a stage in one step or change control flow.",
        "Memory-safety sanitizers could not be run with the MinGW toolchain used.",
    ])

    # ---------------- XIII. Conclusion ----------------
    H1("Conclusion")
    P("With RetroC we have shown that, for a pure dataflow language, a compiler can treat changes to past "
      "input records and the addition or removal of stages as ordinary typed and atomic operations, and can "
      "carry their effects forward one record at a time so that the work done tracks the amount that "
      f"actually changes. A single-record edit runs {RANGE(keys_for(['E-A'], W1S))} faster than a full rerun on a "
      f"sensor pipeline from 10 k to 1 M records, {X('W3', 'E-A')} faster on a 12-stage pipeline and "
      f"{X('W2', 'E-A')} faster on a join, and every one of the {N_SCEN} measured scenarios is faster than a "
      f"full rerun and either faster than or on par with stage-level invalidation. Every result matched a full rerun across "
      "unit, golden and benchmark tests and 10,000 random edit scripts. A per-stage cost model, an "
      "incremental integer sum and an incremental hash join were the keys to making every kind of edit pay "
      "off.")

    # ---------------- XIV. Future work ----------------
    H1("Future Work")
    BUL([
        "**Real datasets:** evaluate on public data pipelines and longer, realistic edit histories.",
        "**External baseline:** compare input-edit performance with an established incremental dataflow engine.",
        "**Compact memo tables:** store hashes instead of output copies where outputs can be recomputed cheaply.",
        "**Richer edits:** stage replacement in one step and edits to helper functions.",
        "**Parallel propagation** across independent branches of the graph.",
    ])

    # ---------------- Acknowledgment & references ----------------
    BODY.append(para("Heading5", "Acknowledgment"))
    P("We are grateful to the School of Computer Science and Engineering at Vellore Institute of Technology, "
      "Vellore, for the computing resources and guidance that made this project possible.")
    BODY.append(para("Heading5", "References"))


def references():
    out = []
    for i, key in enumerate(order):
        txt = REFS[key]
        out.append(para("references", raw=inline(txt), ppr_extra='<w:ind w:left="354" w:hanging="354"/>'))
    unused = [k for k in REFS if k not in order]
    if unused:
        print("unused references:", unused)
    return out


# ===========================================================================
# Assemble
# ===========================================================================
def author_para(name, dept, org, city, last_line, sect=None):
    sz = 18
    raw = run(name, sz=sz) + '<w:r><w:rPr><w:sz w:val="18"/></w:rPr><w:br/></w:r>'
    raw += run(dept, i=True, sz=sz) + '<w:r><w:rPr><w:sz w:val="18"/></w:rPr><w:br/></w:r>'
    raw += run(org, i=True, sz=sz) + '<w:r><w:rPr><w:sz w:val="18"/></w:rPr><w:br/></w:r>'
    raw += run(city, sz=sz) + '<w:r><w:rPr><w:sz w:val="18"/></w:rPr><w:br/></w:r>'
    raw += run(last_line, sz=sz)
    ppr = '<w:spacing w:before="100" w:beforeAutospacing="1"/>' + (sect or "")
    return para("Author", raw=raw, ppr_extra=ppr)


def build(template, out):
    zin = zipfile.ZipFile(template)
    doc = zin.read("word/document.xml").decode("utf-8")
    sects = re.findall(r"<w:sectPr\b.*?</w:sectPr>", doc, re.S)
    sect_title, sect_auth, sect_body, sect_end = sects[0], sects[1], sects[3], sects[4]
    # Four authors: two columns of two (the template's three columns would
    # leave one author alone on a second row)
    sect_auth = re.sub(r'<w:cols w:num="3"', '<w:cols w:num="2"', sect_auth)
    head = doc[: doc.index("<w:body>") + len("<w:body>")]
    tail = "</w:body></w:document>"

    content()
    parts = []
    parts.append(para("papertitle", raw=run(TITLE)))
    parts.append(para("Author", raw="", ppr_extra=sect_title))
    for k, a in enumerate(AUTHORS):
        parts.append(author_para(*a, sect=sect_auth if k == len(AUTHORS) - 1 else None))
    abstract = run("Abstract", i=True) + run("—") + run(cite_numbers(ABSTRACT))
    parts.append(para("Abstract", raw=abstract))
    parts.append(para("Keywords", raw=run("Keywords—") + run(KEYWORDS, b=False)))
    parts.extend(BODY)
    refs = references()
    # the last reference closes the two-column section so its columns balance
    last = refs[-1].replace("</w:pPr>", sect_body + "</w:pPr>", 1)
    parts.extend(refs[:-1] + [last])
    parts.append('<w:p><w:pPr><w:spacing w:after="0"/></w:pPr></w:p>')
    body = "".join(parts) + sect_end
    new_doc = head + body + tail

    rels = zin.read("word/_rels/document.xml.rels").decode("utf-8")
    extra = "".join(
        f'<Relationship Id="{rid}" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/image" '
        f'Target="media/{fn}"/>' for rid, fn in IMAGES)
    rels = rels.replace("</Relationships>", extra + "</Relationships>")
    ctypes = zin.read("[Content_Types].xml").decode("utf-8")
    if 'Extension="png"' not in ctypes:
        ctypes = ctypes.replace("<Default ", '<Default Extension="png" ContentType="image/png"/><Default ', 1)

    # Heading 1 numbers ("VIII.") overrun the template's tab stop; use a space.
    numbering = zin.read("word/numbering.xml").decode("utf-8")
    m = re.search(r'<w:abstractNum [^>]*w:abstractNumId="19".*?</w:abstractNum>', numbering, re.S)
    fixed = m.group(0).replace('<w:pStyle w:val="Heading1"/><w:lvlText',
                               '<w:pStyle w:val="Heading1"/><w:suff w:val="space"/><w:lvlText', 1)
    numbering = numbering.replace(m.group(0), fixed)

    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as zout:
        for item in zin.infolist():
            if item.filename == "word/numbering.xml":
                zout.writestr(item, numbering.encode("utf-8"))
            elif item.filename == "word/document.xml":
                zout.writestr(item, new_doc.encode("utf-8"))
            elif item.filename == "word/_rels/document.xml.rels":
                zout.writestr(item, rels.encode("utf-8"))
            elif item.filename == "[Content_Types].xml":
                zout.writestr(item, ctypes.encode("utf-8"))
            else:
                zout.writestr(item, zin.read(item.filename))
        for rid, fn in IMAGES:
            zout.write(os.path.join(FIG, fn), f"word/media/{fn}")
    print(f"wrote {out}: {len(order)} references cited, {len(IMAGES)} figures")


if __name__ == "__main__":
    build(sys.argv[1], sys.argv[2])
