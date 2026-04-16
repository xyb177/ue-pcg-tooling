#!/usr/bin/env python3
import argparse
import json
import math
import os
import statistics
from datetime import datetime
from pathlib import Path


def read_json(path: Path):
    return json.loads(path.read_text(encoding="utf-8-sig", errors="replace"))


def percentile(values, p):
    if not values:
        return 0.0
    s = sorted(values)
    if len(s) == 1:
        return float(s[0])
    pos = (len(s) - 1) * p
    lo = int(math.floor(pos))
    hi = int(math.ceil(pos))
    if lo == hi:
        return float(s[lo])
    frac = pos - lo
    return float(s[lo] * (1.0 - frac) + s[hi] * frac)


def safe_cv(values):
    if not values:
        return 0.0
    mean_v = statistics.fmean(values)
    if abs(mean_v) < 1e-9:
        return 0.0
    if len(values) == 1:
        return 0.0
    return float(statistics.pstdev(values) / mean_v)


def build_run_rows(run_paths, project_root):
    rows = []
    for idx, p in enumerate(run_paths):
        data = read_json(p)
        thread = data.get("thread_load", {}) or {}
        pe = ((thread.get("parallel_efficiency") or {}).get("score")) or 0.0
        top_p95 = float(data.get("top_node_p95_ms", 0.0) or 0.0)
        dur = float(data.get("run_duration_ms", 0.0) or 0.0)
        node_count = int(data.get("node_count", 0) or 0)
        run_name = data.get("run_name", "") or p.stem
        json_stem = p.stem
        dash = project_root / "Saved" / "Profiling" / "PCG" / "Visualization" / json_stem / "dashboard.html"
        rows.append({
            "index": idx + 1,
            "run_name": run_name,
            "json_path": str(p),
            "json_stem": json_stem,
            "run_duration_ms": dur,
            "top_node_p95_ms": top_p95,
            "parallel_efficiency_score": float(pe),
            "node_count": node_count,
            "dashboard_path": str(dash),
        })
    return rows


def build_top_node_stability(run_paths):
    buckets = {}
    for p in run_paths:
        data = read_json(p)
        for n in data.get("nodes", []) or []:
            key = "{}|{}|{}".format(n.get("graph_name", ""), n.get("node_id", ""), n.get("node_name", ""))
            b = buckets.setdefault(key, {
                "graph_name": n.get("graph_name", ""),
                "node_id": n.get("node_id", ""),
                "node_name": n.get("node_name", ""),
                "total_ms_series": [],
                "p95_ms_series": [],
            })
            b["total_ms_series"].append(float(n.get("total_ms", 0.0) or 0.0))
            b["p95_ms_series"].append(float(n.get("p95_ms", 0.0) or 0.0))

    rows = []
    for v in buckets.values():
        total_series = v["total_ms_series"]
        p95_series = v["p95_ms_series"]
        mean_total = statistics.fmean(total_series) if total_series else 0.0
        mean_p95 = statistics.fmean(p95_series) if p95_series else 0.0
        rows.append({
            "graph_name": v["graph_name"],
            "node_id": v["node_id"],
            "node_name": v["node_name"],
            "mean_total_ms": float(mean_total),
            "p95_total_ms": percentile(total_series, 0.95),
            "cv_total_ms": safe_cv(total_series),
            "mean_p95_ms": float(mean_p95),
            "p95_of_p95_ms": percentile(p95_series, 0.95),
            "cv_p95_ms": safe_cv(p95_series),
            "samples": len(total_series),
        })

    rows.sort(key=lambda x: (x["mean_total_ms"], x["p95_total_ms"]), reverse=True)
    return rows[:50]


def build_regression(run_rows):
    if len(run_rows) < 2:
        return {"status": "INFO", "items": ["Need at least 2 runs for regression check."]}
    base = run_rows[0]
    latest = run_rows[-1]

    def delta_pct(cur, b):
        return ((cur - b) / b * 100.0) if abs(b) > 1e-9 else 0.0

    dur_delta = delta_pct(latest["run_duration_ms"], base["run_duration_ms"])
    p95_delta = delta_pct(latest["top_node_p95_ms"], base["top_node_p95_ms"])
    pe_delta = latest["parallel_efficiency_score"] - base["parallel_efficiency_score"]

    items = []
    status = "PASS"
    if dur_delta > 10.0:
        status = "FAIL"
        items.append("run_duration regression: +{:.2f}% (>10%)".format(dur_delta))
    if p95_delta > 15.0:
        status = "FAIL"
        items.append("top_node_p95 regression: +{:.2f}% (>15%)".format(p95_delta))
    if pe_delta < -8.0:
        status = "FAIL"
        items.append("parallel_efficiency drop: {:.2f} (< -8)".format(pe_delta))
    if not items:
        items.append("No regression hit by current thresholds.")
    return {
        "status": status,
        "items": items,
        "metrics": {
            "run_duration_delta_pct": dur_delta,
            "top_node_p95_delta_pct": p95_delta,
            "parallel_efficiency_delta": pe_delta,
        },
    }


def render_html(out_html: Path, payload):
    data_json = json.dumps(payload, ensure_ascii=False)
    html = f"""<!doctype html>
<html><head><meta charset='utf-8'/><title>PCG Batch Summary</title>
<style>
body{{font-family:Segoe UI,Arial,sans-serif;background:#0f1320;color:#e9edf7;margin:0;padding:16px;}}
h1,h2{{margin:8px 0 12px;}} .small{{opacity:.75;font-size:12px;}}
.card{{background:#171d2f;border:1px solid #2b3350;border-radius:10px;padding:12px;margin:10px 0;}}
table{{width:100%;border-collapse:collapse;font-size:13px;}} th,td{{padding:6px 8px;border-bottom:1px solid #2a3150;text-align:left;}}
th{{color:#b7c2eb;font-weight:600;}} .ok{{color:#35d18a;}} .bad{{color:#ff6b6b;}}
.grid{{display:grid;grid-template-columns:repeat(4,minmax(120px,1fr));gap:10px;}} .kpi{{background:#1a2138;border-radius:8px;padding:10px;}}
a{{color:#7ec8ff;}}
</style></head><body>
<h1>PCG Batch Summary</h1>
<div class='small' id='meta'></div>
<div class='card'><div class='grid' id='kpis'></div></div>
<div class='card'><h2>Regression</h2><div id='reg'></div></div>
<div class='card'><h2>Per-Run Details</h2><table id='runs'><thead><tr><th>#</th><th>Run</th><th>Duration(ms)</th><th>TopNodeP95(ms)</th><th>ParallelEff</th><th>Nodes</th><th>Dashboard</th></tr></thead><tbody></tbody></table></div>
<div class='card'><h2>Top Node Stability (Top 50)</h2><table id='nodes'><thead><tr><th>NodeID</th><th>Node</th><th>Graph</th><th>MeanTotal</th><th>P95Total</th><th>CV(Total)</th><th>MeanP95</th><th>P95ofP95</th><th>CV(P95)</th><th>Samples</th></tr></thead><tbody></tbody></table></div>
<script>
const D={data_json};
function fmt(v,d=2){{return Number(v||0).toFixed(d);}}
function pct(v){{return (Number(v||0)*100).toFixed(2)+'%';}}
document.getElementById('meta').textContent='Generated: '+(D.generated_at||'')+' | Runs: '+(D.run_count||0);
const s=D.stats||{{}};
const k=[
 ['run_duration p50', fmt(s.run_duration_p50_ms)],
 ['run_duration p95', fmt(s.run_duration_p95_ms)],
 ['run_duration min/max', fmt(s.run_duration_min_ms)+' / '+fmt(s.run_duration_max_ms)],
 ['parallel_eff avg', fmt(s.parallel_efficiency_avg,1)]
];
document.getElementById('kpis').innerHTML=k.map(x=>`<div class='kpi'><div class='small'>${{x[0]}}</div><div><b>${{x[1]}}</b></div></div>`).join('');
const reg=D.regression||{{status:'INFO',items:[]}};
const cls=reg.status==='FAIL'?'bad':'ok';
document.getElementById('reg').innerHTML=`<div><b class='${{cls}}'>${{reg.status}}</b></div><ul>${{(reg.items||[]).map(i=>`<li>${{i}}</li>`).join('')}}</ul>`;
document.querySelector('#runs tbody').innerHTML=(D.runs||[]).map(r=>`<tr>
<td>${{r.index}}</td><td>${{r.run_name||''}}</td><td>${{fmt(r.run_duration_ms)}}</td>
<td>${{fmt(r.top_node_p95_ms)}}</td><td>${{fmt(r.parallel_efficiency_score,1)}}</td>
<td>${{r.node_count||0}}</td><td><a href='${{r.dashboard_rel||'#'}}'>open</a></td></tr>`).join('');
document.querySelector('#nodes tbody').innerHTML=(D.top_node_stability||[]).map(n=>`<tr>
<td>${{n.node_id||''}}</td><td>${{n.node_name||''}}</td><td>${{n.graph_name||''}}</td>
<td>${{fmt(n.mean_total_ms)}}</td><td>${{fmt(n.p95_total_ms)}}</td><td>${{pct(n.cv_total_ms)}}</td>
<td>${{fmt(n.mean_p95_ms)}}</td><td>${{fmt(n.p95_of_p95_ms)}}</td><td>${{pct(n.cv_p95_ms)}}</td><td>${{n.samples||0}}</td></tr>`).join('');
</script></body></html>"""
    out_html.write_text(html, encoding="utf-8")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--input-json", action="append", default=[])
    ap.add_argument("--output-dir", default="")
    ap.add_argument("--project-root", default="")
    args = ap.parse_args()

    run_paths = []
    for raw in args.input_json:
        p = Path(raw).expanduser().resolve()
        if p.exists() and p.suffix.lower() == ".json":
            run_paths.append(p)
    # preserve order and de-dup
    uniq = []
    seen = set()
    for p in run_paths:
        s = str(p)
        if s in seen:
            continue
        seen.add(s)
        uniq.append(p)
    run_paths = uniq
    if len(run_paths) < 2:
        raise SystemExit("Need at least two input JSON files.")

    project_root = Path(args.project_root).expanduser().resolve() if args.project_root else run_paths[0].parents[3]
    out_dir = Path(args.output_dir).expanduser().resolve() if args.output_dir else project_root / "Saved" / "Profiling" / "PCG" / "Visualization"
    out_dir.mkdir(parents=True, exist_ok=True)

    run_rows = build_run_rows(run_paths, project_root)
    for row in run_rows:
        dash_path = Path(row["dashboard_path"])
        if dash_path.exists():
            row["dashboard_rel"] = os.path.relpath(str(dash_path), str(out_dir)).replace("\\", "/")
        else:
            row["dashboard_rel"] = "#"

    durations = [r["run_duration_ms"] for r in run_rows]
    pe_scores = [r["parallel_efficiency_score"] for r in run_rows]
    stats = {
        "run_duration_p50_ms": percentile(durations, 0.5),
        "run_duration_p95_ms": percentile(durations, 0.95),
        "run_duration_min_ms": min(durations) if durations else 0.0,
        "run_duration_max_ms": max(durations) if durations else 0.0,
        "parallel_efficiency_avg": statistics.fmean(pe_scores) if pe_scores else 0.0,
    }

    payload = {
        "generated_at": datetime.now().isoformat(),
        "run_count": len(run_rows),
        "runs": run_rows,
        "stats": stats,
        "top_node_stability": build_top_node_stability(run_paths),
        "regression": build_regression(run_rows),
    }

    summary_json = out_dir / "batch_summary.json"
    summary_html = out_dir / "batch_summary.html"
    summary_json.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")
    render_html(summary_html, payload)
    print(f"BATCH_SUMMARY_JSON={summary_json}")
    print(f"BATCH_SUMMARY_HTML={summary_html}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
