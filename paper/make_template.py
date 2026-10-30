"""Rebuild a body-less Transitional template from a docx produced by build_paper.py.

Usage (repository root):
    python paper/make_template.py paper/RetroC_IEEE_paper_final.docx paper/figures <template.docx>
Use it when the Word-converted IEEE template is not at hand; the result reproduces
the committed paper byte for byte when fed the same build_paper.py.
Keeps every package part (styles, numbering, footer, settings) and the section
properties; drops the body text and the figure images and their relationships.
"""
import os
import re
import sys
import zipfile

src, fig_dir, out = sys.argv[1], sys.argv[2], sys.argv[3]
figs = set(os.listdir(fig_dir))
zin = zipfile.ZipFile(src)
doc = zin.read("word/document.xml").decode("utf-8")
sects = re.findall(r"<w:sectPr\b.*?</w:sectPr>", doc, re.S)
assert len(sects) == 4, len(sects)
title, auth, body, end = sects
head = doc[: doc.index("<w:body>") + len("<w:body>")]
paras = "".join(f"<w:p><w:pPr>{s}</w:pPr></w:p>" for s in (title, auth, body, body))
new_doc = head + paras + end + "</w:body></w:document>"

rels = zin.read("word/_rels/document.xml.rels").decode("utf-8")
rels = re.sub(r'<Relationship [^>]*Target="media/([^"]+)"/>',
              lambda m: "" if m.group(1) in figs else m.group(0), rels)

with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as zout:
    for item in zin.infolist():
        name = item.filename
        if name.startswith("word/media/") and os.path.basename(name) in figs:
            continue
        if name == "word/document.xml":
            zout.writestr(item, new_doc.encode("utf-8"))
        elif name == "word/_rels/document.xml.rels":
            zout.writestr(item, rels.encode("utf-8"))
        else:
            zout.writestr(item, zin.read(name))
print("wrote", out)
