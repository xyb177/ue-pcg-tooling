import json
import os
import runpy
import time
import unreal

# Semi-auto batch runner (run inside opened editor map)
ITERATIONS = 1
GAP_SECONDS = 1.0
MAX_WAIT_SECONDS = 180.0
POLL_SECONDS = 0.5
STABLE_TICKS_REQUIRED = 10
REQUIRE_NODE_GT_ZERO = True

SCRIPTS_DIR = r"F:/UnrealEngine-5.7.3-release/Plugins/PCGProfiler/Scripts"
START_SCRIPT = os.path.join(SCRIPTS_DIR, "pcg_start_run.py")
FINISH_SCRIPT = os.path.join(SCRIPTS_DIR, "pcg_finish_export.py")
PROJECT_ROOT = r"F:/Unreal Projects/ElectricDreamsEnv"
PROFILE_DIR = os.path.join(PROJECT_ROOT, "Saved", "Profiling", "PCG")
SUMMARY_DIR = os.path.join(PROJECT_ROOT, "Saved", "Profiling", "PCG", "Benchmark")
os.makedirs(SUMMARY_DIR, exist_ok=True)

editor_subsystem = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
world = editor_subsystem.get_editor_world() if editor_subsystem else None
if not world:
    unreal.log_error("[PCG-SEMI] No editor world.")
    raise SystemExit(2)

if not os.path.exists(START_SCRIPT) or not os.path.exists(FINISH_SCRIPT):
    unreal.log_error("[PCG-SEMI] Missing start/finish script.")
    raise SystemExit(2)


def _list_jsons():
    out = []
    if not os.path.isdir(PROFILE_DIR):
        return out
    for n in os.listdir(PROFILE_DIR):
        if n.lower().endswith(".json") and n.startswith("PCGProfiler_"):
            p = os.path.join(PROFILE_DIR, n)
            try:
                out.append((os.path.getmtime(p), p))
            except Exception:
                pass
    out.sort(reverse=True)
    return [x[1] for x in out]


def _wait_until_stable():
    start_t = time.time()
    stable_ticks = 0
    next_log_t = start_t

    while True:
        actors = actor_subsystem.get_all_level_actors() if actor_subsystem else []
        components = []
        for actor in actors:
            try:
                comps = actor.get_components_by_class(unreal.PCGComponent)
                if comps:
                    components.extend(comps)
            except Exception:
                pass

        active = 0
        dirty = 0
        state_api_seen = False

        for comp in components:
            generating = None
            for n in ["is_generation_in_progress", "is_refresh_in_progress", "is_generating"]:
                fn = getattr(comp, n, None)
                if callable(fn):
                    try:
                        generating = bool(fn())
                        state_api_seen = True
                        break
                    except Exception:
                        pass

            comp_dirty = None
            for n in ["is_dirty", "is_component_dirty"]:
                fn = getattr(comp, n, None)
                if callable(fn):
                    try:
                        comp_dirty = bool(fn())
                        state_api_seen = True
                        break
                    except Exception:
                        pass

            if generating:
                active += 1
            if comp_dirty:
                dirty += 1

        elapsed = time.time() - start_t

        if state_api_seen and active == 0 and dirty == 0:
            stable_ticks += 1
        elif state_api_seen:
            stable_ticks = 0

        if state_api_seen and stable_ticks >= STABLE_TICKS_REQUIRED:
            return {"ok": True, "elapsed": elapsed, "active": active, "dirty": dirty, "state_api_seen": True}

        if elapsed >= MAX_WAIT_SECONDS:
            return {"ok": False, "elapsed": elapsed, "active": active, "dirty": dirty, "state_api_seen": state_api_seen}

        now = time.time()
        if now >= next_log_t:
            unreal.log("[PCG-SEMI] waiting elapsed={:.1f}s active={} dirty={} state_api_seen={}".format(elapsed, active, dirty, state_api_seen))
            next_log_t = now + 5.0

        time.sleep(POLL_SECONDS)


def _read_json(path):
    try:
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f), None
    except Exception as e:
        return None, str(e)


world_name = world.get_name() if hasattr(world, "get_name") else "UnknownWorld"
unreal.log("[PCG-SEMI] Start batch on world={} iterations={}".format(world_name, ITERATIONS))

results = []
for i in range(1, ITERATIONS + 1):
    unreal.log("[PCG-SEMI] Iteration {}/{} start".format(i, ITERATIONS))
    before = set(_list_jsons())

    runpy.run_path(START_SCRIPT, run_name="__main__")
    wait_info = _wait_until_stable()
    runpy.run_path(FINISH_SCRIPT, run_name="__main__")

    time.sleep(0.5)
    after = _list_jsons()
    new_json = None
    for p in after:
        if p not in before:
            new_json = p
            break
    if not new_json and after:
        new_json = after[0]

    item = {
        "iteration": i,
        "wait_ok": wait_info["ok"],
        "wait_elapsed_s": round(wait_info["elapsed"], 3),
        "json_path": new_json,
        "node_count": None,
        "events_count": None,
        "run_duration_ms": None,
        "json_parse_error": None,
        "valid": False,
    }

    if new_json:
        obj, err = _read_json(new_json)
        if err:
            item["json_parse_error"] = err
        else:
            item["node_count"] = int(obj.get("node_count", 0) or 0)
            item["events_count"] = len(obj.get("events", []) or [])
            item["run_duration_ms"] = obj.get("run_duration_ms", 0)
            if REQUIRE_NODE_GT_ZERO:
                item["valid"] = item["node_count"] > 0
            else:
                item["valid"] = True

    results.append(item)
    unreal.log("[PCG-SEMI] Iteration {} done: valid={} node_count={} json={}".format(i, item["valid"], item["node_count"], item["json_path"]))

    if i < ITERATIONS and GAP_SECONDS > 0:
        time.sleep(GAP_SECONDS)

success = sum(1 for r in results if r["valid"])
summary = {
    "generated_at_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
    "world": world_name,
    "iterations": ITERATIONS,
    "require_node_gt_zero": REQUIRE_NODE_GT_ZERO,
    "success_count": success,
    "failure_count": ITERATIONS - success,
    "success_rate": (float(success) / float(ITERATIONS)) if ITERATIONS > 0 else 0.0,
    "results": results,
}

stamp = time.strftime("%Y%m%d_%H%M%S", time.localtime())
out_path = os.path.join(SUMMARY_DIR, "semi_auto_summary_{}.json".format(stamp))
with open(out_path, "w", encoding="utf-8") as f:
    json.dump(summary, f, ensure_ascii=False, indent=2)

unreal.log("[PCG-SEMI] Batch finished: success={}/{} rate={:.1%}".format(success, ITERATIONS, summary["success_rate"]))
unreal.log("[PCG-SEMI] Summary: {}".format(out_path))
