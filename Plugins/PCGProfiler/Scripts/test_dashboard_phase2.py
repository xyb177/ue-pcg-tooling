#!/usr/bin/env python3
import json
import subprocess
import sys
from pathlib import Path


def main() -> int:
    script_dir = Path(__file__).resolve().parent
    project_root = script_dir.parent
    out_dir = project_root / "Reports" / "dashboard_phase2_regression"
    out_dir.mkdir(parents=True, exist_ok=True)

    fixture_path = out_dir / "phase2_fixture.json"
    fixture = {
        "run_name": "Phase2Fixture",
        "map_name": "FixtureMap",
        "run_duration_ms": 3210.0,
        "node_count": 4,
        "nodes": [
            {"node_id": "A", "node_name": "RootA", "graph_name": "G", "total_ms": 120.0, "p95_ms": 70.0, "cache_hit_rate": 0.4, "cache_miss_count": 3, "error_count": 0, "warning_count": 0, "cancelled_count": 0},
            {"node_id": "B", "node_name": "ChildB", "graph_name": "G", "total_ms": 70.0, "p95_ms": 45.0, "cache_hit_rate": 0.6, "cache_miss_count": 2, "error_count": 0, "warning_count": 0, "cancelled_count": 0},
        ],
        "events": [
            {"node_id": "A", "node_name": "RootA", "phase": "Execute", "thread_group": "GameThread", "first_seen_time_ms": 0.0, "duration_ms": 40.0, "inclusive_ms": 40.0, "self_ms": 18.0, "execution_path": "/A", "parent_execution_path": ""},
            {"node_id": "B", "node_name": "ChildB", "phase": "Execute", "thread_group": "Worker", "first_seen_time_ms": 8.0, "duration_ms": 14.0, "inclusive_ms": 14.0, "self_ms": 10.0, "execution_path": "/A/B", "parent_execution_path": "/A"},
            {"node_id": "C", "node_name": "ChildC", "phase": "Execute", "thread_group": "Worker", "first_seen_time_ms": 24.0, "duration_ms": 8.0, "inclusive_ms": 8.0, "self_ms": 8.0, "execution_path": "/A/C", "parent_execution_path": "/A"},
            {"node_id": "D", "node_name": "RootD", "phase": "PostExecute", "thread_group": "GameThread", "first_seen_time_ms": 52.0, "duration_ms": 11.0, "inclusive_ms": 11.0, "self_ms": 11.0, "execution_path": "/D", "parent_execution_path": ""},
        ],
        "critical_path": [
            {"node_id": "A", "node_title": "RootA", "start_ms": 0.0, "inclusive_ms": 40.0},
            {"node_id": "B", "node_title": "ChildB", "start_ms": 8.0, "inclusive_ms": 14.0},
        ],
        "lifecycle_tracking": {
            "created_points_total": 5000,
            "reduced_points_total": 1200,
            "net_points_change": 3800,
            "top_creators": [{"node_id": "A", "node_name": "RootA", "created_points": 3000}],
            "top_reducers": [{"node_id": "B", "node_name": "ChildB", "reduced_points": 700}],
        },
        "memory_insights": {
            "memory_hotspots": [{"node_id": "A", "node_name": "RootA", "estimated_memory_bytes": 1024 * 1024}],
            "redundant_intermediate_candidates": [{"node_id": "C", "node_name": "ChildC", "throughput_points": 60000}],
            "redundant_candidate_rule": "throughput_points>50000 && delta_ratio<0.05 && avg_ms<5",
        },
        "link_contribution_graph": [
            {"parent_node_id": "A", "parent_node_title": "RootA", "child_node_id": "B", "child_node_title": "ChildB", "total_child_inclusive_ms": 14.0, "call_count": 1},
            {"parent_node_id": "A", "parent_node_title": "RootA", "child_node_id": "C", "child_node_title": "ChildC", "total_child_inclusive_ms": 8.0, "call_count": 1},
        ],
        "thread_load": {"parallel_efficiency": {"score": 75.0}, "timeline_windows": [{"window_start_ms": 0.0, "window_end_ms": 100.0, "game_thread_ms": 51.0, "worker_ms": 22.0, "unknown_ms": 0.0}]},
        "cache_analysis": {"graph_cache_hit_rate": 0.5},
        "scale_summary": {"global_output_input_ratio": 1.2},
    }
    fixture_path.write_text(json.dumps(fixture, ensure_ascii=False, indent=2), encoding="utf-8")

    generator = script_dir / "generate_pcg_dashboard.py"
    cmd = [sys.executable, str(generator), "--input-json", str(fixture_path), "--output-dir", str(out_dir), "--skip-screenshot"]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        print(proc.stdout)
        print(proc.stderr)
        raise SystemExit(proc.returncode)

    dashboard = out_dir / "dashboard.html"
    nodes_csv = out_dir / "nodes.csv"
    events_csv = out_dir / "events.csv"
    summary_json = out_dir / "summary.json"
    compare_json = out_dir / "compare.json"
    for p in [dashboard, nodes_csv, events_csv, summary_json, compare_json]:
        if not p.exists():
            raise SystemExit(f"missing output: {p}")

    html = dashboard.read_text(encoding="utf-8")
    required_tokens = [
        "事件火焰图/调用树（events + parent_execution_path）",
        "关键路径甘特图（critical_path）",
        "生命周期与冗余链路",
        "call_tree_flame_rects",
        "critical_gantt",
    ]
    for token in required_tokens:
        if token not in html:
            raise SystemExit(f"missing token in html: {token}")

    summary = json.loads(summary_json.read_text(encoding="utf-8"))
    if int(summary.get("event_count", 0)) < 4:
        raise SystemExit("unexpected event_count in summary")

    print(f"PASS: {dashboard}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
