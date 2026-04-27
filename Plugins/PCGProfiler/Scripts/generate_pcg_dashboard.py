#!/usr/bin/env python3
import argparse
import csv
import json
import subprocess
import sys
from datetime import datetime
from pathlib import Path


def read_json(path: Path):
    return json.loads(path.read_text(encoding='utf-8-sig', errors='replace'))


def find_latest_valid_json(pcg_dir: Path):
    files = sorted(pcg_dir.glob('PCGProfiler_*.json'), key=lambda p: p.stat().st_mtime, reverse=True)
    for f in files:
        try:
            return f, read_json(f)
        except Exception:
            continue
    return None, None


def find_candidate_project_roots(start: Path):
    roots = []
    cur = start.resolve()
    if cur.is_file():
        cur = cur.parent
    roots.append(cur)
    roots.extend(cur.parents)
    return roots


def resolve_pcg_dir(project_root_arg: str, input_json_arg: str):
    if project_root_arg:
        root = Path(project_root_arg).expanduser().resolve()
        return root / 'Saved' / 'Profiling' / 'PCG'

    if input_json_arg:
        p = Path(input_json_arg).expanduser().resolve()
        for base in [p.parent] + list(p.parent.parents):
            if base.name.lower() == 'pcg' and base.parent.name.lower() == 'profiling':
                return base
            candidate = base / 'Saved' / 'Profiling' / 'PCG'
            if candidate.exists():
                return candidate

    for root in find_candidate_project_roots(Path.cwd()):
        candidate = root / 'Saved' / 'Profiling' / 'PCG'
        if candidate.exists():
            return candidate

    script_dir = Path(__file__).resolve().parent
    for root in find_candidate_project_roots(script_dir):
        candidate = root / 'Saved' / 'Profiling' / 'PCG'
        if candidate.exists():
            return candidate

    return None


def get_nodes(data):
    return data.get('nodes', []) if isinstance(data, dict) else []


def get_events(data):
    return data.get('events', []) if isinstance(data, dict) else []


def severity_of(node):
    if (node.get('error_count') or 0) > 0:
        return 'Error'
    if (node.get('warning_count') or 0) > 0:
        return 'Warning'
    if (node.get('cancelled_count') or 0) > 0:
        return 'Cancelled'
    return 'Info'


def build_diagnosis(data, nodes):
    out = []
    thread = data.get('thread_load', {}) or {}
    cache = data.get('cache_analysis', {}) or {}
    scale = data.get('scale_summary', {}) or {}
    pe = float(((thread.get('parallel_efficiency') or {}).get('score')) or 0)
    hit = cache.get('graph_cache_hit_rate', None)
    ratio = scale.get('global_output_input_ratio', None)
    if pe < 60:
        out.append(f'Parallel efficiency is low ({pe:.1f}).')
    if hit is not None and hit < 0.4:
        out.append(f'Cache hit rate is low ({hit:.1%}).')
    if ratio is not None and ratio > 2.0:
        out.append(f'Point amplification is high (Out/In={ratio:.2f}).')
    top = sorted(nodes, key=lambda n: n.get('total_ms', 0) or 0, reverse=True)[:3]
    if top:
        out.append('Top costly nodes: ' + ', '.join([f"{n.get('node_name','?')}({(n.get('total_ms') or 0):.1f}ms)" for n in top]))
    return out or ['No obvious anomaly detected. Focus on cross-run variance.']


def compare_runs(current, baseline):
    result = {'available': bool(baseline), 'summary': {}, 'regressions': [], 'node_component_diff': {}}
    if not baseline:
        return result

    c_run = float(current.get('run_duration_ms', 0) or 0)
    b_run = float(baseline.get('run_duration_ms', 0) or 0)
    c_pe = float((((current.get('thread_load', {}) or {}).get('parallel_efficiency', {}) or {}).get('score') or 0))
    b_pe = float((((baseline.get('thread_load', {}) or {}).get('parallel_efficiency', {}) or {}).get('score') or 0))
    run_delta = ((c_run - b_run) / b_run * 100.0) if b_run else 0.0
    pe_delta = c_pe - b_pe
    result['summary'] = {'run_duration_delta_pct': run_delta, 'parallel_eff_delta': pe_delta}
    if run_delta > 10:
        result['regressions'].append(f'Run duration regressed by {run_delta:.2f}% (>10%)')
    if pe_delta < -8:
        result['regressions'].append(f'Parallel efficiency dropped by {abs(pe_delta):.2f} points (>8)')

    def agg(events):
        buckets = {}
        for e in events:
            k = f"{e.get('node_id','')}|{e.get('component_name','')}|{e.get('execution_path','')}"
            b = buckets.setdefault(k, {
                'node_id': e.get('node_id', ''),
                'node_name': e.get('node_name') or e.get('node_title') or '',
                'component_name': e.get('component_name', ''),
                'execution_path': e.get('execution_path', ''),
                'inclusive_total_ms': 0.0,
                'count': 0,
                'cache_hit_count': 0,
            })
            b['inclusive_total_ms'] += float(e.get('inclusive_ms', 0) or 0)
            b['count'] += 1
            b['cache_hit_count'] += 1 if e.get('cache_hit') else 0
        return buckets

    cur = agg(get_events(current))
    base = agg(get_events(baseline))
    for k in cur.keys() & base.keys():
        c = cur[k]
        b = base[k]
        bi = b['inclusive_total_ms']
        result['node_component_diff'][k] = {
            'node_id': c['node_id'],
            'node_name': c['node_name'],
            'component_name': c['component_name'],
            'execution_path': c['execution_path'],
            'current_inclusive_total_ms': c['inclusive_total_ms'],
            'baseline_inclusive_total_ms': bi,
            'delta_inclusive_ms': c['inclusive_total_ms'] - bi,
            'delta_inclusive_pct': ((c['inclusive_total_ms'] - bi) / bi * 100.0) if bi else 0.0,
            'current_cache_hit_rate': (c['cache_hit_count'] / c['count']) if c['count'] else 0.0,
            'baseline_cache_hit_rate': (b['cache_hit_count'] / b['count']) if b['count'] else 0.0,
        }
    return result


def build_event_drilldown(events):
    buckets = {}
    for e in events:
        phase = e.get('phase', 'Unknown') or 'Unknown'
        thread = e.get('thread_group', 'Unknown') or 'Unknown'
        cache = 'hit' if e.get('cache_hit') else 'miss'
        key = f'{phase}|{thread}|{cache}'
        b = buckets.setdefault(key, {'phase': phase, 'thread_group': thread, 'cache_state': cache, 'count': 0, 'inclusive_total_ms': 0.0, 'queue_wait_total_ms': 0.0})
        b['count'] += 1
        b['inclusive_total_ms'] += float(e.get('inclusive_ms', 0) or 0)
        b['queue_wait_total_ms'] += float(e.get('queue_wait_ms', 0) or 0)
    return sorted(buckets.values(), key=lambda x: x['inclusive_total_ms'], reverse=True)


def build_call_tree_model(events):
    by_path = {}
    for e in events:
        p = e.get('execution_path') or ''
        if not p:
            continue
        n = by_path.setdefault(p, {
            'execution_path': p,
            'parent_execution_path': e.get('parent_execution_path') or '',
            'node_name': e.get('node_name') or e.get('node_title') or '',
            'component_name': e.get('component_name', ''),
            'inclusive_ms': 0.0,
            'self_ms': 0.0,
            'count': 0,
            'children': [],
        })
        n['inclusive_ms'] += float(e.get('inclusive_ms', 0) or 0)
        n['self_ms'] += float(e.get('self_ms', 0) or 0)
        n['count'] += 1
        if not n['node_name']:
            n['node_name'] = e.get('node_name') or e.get('node_title') or ''
        if not n['component_name']:
            n['component_name'] = e.get('component_name', '')

    for path, node in by_path.items():
        parent = node.get('parent_execution_path') or ''
        if parent and parent in by_path and parent != path:
            by_path[parent]['children'].append(path)

    for node in by_path.values():
        node['children'].sort(key=lambda child: by_path[child]['inclusive_ms'], reverse=True)

    roots = []
    for path, node in by_path.items():
        parent = node.get('parent_execution_path') or ''
        if not parent or parent not in by_path or parent == path:
            roots.append(path)
    roots.sort(key=lambda r: by_path[r]['inclusive_ms'], reverse=True)

    flat = []
    flame_rects = []
    total = sum(by_path[r]['inclusive_ms'] for r in roots) or 1.0

    def walk(path, depth, x0, x1):
        node = by_path[path]
        flat.append({
            'execution_path': node['execution_path'],
            'parent_execution_path': node['parent_execution_path'],
            'node_name': node['node_name'],
            'component_name': node['component_name'],
            'inclusive_ms': node['inclusive_ms'],
            'self_ms': node['self_ms'],
            'count': node['count'],
            'depth': depth,
        })
        flame_rects.append({
            'execution_path': node['execution_path'],
            'node_name': node['node_name'],
            'inclusive_ms': node['inclusive_ms'],
            'self_ms': node['self_ms'],
            'depth': depth,
            'x0': x0,
            'x1': x1,
        })

        children = node['children']
        if not children:
            return
        child_total = sum(by_path[c]['inclusive_ms'] for c in children) or 1.0
        cursor = x0
        span = max(0.0, x1 - x0)
        for child in children:
            w = span * (by_path[child]['inclusive_ms'] / child_total)
            nx0 = cursor
            nx1 = min(x1, cursor + w)
            walk(child, depth + 1, nx0, nx1)
            cursor += w

    cursor = 0.0
    for root in roots:
        width = by_path[root]['inclusive_ms'] / total
        walk(root, 0, cursor, min(1.0, cursor + width))
        cursor += width

    flat.sort(key=lambda x: (-x['inclusive_ms'], x['depth']))
    return {
        'flat': flat,
        'flame_rects': flame_rects,
    }


def build_redundancy(data):
    edges = data.get('link_contribution_graph', []) or []
    tops = sorted(edges, key=lambda e: e.get('total_child_inclusive_ms', 0) or 0, reverse=True)[:120]
    parent_sum = {}
    for e in tops:
        p = e.get('parent_node_id') or ''
        parent_sum[p] = parent_sum.get(p, 0.0) + float(e.get('total_child_inclusive_ms', 0) or 0)
    rows = []
    for e in tops:
        p = e.get('parent_node_id') or ''
        c = float(e.get('total_child_inclusive_ms', 0) or 0)
        rows.append({'parent_node_title': e.get('parent_node_title', ''), 'child_node_title': e.get('child_node_title', ''), 'total_child_inclusive_ms': c, 'redundancy_ratio': (c / parent_sum[p]) if parent_sum.get(p, 0) else 0.0, 'call_count': int(e.get('call_count', 0) or 0)})
    return sorted(rows, key=lambda x: (x['redundancy_ratio'], x['total_child_inclusive_ms']), reverse=True)


def build_lifecycle_snapshot(data):
    life = (data.get('lifecycle_tracking') or {})
    mem = (data.get('memory_insights') or {})
    return {
        'created_points_total': float(life.get('created_points_total', 0) or 0),
        'reduced_points_total': float(life.get('reduced_points_total', 0) or 0),
        'net_points_change': float(life.get('net_points_change', 0) or 0),
        'top_creators': (life.get('top_creators') or [])[:20],
        'top_reducers': (life.get('top_reducers') or [])[:20],
        'memory_hotspots': (mem.get('memory_hotspots') or [])[:20],
        'redundant_intermediate_candidates': (mem.get('redundant_intermediate_candidates') or [])[:20],
        'redundant_candidate_rule': mem.get('redundant_candidate_rule', ''),
    }

def build_cache_heatmap(data):
    cache = data.get('cache_analysis', {}) or {}
    stats = cache.get('node_cache_stats', []) or []
    rows = []
    for n in stats:
        hit = float(n.get('hit_rate', n.get('cache_hit_rate', 0)) or 0)
        miss = int(n.get('miss_count', n.get('cache_miss_count', 0)) or 0)
        rows.append({
            'node_id': n.get('node_id', ''),
            'node_name': n.get('node_name') or n.get('node_title') or '',
            'component_name': n.get('component_name', ''),
            'hit_rate': max(0.0, min(1.0, hit)),
            'miss_count': miss,
        })
    rows.sort(key=lambda x: (x['hit_rate'], -x['miss_count']))
    return rows[:80]


def build_stability_view(data):
    stability = data.get('stability', {}) or {}
    cross = data.get('cross_run_node_stats', []) or []
    top_n = data.get('top_n', {}) or {}
    return {
        'run_duration_cv': float(stability.get('run_duration_cv', 0) or 0),
        'parallel_efficiency_cv': float(stability.get('parallel_efficiency_cv', 0) or 0),
        'top_node_p95_cv': float(stability.get('top_node_p95_cv', 0) or 0),
        'cross_run_node_stats': cross[:120],
        'top_n_by_total_ms': (top_n.get('by_total_ms') or [])[:30],
        'top_n_by_peak_ms': (top_n.get('by_peak_ms') or [])[:30],
    }


def write_csv(path: Path, rows, fieldnames):
    with path.open('w', encoding='utf-8-sig', newline='') as f:
        w = csv.DictWriter(f, fieldnames=fieldnames)
        w.writeheader()
        for r in rows:
            w.writerow({k: r.get(k, '') for k in fieldnames})

def render_dashboard(output_html: Path, payload):
    data_json = json.dumps(payload, ensure_ascii=False)
    html = f"""<!doctype html><html><head><meta charset='utf-8'/><title>PCG Profiler Dashboard</title>
<style>
body{{font-family:'Segoe UI','PingFang SC',sans-serif;margin:14px;background:#f4f6fb;color:#0f172a}}
.panel{{background:#fff;border:1px solid #dbe2ea;border-radius:10px;padding:12px;margin:10px 0}}
.grid{{display:grid;grid-template-columns:repeat(4,minmax(180px,1fr));gap:8px}}
.kpi{{font-size:22px;font-weight:700}} .small{{font-size:12px;color:#475467}}
.controls{{display:flex;gap:8px;flex-wrap:wrap}} input,select{{padding:6px 8px;border:1px solid #cbd5e1;border-radius:6px}}
.table-wrap{{overflow:auto;max-height:420px}} table{{border-collapse:collapse;width:100%;font-size:12px}}
th,td{{border:1px solid #e5e7eb;padding:6px;white-space:nowrap}} th{{position:sticky;top:0;background:#eef3f8}}
.row{{display:grid;grid-template-columns:1fr 1fr;gap:10px}} .bad{{color:#b42318;font-weight:700}} .good{{color:#067647;font-weight:700}}
svg{{width:100%;display:block}} #timeline{{height:250px;border:1px solid #dbe2ea;border-radius:8px;background:#fbfdff}} #flame{{height:320px;border:1px solid #dbe2ea;border-radius:8px;background:#fbfdff}} #gantt{{height:280px;border:1px solid #dbe2ea;border-radius:8px;background:#fbfdff}}
.code{{font-family:Consolas,monospace;font-size:11px;white-space:pre-wrap;word-break:break-all;background:#0f172a;color:#e2e8f0;padding:8px;border-radius:8px}}
</style></head><body>
<h1>PCG Profiler Dashboard (Phase 2)</h1><div id='app'></div>
<script>
const payload={data_json};
const run=payload.run||{{}}; const nodes=payload.nodes||[]; const events=payload.events||[]; const compare=payload.compare||{{}};
const fmt=(n,d=2)=>Number.isFinite(n)?n.toLocaleString(undefined,{{maximumFractionDigits:d}}):'-'; const pct=n=>Number.isFinite(n)?(n*100).toFixed(2)+'%':'-';
const esc=s=>String(s??'').replaceAll('&','&amp;').replaceAll('<','&lt;').replaceAll('>','&gt;').replaceAll('"','&quot;');
const pe=((run.thread_load||{{}}).parallel_efficiency||{{}}).score||0; const hit=((run.cache_analysis||{{}}).graph_cache_hit_rate)||0; const ratio=((run.scale_summary||{{}}).global_output_input_ratio)||0;
const missingEvents=(run.missing_required_event_count||0); const missingFields=(run.missing_required_field_total||0);
function kpi(t,v,c=''){{return `<div class='panel'><div class='small'>${{t}}</div><div class='kpi ${{c}}'>${{v}}</div></div>`;}}

const app=document.getElementById('app');
app.innerHTML=`<div class='grid'>${{kpi('Run Duration(ms)',fmt(run.run_duration_ms))}}${{kpi('Parallel Efficiency',fmt(pe),pe<60?'bad':'good')}}${{kpi('Cache Hit Rate',pct(hit),hit<0.4?'bad':'good')}}${{kpi('Output/Input',fmt(ratio),ratio>2?'bad':'')}}${{kpi('Node Count',fmt(run.node_count||nodes.length,0))}}${{kpi('Event Count',fmt(events.length,0))}}${{kpi('Map',esc(run.map_name||'-'))}}${{kpi('Run',esc(run.run_name||'-'))}}</div>
<div class='panel'><h3>Data Quality</h3><div class='grid'>${{kpi('Schema',esc(run.schema_version||'-'))}}${{kpi('Missing Events',fmt(missingEvents,0),missingEvents>0?'bad':'good')}}${{kpi('Missing Fields',fmt(missingFields,0),missingFields>0?'bad':'good')}}${{kpi('Memory Peak',(payload.node_memory_top||[]).length?esc((payload.node_memory_top[0]||{{}}).node_name||'')+' '+fmt((payload.node_memory_top[0]||{{}}).est_mem_mb||0)+'MB':'N/A')}}</div></div>
<div class='panel'><h3>Auto Diagnosis</h3><ul>${{(payload.diagnosis||[]).map(x=>`<li>${{esc(x)}}</li>`).join('')}}</ul></div>
<div class='row'><div class='panel'><h3>Stability (stability/cross_run_node_stats/top_n)</h3><div id='stabilityBox'></div><div class='table-wrap'><table id='crossTbl'><thead><tr><th>NodeID</th><th>P50</th><th>P95</th><th>CV</th><th>Samples</th></tr></thead><tbody></tbody></table></div></div><div class='panel'><h3>TopN Nodes (top_n)</h3><div class='small'>by_total_ms</div><div class='table-wrap'><table id='topTotalTbl'><thead><tr><th>NodeID</th><th>Graph</th><th>Total</th><th>Self</th><th>P95</th><th>S/T%</th></tr></thead><tbody></tbody></table></div><div class='small'>by_estimated_memory</div><div class='table-wrap'><table id='topMemTbl'><thead><tr><th>NodeID</th><th>Graph</th><th>InPts</th><th>OutPts</th><th>Est.Mem</th></tr></thead><tbody></tbody></table></div><div class='small'>by_peak_ms</div><div class='table-wrap'><table id='topPeakTbl'><thead><tr><th>NodeID</th><th>Max</th><th>Calls</th><th>Graph</th></tr></thead><tbody></tbody></table></div></div></div>
<div class='panel'><h3>Cache Hit Heatmap (cache_analysis.node_cache_stats)</h3><div id='cacheHeat'></div></div>
<div class='panel'><h3>Node Table</h3><div class='controls'><input id='qNode' placeholder='Search node/graph/component'/><select id='graphNode'><option value=''>All Graph</option></select><select id='sortNode'><option value='total_ms'>total_ms</option><option value='self_ms'>self_ms</option><option value='p95_ms'>p95_ms</option><option value='worker_thread_ratio'>worker%</option><option value='queue_wait_avg_ms'>queue_wait</option><option value='estimated_memory_bytes'>est_mem</option><option value='input_points_max'>input_pts</option><option value='cache_hit_rate'>hit_rate</option><option value='cache_miss_count'>cache_miss</option><option value='duration_cv'>CV</option></select></div><div class='table-wrap'><table id='nodeTbl'><thead><tr><th>NodeID</th><th>Graph</th><th>Total</th><th>Self</th><th>P95</th><th>Wk%</th><th>Pts</th><th>Mem</th><th>Hit%</th></tr></thead><tbody></tbody></table></div></div>
<div class='panel'><h3>Events Timeline + Details</h3><div class='controls'><select id='phaseEv'><option value=''>All Phase</option></select><select id='threadEv'><option value=''>All Thread</option><option>GameThread</option><option>Worker</option><option>Unknown</option></select><select id='cacheEv'><option value=''>All Cache</option><option value='hit'>Hit</option><option value='miss'>Miss</option></select><input id='qEv' placeholder='Search node/component/path'/></div><svg id='timeline'></svg><div class='table-wrap'><table id='eventTbl'><thead><tr><th>Start</th><th>Duration</th><th>NodeID</th><th>Phase</th><th>Thread</th><th>Cache</th><th>Component</th><th>Path</th></tr></thead><tbody></tbody></table></div></div>
<div class='row'><div class='panel'><h3>Drill-down (phase/thread/cache)</h3><div class='table-wrap'><table id='drillTbl'><thead><tr><th>Phase</th><th>Thread</th><th>Cache</th><th>Count</th><th>InclusiveTotal</th><th>QueueWaitTotal</th></tr></thead><tbody></tbody></table></div></div><div class='panel'><h3>Thread Load (timeline_windows)</h3><div id='winBox'></div></div></div>
<div class='panel'><h3>Event Flamegraph / Call Tree (events + parent_execution_path)</h3><div class='controls'><label><input type='checkbox' id='onlyCriticalCall'/> Show critical-path nodes only</label></div><svg id='flame'></svg><div class='table-wrap'><table id='callTbl'><thead><tr><th>Depth</th><th>NodeID</th><th>Inclusive</th><th>Self</th><th>Count</th><th>Component</th><th>Path</th></tr></thead><tbody></tbody></table></div></div>
<div class='row'><div class='panel'><h3>Critical Path Gantt (critical_path)</h3><svg id='gantt'></svg></div><div class='panel'><h3>Lifecycle + Redundancy</h3><div id='life'></div><div class='table-wrap'><table id='redTbl'><thead><tr><th>Parent</th><th>Child</th><th>Contrib</th><th>Ratio</th><th>Calls</th></tr></thead><tbody></tbody></table></div></div></div>
<div class='panel'><h3>Exports</h3><ul><li><a href='nodes.csv'>nodes.csv</a></li><li><a href='events.csv'>events.csv</a></li><li><a href='summary.json'>summary.json</a></li></ul></div>`;

const graphs=[...new Set(nodes.map(n=>n.graph_name||'').filter(Boolean))].sort(); graphs.forEach(g=>document.getElementById('graphNode').insertAdjacentHTML('beforeend',`<option>${{esc(g)}}</option>`));
const phases=[...new Set(events.map(e=>e.phase||'').filter(Boolean))].sort(); phases.forEach(p=>document.getElementById('phaseEv').insertAdjacentHTML('beforeend',`<option>${{esc(p)}}</option>`));
function fNodes(){{const q=(document.getElementById('qNode').value||'').toLowerCase();const g=document.getElementById('graphNode').value;const k=document.getElementById('sortNode').value;const arr=nodes.filter(n=>{{const t=`${{n.node_name||''}} ${{n.node_id||''}} ${{n.graph_name||''}} ${{n.component_name||''}}`.toLowerCase();if(q&&!t.includes(q))return false;if(g&&(n.graph_name||'')!==g)return false;return true;}});arr.sort((a,b)=>{{const vA=a[k]||0;const vB=b[k]||0;if(k==='self_ms'){{const sA=a.total_ms>0?vA/a.total_ms:0;const sB=b.total_ms>0?vB/b.total_ms:0;return sB-sA;}}return vB-vA;}});return arr;}}
function renderNodes(){{const tb=document.querySelector('#nodeTbl tbody');tb.innerHTML='';for(const n of fNodes().slice(0,800)){{const tr=document.createElement('tr');const wkPct=n.game_thread_ratio&&n.worker_thread_ratio?pct(n.worker_thread_ratio):'-';const pts=n.input_points_max||0;const ptsStr=pts>=1e9?(pts/1e9).toFixed(1)+'B':pts>=1e6?(pts/1e6).toFixed(1)+'M':pts>=1e3?(pts/1e3).toFixed(1)+'K':pts||'-';const estMem=(n.estimated_memory_bytes||(n.input_points_max+n.output_points_max)*160)||0;const memMB=estMem>=1e9?(estMem/1e9).toFixed(1)+'GB':(estMem/1e6).toFixed(1)+'MB';tr.innerHTML=`<td title='${{esc((n.node_name||'')+' | '+(n.node_id||''))}}'>${{esc(n.node_id||'-')}}</td><td>${{esc(n.graph_name||'')}}</td><td class='${{n.total_ms>100?'bad':''}}'>${{fmt(n.total_ms||0)}}</td><td>${{fmt(n.self_ms||0)}}</td><td>${{fmt(n.p95_ms||0)}}</td><td>${{wkPct}}</td><td>${{ptsStr}}</td><td>${{memMB}}</td><td>${{pct(n.cache_hit_rate||0)}}</td>`;tb.appendChild(tr);}}}}
function fEvents(){{const p=document.getElementById('phaseEv').value;const t=document.getElementById('threadEv').value;const c=document.getElementById('cacheEv').value;const q=(document.getElementById('qEv').value||'').toLowerCase();return events.filter(e=>{{if(p&&(e.phase||'')!==p)return false;if(t&&(e.thread_group||'Unknown')!==t)return false;if(c&&((e.cache_hit?'hit':'miss')!==c))return false;const text=`${{e.node_name||e.node_title||''}} ${{e.component_name||''}} ${{e.execution_path||''}}`.toLowerCase();if(q&&!text.includes(q))return false;return true;}});}}
function renderEvents(){{const arr=fEvents().slice().sort((a,b)=>(a.first_seen_time_ms||0)-(b.first_seen_time_ms||0));const tb=document.querySelector('#eventTbl tbody');tb.innerHTML='';for(const e of arr.slice(0,1200)){{tb.insertAdjacentHTML('beforeend',`<tr><td>${{fmt(e.first_seen_time_ms||0)}}</td><td>${{fmt(Math.max(e.duration_ms||0,e.inclusive_ms||0))}}</td><td title='${{esc((e.node_name||e.node_title||'')+' | '+(e.node_id||''))}}'>${{esc(e.node_id||'-')}}</td><td>${{esc(e.phase||'')}}</td><td>${{esc(e.thread_group||'Unknown')}}</td><td>${{e.cache_hit?'hit':'miss'}}</td><td>${{esc(e.component_name||'')}}</td><td title='${{esc(e.execution_path||'')}}'>${{esc((e.execution_path||'').slice(0,80))}}</td></tr>`);}}}}
function renderTimeline(){{const arr=fEvents();const svg=document.getElementById('timeline');if(!arr.length){{svg.innerHTML='';return;}}const W=svg.clientWidth||1200,H=svg.clientHeight||250;const min=Math.min(...arr.map(e=>e.first_seen_time_ms||0));const max=Math.max(...arr.map(e=>(e.first_seen_time_ms||0)+Math.max(e.duration_ms||0,e.inclusive_ms||0,0.01)));const sx=x=>((x-min)/Math.max(0.001,max-min))*(W-24)+12;const laneY={{GameThread:12,Worker:92,Unknown:172}};const colors={{GameThread:'#0b6ef6',Worker:'#16a34a',Unknown:'#94a3b8'}};let out='';for(const k of ['GameThread','Worker','Unknown']){{out+=`<rect x='8' y='${{laneY[k]}}' width='${{W-16}}' height='70' fill='none' stroke='#dbe2ea'/>`;out+=`<text x='12' y='${{laneY[k]+12}}' font-size='11' fill='#475467'>${{k}}</text>`;}}for(const e of arr.slice(0,2000)){{const lane=['GameThread','Worker'].includes(e.thread_group)?e.thread_group:'Unknown';const s=e.first_seen_time_ms||0;const d=Math.max(e.duration_ms||0,e.inclusive_ms||0,0.01);const x=sx(s),w=Math.max(1,sx(s+d)-x),y=laneY[lane]+18+((e.node_id||'').length*7%44);const miss=e.cache_hit?'':" stroke='#d97706' stroke-width='1'";out+=`<rect x='${{x.toFixed(2)}}' y='${{y.toFixed(2)}}' width='${{w.toFixed(2)}}' height='7' rx='2' fill='${{colors[lane]}}' opacity='0.75'${{miss}}><title>${{esc((e.node_name||e.node_title||'')+' '+d.toFixed(2)+'ms')}}</title></rect>`;}}svg.innerHTML=out;}}
function renderDrill(){{const arr=payload.event_drilldown||[];const tb=document.querySelector('#drillTbl tbody');tb.innerHTML='';for(const r of arr.slice(0,200)){{tb.insertAdjacentHTML('beforeend',`<tr><td>${{esc(r.phase||'')}}</td><td>${{esc(r.thread_group||'')}}</td><td>${{esc(r.cache_state||'')}}</td><td>${{fmt(r.count||0,0)}}</td><td>${{fmt(r.inclusive_total_ms||0)}}</td><td>${{fmt(r.queue_wait_total_ms||0)}}</td></tr>`);}}}}
function renderWins(){{const tw=((run.thread_load||{{}}).timeline_windows)||[];const box=document.getElementById('winBox');if(!tw.length){{box.innerHTML='<div class=\"small\">No timeline_windows data.</div>';return;}}let m=0;tw.forEach(w=>m=Math.max(m,w.game_thread_ms||0,w.worker_ms||0,w.unknown_ms||0));let h='<div class=\"small\">Blue=GT Green=Worker Gray=Unknown</div>';for(const w of tw.slice(0,180)){{const g=((w.game_thread_ms||0)/Math.max(.001,m))*100,k=((w.worker_ms||0)/Math.max(.001,m))*100,u=((w.unknown_ms||0)/Math.max(.001,m))*100;h+=`<div class='small'>${{fmt(w.window_start_ms||0)}}-${{fmt(w.window_end_ms||0)}} ms</div><div style='display:flex;gap:3px;margin:2px 0 6px'><div style='height:10px;background:#0b6ef6;width:${{g.toFixed(2)}}%'></div><div style='height:10px;background:#16a34a;width:${{k.toFixed(2)}}%'></div><div style='height:10px;background:#94a3b8;width:${{u.toFixed(2)}}%'></div></div>`;}}box.innerHTML=h;}}
function renderCallAndFlame(){{const onlyCritical=(document.getElementById('onlyCriticalCall')||{{checked:false}}).checked;const cp=(run.critical_path||[]);const cpPathSet=new Set(cp.map(x=>x.execution_path||'').filter(Boolean));const cpNameSet=new Set(cp.map(x=>(x.node_name||x.node_title||'')).filter(Boolean));let rows=(payload.call_tree_flat||[]);let rects=(payload.call_tree_flame_rects||[]);if(onlyCritical){{rows=rows.filter(r=>cpPathSet.has(r.execution_path||'')||cpNameSet.has(r.node_name||''));rects=rects.filter(r=>cpPathSet.has(r.execution_path||'')||cpNameSet.has(r.node_name||''));}}const tb=document.querySelector('#callTbl tbody');tb.innerHTML='';for(const r of rows.slice(0,800)){{const indent=''.padStart((r.depth||0)*2,'\\u00A0');tb.insertAdjacentHTML('beforeend',`<tr><td>${{fmt(r.depth||0,0)}}</td><td title='${{esc((r.node_name||'')+' | '+(r.node_id||''))}}'>${{indent}}${{esc(r.node_id||'-')}}</td><td>${{fmt(r.inclusive_ms||0)}}</td><td>${{fmt(r.self_ms||0)}}</td><td>${{fmt(r.count||0,0)}}</td><td>${{esc(r.component_name||'')}}</td><td title='${{esc(r.execution_path||'')}}'>${{esc((r.execution_path||'').slice(0,85))}}</td></tr>`);}}const svg=document.getElementById('flame');if(!rects.length){{svg.innerHTML='';return;}}const W=svg.clientWidth||1200,H=svg.clientHeight||320;const maxDepth=Math.max(...rects.map(r=>r.depth||0),0);const rowH=Math.max(12,Math.floor((H-12)/Math.max(1,maxDepth+1)));let out='';for(const r of rects.slice(0,3000)){{const x=8+(r.x0||0)*(W-16),w=Math.max(1,((r.x1||0)-(r.x0||0))*(W-16));const y=H-6-rowH*((r.depth||0)+1);const hue=((r.depth||0)*37 + ((r.node_name||'').length*11))%360;out+=`<rect x='${{x.toFixed(2)}}' y='${{y.toFixed(2)}}' width='${{w.toFixed(2)}}' height='${{Math.max(8,rowH-2)}}' rx='2' fill='hsl(${{hue}},72%,62%)' stroke='#fff'><title>${{esc((r.node_name||'')+' | '+(r.node_id||'')+' | inc='+fmt(r.inclusive_ms||0)+'ms self='+fmt(r.self_ms||0)+'ms')}}</title></rect>`;if(w>90){{out+=`<text x='${{(x+3).toFixed(2)}}' y='${{(y+Math.max(10,rowH-4)).toFixed(2)}}' font-size='10'>${{esc((r.node_id||r.node_name||'').slice(0,18))}}</text>`;}}}}svg.innerHTML=out;}}
function renderGantt(){{const rows=payload.critical_gantt||[];const svg=document.getElementById('gantt');if(!rows.length){{svg.innerHTML='';return;}}const W=svg.clientWidth||1200,H=svg.clientHeight||280;const min=Math.min(...rows.map(r=>r.start_ms||0));const max=Math.max(...rows.map(r=>r.end_ms||0));const sx=x=>((x-min)/Math.max(.001,max-min))*(W-180)+170;let out='';const rowH=Math.max(14,Math.min(24,Math.floor((H-16)/Math.max(1,rows.length))));for(let i=0;i<rows.length;i++){{const r=rows[i],y=8+i*rowH,x=sx(r.start_ms||0),w=Math.max(1,sx(r.end_ms||0)-x);out+=`<text x='6' y='${{y+10}}' font-size='10' fill='#475467'>${{esc((r.node_name||'').slice(0,24))}}</text>`;out+=`<rect x='${{x.toFixed(2)}}' y='${{y.toFixed(2)}}' width='${{w.toFixed(2)}}' height='${{Math.max(6,rowH-3)}}' fill='#0ea5e9' opacity='.75'></rect>`;}}svg.innerHTML=out;}}
function renderLife(){{const life=payload.lifecycle_snapshot||{{}};const creators=life.top_creators||[];const reducers=life.top_reducers||[];const memHot=life.memory_hotspots||[];const redCand=life.redundant_intermediate_candidates||[];document.getElementById('life').innerHTML=`<div>created_points_total: <b>${{fmt(life.created_points_total||0,0)}}</b></div><div>reduced_points_total: <b>${{fmt(life.reduced_points_total||0,0)}}</b></div><div>net_points_change: <b>${{fmt(life.net_points_change||0,0)}}</b></div><div>top_creators: <b>${{fmt(creators.length,0)}}</b> | top_reducers: <b>${{fmt(reducers.length,0)}}</b></div><div>memory_hotspots: <b>${{fmt(memHot.length,0)}}</b> | redundant_candidates: <b>${{fmt(redCand.length,0)}}</b></div><div class='small'>rule: ${{esc(life.redundant_candidate_rule||'')}}</div>`;const red=payload.redundancy||[];const tb=document.querySelector('#redTbl tbody');tb.innerHTML='';for(const r of red.slice(0,150)){{tb.insertAdjacentHTML('beforeend',`<tr><td>${{esc(r.parent_node_title||'')}}</td><td>${{esc(r.child_node_title||'')}}</td><td>${{fmt(r.total_child_inclusive_ms||0)}}</td><td>${{pct(r.redundancy_ratio||0)}}</td><td>${{fmt(r.call_count||0,0)}}</td></tr>`);}}}}
function renderStability(){{const s=payload.stability_view||{{}};const box=document.getElementById('stabilityBox');box.innerHTML=`<div>run_duration_cv: <b>${{fmt(s.run_duration_cv||0,4)}}</b></div><div>parallel_efficiency_cv: <b>${{fmt(s.parallel_efficiency_cv||0,4)}}</b></div><div>top_node_p95_cv: <b>${{fmt(s.top_node_p95_cv||0,4)}}</b></div>`;const cross=s.cross_run_node_stats||[];const tb=document.querySelector('#crossTbl tbody');tb.innerHTML='';for(const r of cross.slice(0,120)){{tb.insertAdjacentHTML('beforeend',`<tr><td title='${{esc((r.node_name||r.node_title||'')+' | '+(r.node_id||''))}}'>${{esc(r.node_id||'-')}}</td><td>${{fmt(r.p50_ms||0)}}</td><td>${{fmt(r.p95_ms||0)}}</td><td>${{fmt(r.cv||r.duration_cv||0,4)}}</td><td>${{fmt(r.sample_count||0,0)}}</td></tr>`);}}const tt=document.querySelector('#topTotalTbl tbody');tt.innerHTML='';for(const r of (s.top_n_by_total_ms||[]).slice(0,30)){{const st=r.total_ms>0?(r.self_ms||0)/r.total_ms*100:0;tt.insertAdjacentHTML('beforeend',`<tr><td title='${{esc((r.node_name||r.node_title||'')+' | '+(r.node_id||''))}}'>${{esc(r.node_id||'-')}}</td><td>${{esc(r.graph_name||'')}}</td><td>${{fmt(r.total_ms||0)}}</td><td>${{fmt(r.self_ms||0)}}</td><td>${{fmt(r.p95_ms||0)}}</td><td class='${{st>80?'bad':'good'}}'>${{st.toFixed(0)}}%</td></tr>`);}}const tm=document.querySelector('#topMemTbl tbody');if(tm){{tm.innerHTML='';const memTop=s.node_memory_top||[];for(const r of memTop.slice(0,20)){{const ptsF=n=>n>=1e9?(n/1e9).toFixed(1)+'B':n>=1e6?(n/1e6).toFixed(1)+'M':n>=1e3?(n/1e3).toFixed(1)+'K':(n||'-');tm.insertAdjacentHTML('beforeend',`<tr><td title='${{esc((r.node_name||'')+' | '+(r.node_id||''))}}'>${{esc(r.node_id||'-')}}</td><td>${{esc(r.graph_name||'')}}</td><td>${{ptsF(r.input_points_max||0)}}</td><td>${{ptsF(r.output_points_max||0)}}</td><td>${{fmt((r.estimated_memory_bytes||0)/1e6,1)}}MB</td></tr>`);}}}}const tp=document.querySelector('#topPeakTbl tbody');tp.innerHTML='';for(const r of (s.top_n_by_peak_ms||[]).slice(0,30)){{tp.insertAdjacentHTML('beforeend',`<tr><td title='${{esc((r.node_name||r.node_title||'')+' | '+(r.node_id||''))}}'>${{esc(r.node_id||'-')}}</td><td>${{fmt(r.max_ms||r.peak_ms||0)}}</td><td>${{fmt(r.call_count||0,0)}}</td><td>${{esc(r.graph_name||'')}}</td></tr>`);}}}}
function renderCacheHeatmap(){{const rows=payload.cache_heatmap||[];const box=document.getElementById('cacheHeat');if(!rows.length){{box.innerHTML='<div class=\"small\">No node_cache_stats data.</div>';return;}}const allSame=rows.every(r=>(r.hit_rate||0)===rows[0].hit_rate);if(allSame){{box.innerHTML=`<div class='small'>All nodes show same cache hit rate (${{pct(rows[0].hit_rate||0)}}). Cache heatmap is not useful for this run.</div>`;return;}}let h='';for(const r of rows){{const hit=Math.max(0,Math.min(1,r.hit_rate||0));const miss=(r.miss_count||0);const bad=hit<0.4?'bad':'';const w=(hit*100).toFixed(1);h+=`<div style='display:grid;grid-template-columns:320px 1fr 120px;gap:8px;align-items:center;margin:4px 0'><div title='${{esc(r.node_id||'')}}'>${{esc((r.node_name||r.node_id||'Unknown').slice(0,40))}}</div><div style='height:10px;background:#e5e7eb;border-radius:8px;overflow:hidden'><div style='height:10px;width:${{w}}%;background:${{hit<0.4?'#ef4444':hit<0.75?'#f59e0b':'#16a34a'}}'></div></div><div class='${{bad}}'>hit=${{(hit*100).toFixed(1)}}% miss=${{fmt(miss,0)}}</div></div>`;}}box.innerHTML=h;}}
['qNode','graphNode','sortNode'].forEach(id=>document.getElementById(id).addEventListener('input',renderNodes));['phaseEv','threadEv','cacheEv','qEv'].forEach(id=>document.getElementById(id).addEventListener('input',()=>{{renderEvents();renderTimeline();}}));document.getElementById('onlyCriticalCall').addEventListener('change',renderCallAndFlame);
function renderQueueTop(){{const rows=payload.queue_wait_top||[];const box=document.getElementById('queueTop');if(!box||!rows.length)return;let h='<div class=\"table-wrap\"><table><thead><tr><th>NodeID</th><th>Graph</th><th>QueueTotal</th><th>QueueAvg</th><th>Total</th><th>%Wait</th></tr></thead><tbody>';for(const r of rows){{const pctWait=r.total_ms>0?(r.queue_wait_total_ms/r.total_ms*100).toFixed(1):'-';h+=`<tr><td title='${{esc((r.node_name||'')+' | '+(r.node_id||''))}}'>${{esc(r.node_id||'-')}}</td><td>${{esc(r.graph_name||'')}}</td><td>${{fmt(r.queue_wait_total_ms||0)}}</td><td>${{fmt(r.queue_wait_avg_ms||0)}}</td><td>${{fmt(r.total_ms||0)}}</td><td class='${{(r.queue_wait_total_ms/r.total_ms)>0.5?'bad':''}}'>${{pctWait}}%</td></tr>`;}}h+='</tbody></table></div>';box.innerHTML=h;}}
renderStability();renderCacheHeatmap();renderQueueTop();renderNodes();renderEvents();renderTimeline();renderDrill();renderWins();renderCallAndFlame();renderGantt();renderLife();
</script></body></html>"""
    output_html.write_text(html, encoding='utf-8')


def maybe_capture_screenshot(html_path: Path, out_png: Path):
    candidates = [
        r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
        r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
        r"C:\Program Files\Google\Chrome\Application\chrome.exe",
        r"C:\Program Files (x86)\Google\Chrome\Application\chrome.exe",
    ]
    browser = next((c for c in candidates if Path(c).exists()), None)
    if not browser:
        return False, 'no_browser'
    try:
        subprocess.run([browser, '--headless=new', f'--screenshot={str(out_png)}', '--window-size=1920,1080', html_path.resolve().as_uri()], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        return out_png.exists(), 'ok'
    except Exception as e:
        return False, str(e)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--input-json', default='')
    ap.add_argument('--baseline-json', default='')
    ap.add_argument('--output-dir', default='')
    ap.add_argument('--project-root', default='')
    ap.add_argument('--skip-screenshot', action='store_true')
    args = ap.parse_args()

    pcg_dir = resolve_pcg_dir(args.project_root, args.input_json)
    if pcg_dir is None:
        print('ERROR: unable to locate Saved/Profiling/PCG. Provide --project-root or --input-json.')
        return 2

    if args.input_json:
        run_path = Path(args.input_json).expanduser().resolve()
        run = read_json(run_path)
    else:
        run_path, run = find_latest_valid_json(pcg_dir)
        if run is None:
            print('ERROR: no valid profiling json found')
            return 2

    baseline = read_json(Path(args.baseline_json).expanduser().resolve()) if args.baseline_json else None
    out_dir = Path(args.output_dir).expanduser().resolve() if args.output_dir else (pcg_dir / 'Visualization' / datetime.now().strftime('%Y%m%d_%H%M%S'))
    out_dir.mkdir(parents=True, exist_ok=True)

    nodes = get_nodes(run)
    events = get_events(run)
    for n in nodes:
        n['severity'] = severity_of(n)

    diagnosis = build_diagnosis(run, nodes)
    compare = compare_runs(run, baseline)
    event_drilldown = build_event_drilldown(events)
    call_tree_model = build_call_tree_model(events)
    critical_gantt = []
    for e in (run.get('critical_path', []) or []):
        s = float(e.get('start_ms', e.get('first_seen_time_ms', 0)) or 0)
        inc = float(e.get('inclusive_ms', 0) or 0)
        critical_gantt.append({'node_name': e.get('node_name') or e.get('node_title') or '', 'start_ms': s, 'end_ms': s + max(inc, 0.01)})
    critical_gantt.sort(key=lambda x: x['start_ms'])
    redundancy = build_redundancy(run)
    lifecycle_snapshot = build_lifecycle_snapshot(run)
    cache_heatmap = build_cache_heatmap(run)
    stability_view = build_stability_view(run)

    # Queue wait analysis
    qw_nodes = sorted([n for n in nodes if (n.get('queue_wait_avg_ms') or 0) > 0], key=lambda n: n.get('queue_wait_total_ms', 0) or 0, reverse=True)[:10]
    queue_wait_top = [{'node_id': n.get('node_id', ''), 'node_name': n.get('node_name', ''), 'graph_name': n.get('graph_name', ''),
                       'queue_wait_total_ms': n.get('queue_wait_total_ms', 0), 'queue_wait_avg_ms': n.get('queue_wait_avg_ms', 0),
                       'total_ms': n.get('total_ms', 0), 'self_ms': n.get('self_ms', 0)} for n in qw_nodes]
    queue_wait_total = sum((n.get('queue_wait_total_ms', 0) or 0) for n in nodes)

    # Memory top nodes
    mem_nodes = []
    for n in nodes:
        est = n.get('estimated_memory_bytes') or (n.get('input_points_max', 0) + n.get('output_points_max', 0)) * 160
        mem_nodes.append({**n, 'estimated_memory_bytes': est})
    mem_nodes.sort(key=lambda n: n['estimated_memory_bytes'], reverse=True)
    node_memory_top = [{'node_name': n.get('node_name', ''), 'node_id': n.get('node_id', ''),
                        'graph_name': n.get('graph_name', ''), 'input_points_max': n.get('input_points_max', 0),
                        'output_points_max': n.get('output_points_max', 0), 'estimated_memory_bytes': n['estimated_memory_bytes']}
                       for n in mem_nodes[:30]]
    stability_view['node_memory_top'] = node_memory_top

    write_csv(out_dir / 'nodes.csv', nodes, [
        'node_id','node_name','graph_name','component_name','data_type','severity','total_ms','p95_ms','max_ms','avg_ms','call_count','duration_nonzero_rate','duration_nonzero_samples','sparse_sample_count','game_thread_ratio','worker_thread_ratio','input_points_max','output_points_max','output_input_ratio_max','cache_hit_rate','cache_miss_count','estimated_memory_bytes','measured_memory_delta_sum_bytes'
    ])
    write_csv(out_dir / 'events.csv', events, [
        'node_id','node_name','component_name','graph_name','phase','thread_group','first_seen_time_ms','duration_ms','inclusive_ms','self_ms','queue_wait_ms','execution_ms','input_points','output_points','estimated_memory_bytes','cache_hit','cache_miss_reason','memory_delta_bytes','execution_path','parent_execution_path'
    ])

    summary = {
        'generated_at': datetime.now().isoformat(),
        'input_json': str(run_path),
        'baseline_json': args.baseline_json,
        'output_dir': str(out_dir),
        'run_name': run.get('run_name', ''),
        'node_count': run.get('node_count', 0),
        'event_count': len(events),
        'schema_version': run.get('schema_version', ''),
        'missing_required_event_count': run.get('missing_required_event_count', 0),
        'missing_required_field_total': run.get('missing_required_field_total', 0),
        'diagnosis': diagnosis,
    }
    (out_dir / 'summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding='utf-8')
    (out_dir / 'compare.json').write_text(json.dumps(compare, ensure_ascii=False, indent=2), encoding='utf-8')

    render_dashboard(out_dir / 'dashboard.html', {
        'run': run,
        'nodes': nodes,
        'events': events,
        'diagnosis': diagnosis,
        'compare': compare,
        'event_drilldown': event_drilldown,
        'call_tree_flat': call_tree_model['flat'],
        'call_tree_flame_rects': call_tree_model['flame_rects'],
        'critical_gantt': critical_gantt,
        'redundancy': redundancy,
        'lifecycle_snapshot': lifecycle_snapshot,
        'cache_heatmap': cache_heatmap,
        'stability_view': stability_view,
        'queue_wait_top': queue_wait_top,
        'queue_wait_total': queue_wait_total,
        'node_memory_top': node_memory_top,
    })

    if args.skip_screenshot:
        ok, msg = False, 'skipped'
    else:
        ok, msg = maybe_capture_screenshot(out_dir / 'dashboard.html', out_dir / 'dashboard.png')

    print(f'INPUT_JSON={run_path}')
    print(f'OUTPUT_DIR={out_dir}')
    print(f'DASHBOARD_HTML={out_dir / "dashboard.html"}')
    print(f'CSV_NODES={out_dir / "nodes.csv"}')
    print(f'CSV_EVENTS={out_dir / "events.csv"}')
    print(f'COMPARE_JSON={out_dir / "compare.json"}')
    print(f'SCREENSHOT={(out_dir / "dashboard.png") if ok else "N/A"}')
    print(f'SCREENSHOT_STATUS={msg}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
