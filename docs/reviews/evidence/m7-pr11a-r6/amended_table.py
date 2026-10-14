"""The 13.x runs after Joe's 2026-10-13 decision on the R6 finding (M7 PR 11a,
docs/plans/m7-pr11a-control-peer.md, "Joe's decisions after the measurement", items 1 to 5).

Read-only: it parses the three committed harness logs beside this file and writes amended_table.md.
- amended_decided_closed_loop.log: NescF16.Case13p{1-4}ClosedLoop with Q11 as amended (the trim's
  references) at divisor 10, gated per item 3 (13.1 and 13.2: all 21 channels; 13.3 and 13.4: the
  15 kinematic and atmosphere channels, the six aero channels reported);
- amended_variants.log: NescF16Variant.*, the DML's hard-coded references (Q11's reported variant
  now), D11 at divisor 1 (item 5), and Q27's three geometries;
- amended_controls.log: NescF16Control.Case13*, commit 6b's controls (item 6).
It also checks the swap against commit 6a's logs (decided_closed_loop.log, variants.log): 6a's
decided runs must equal this branch's DML-references runs, and 6a's SIM 5 runs this branch's
decided runs, in every channel's gated maximum, its sample and its first gated exceedance.

    python3 -I -B docs/reviews/evidence/m7-pr11a-r6/amended_table.py
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROW = re.compile(
    r"^(?:\d+: )?  (\w+)\s+tol_abs (\S+)\s+sample 0 (\S+)\s+ungated max (\S+)\s+at\s+(-?\d+)\s+"
    r"gated max (\S+)\s+at\s+(-?\d+)\s+first gated exceedance\s+(-?\d+)\s*(.*)$"
)
HEAD = re.compile(r"^(?:\d+: )?(case13p\d[^:]*): (\d+) samples compared")
AERO = {"force_x", "force_y", "force_z", "moment_l", "moment_m", "moment_n"}
INSIDE = re.compile(
    r"^(?:\d+: )?(case13p\d[^:]*): inside every band the decided run asserts \([^)]*\): (\w+)$",
    re.M,
)


def parse(path: Path) -> dict[str, list[dict]]:
    runs: dict[str, list[dict]] = {}
    current = None
    for line in path.read_text(encoding="utf-8").splitlines():
        head = HEAD.match(line)
        if head:
            current = head.group(1).replace(" (reported, no band asserted)", "")
            runs[current] = []
            continue
        row = ROW.match(line)
        if row and current is not None:
            runs[current].append(
                {
                    "name": row.group(1),
                    "gated_max": float(row.group(6)),
                    "at": int(row.group(7)),
                    "first": int(row.group(8)),
                    "zero": "zero-accepting" in row.group(9),
                    "reported": "reported, not asserted" in row.group(9),
                }
            )
    return runs


def fmt(rows: list[dict]) -> str:
    return ", ".join(
        f"{r['name']} {r['gated_max']:.3g} ({r['at']}; first {r['first']})"
        + (" zero-accepting" if r["zero"] else "")
        for r in rows
    )


def largest(rows: list[dict]) -> str:
    disc = [r for r in rows if not r["zero"]]
    top = max(disc, key=lambda r: r["gated_max"])
    return f"{top['name']} {top['gated_max']:.3g} ({top['at']})"


def main() -> int:
    decided = parse(HERE / "amended_decided_closed_loop.log")
    variants = parse(HERE / "amended_variants.log")
    old_decided = parse(HERE / "decided_closed_loop.log")
    old_variants = parse(HERE / "variants.log")
    controls_log = HERE / "amended_controls.log"
    controls = parse(controls_log)
    # Each reported run's "inside every band the decided run asserts" line (item 3's gating).
    inside = dict(
        INSIDE.findall((HERE / "amended_variants.log").read_text(encoding="utf-8"))
        + INSIDE.findall(controls_log.read_text(encoding="utf-8"))
    )
    out = [
        "The 13.x runs after Joe's 2026-10-13 decision (Q11 amended; R6 gated per item 3)",
        "",
        "Decided runs (divisor 10, the trim's references), every gated sample from 80 on:",
        "",
        "| Case | Asserted channels | Largest asserted ratio (sample) | Asserted channels above 1 "
        "| Reported channels above 1 (sample; first gated exceedance) |",
        "|---|---|---|---|---|",
    ]
    failures = 0
    for n in "1234":
        rows = decided[f"case13p{n}"]
        asserted = [r for r in rows if not r["reported"]]
        reported = [r for r in rows if r["reported"]]
        top = max(asserted, key=lambda r: r["gated_max"])
        above = [r for r in asserted if r["gated_max"] > 1.0]
        failures += len(above)
        rep_above = [r for r in reported if r["gated_max"] > 1.0]
        none_reported = "none" if reported else "(none reported)"
        out.append(
            f"| 13.{n} | {len(asserted)} | {top['name']} {top['gated_max']:.3g} ({top['at']}) | "
            f"{fmt(above) or 'none'} | {fmt(rep_above) or none_reported} |"
        )
    out += [
        "",
        "Each 13.3 and 13.4 aero channel's largest gated ratio (reported, never asserted; R6 not "
        "met):",
        "",
        "| Case | " + " | ".join(sorted(AERO)) + " |",
        "|---|" + "---|" * len(AERO),
    ]
    for n in "34":
        by = {r["name"]: r for r in decided[f"case13p{n}"]}
        out.append(
            f"| 13.{n} | "
            + " | ".join(
                f"{by[c]['gated_max']:.3g} ({by[c]['at']})" + (" z" if by[c]["zero"] else "")
                for c in sorted(AERO)
            )
            + " |"
        )
    out += [
        "",
        "(z: zero-accepting, never cited as evidence.)",
        "",
        "Reported variants, no band asserted, every channel at every gated sample:",
        "",
        "| Run | Largest discriminating ratio (sample) | Inside every band the decided run asserts "
        "| Channels above 1 (sample; first gated exceedance) |",
        "|---|---|---|---|",
    ]
    for run, rows in variants.items():
        above = [r for r in rows if r["gated_max"] > 1.0]
        out.append(f"| {run} | {largest(rows)} | {inside.get(run, '-')} | {fmt(above) or 'none'} |")

    out += [
        "",
        "The swap against commit 6a's logs (gated max, its sample, first gated exceedance):",
    ]
    for n in "1234":
        pairs = [
            (
                old_decided[f"case13p{n}"],
                variants[f"case13p{n} DML references"],
                "6a decided",
                "DML references",
            ),
            (
                old_variants[f"case13p{n} SIM 5 references"],
                decided[f"case13p{n}"],
                "6a SIM 5",
                "amended decided",
            ),
        ]
        for a, b, la, lb in pairs:
            differ = [
                x["name"]
                for x, y in zip(a, b, strict=True)
                if (x["gated_max"], x["at"], x["first"]) != (y["gated_max"], y["at"], y["first"])
            ]
            failures += len(differ)
            out.append(
                f"- 13.{n}: {la} against {lb}: "
                + (f"{len(a)} of {len(a)} channels equal" if not differ else f"DIFFER: {differ}")
            )
    out += [
        "",
        "Commit 6b's controls (item 6), from amended_controls.log:",
        "",
        "| Control | Asserted | The named channel's ratio (first gated exceedance) | Inside "
        "every band the decided run asserts | Channels above 1 (sample; first gated exceedance) |",
        "|---|---|---|---|---|",
    ]
    for run, rows in controls.items():
        by = {r["name"]: r for r in rows}
        named = "pitch" if "elevator" in run else "roll" if "aileron" in run else None
        above = [r for r in rows if r["gated_max"] > 1.0]
        if named is not None:
            hit = by[named]
            failures += 0 if hit["first"] >= 80 else 1
            cell = f"{named} {hit['gated_max']:.3g} ({hit['first']})"
        else:
            cell = "-"
        out.append(
            f"| {run} | {named or 'reported'} | {cell} | {inside.get(run, '-')} | "
            f"{fmt(above) or 'none'} |"
        )
    out.append("")
    out.append("amended table: " + ("OK" if failures == 0 else f"{failures} finding(s)"))
    text = "\n".join(out) + "\n"
    (HERE / "amended_table.md").write_text(text, encoding="utf-8")
    sys.stdout.write(text)
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
