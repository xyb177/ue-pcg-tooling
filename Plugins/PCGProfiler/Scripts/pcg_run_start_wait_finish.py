import os
import runpy
import time
import unreal

TARGET_MAP_NAME = "ElectricDreams_PCG"
MAX_WAIT_SECONDS = 300.0
POLL_SECONDS = 0.5
STABLE_TICKS_REQUIRED = 10

ROOT = r"F:/UnrealEngine-5.7.3-release/Plugins/PCGProfiler/Scripts"
START_SCRIPT = os.path.join(ROOT, "pcg_start_run.py")
FINISH_SCRIPT = os.path.join(ROOT, "pcg_finish_export.py")

editor_subsystem = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
world = editor_subsystem.get_editor_world() if editor_subsystem else None
if not world:
    unreal.log_error("[PCG-PIPE] No editor world.")
    raise SystemExit(2)

world_name = world.get_name() if hasattr(world, "get_name") else ""
unreal.log("[PCG-PIPE] world={}".format(world_name))
if TARGET_MAP_NAME not in world_name:
    unreal.log_warning("[PCG-PIPE] world name mismatch, expected contains '{}'".format(TARGET_MAP_NAME))

if not os.path.exists(START_SCRIPT):
    unreal.log_error("[PCG-PIPE] Missing start script: {}".format(START_SCRIPT))
    raise SystemExit(2)
if not os.path.exists(FINISH_SCRIPT):
    unreal.log_error("[PCG-PIPE] Missing finish script: {}".format(FINISH_SCRIPT))
    raise SystemExit(2)

unreal.log("[PCG-PIPE] Step1: run start script")
runpy.run_path(START_SCRIPT, run_name="__main__")

start_time = time.time()
stable_ticks = 0
next_log = start_time

unreal.log("[PCG-PIPE] Step2: wait for level/pcg stable")
while True:
    elapsed = time.time() - start_time

    world = editor_subsystem.get_editor_world() if editor_subsystem else world
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

    if state_api_seen and active == 0 and dirty == 0:
        stable_ticks += 1
    elif state_api_seen:
        stable_ticks = 0

    if state_api_seen and stable_ticks >= STABLE_TICKS_REQUIRED:
        unreal.log("[PCG-PIPE] stable reached elapsed={:.1f}s components={}".format(elapsed, len(components)))
        break

    if elapsed >= MAX_WAIT_SECONDS:
        unreal.log_warning("[PCG-PIPE] wait timeout elapsed={:.1f}s active={} dirty={} comps={} state_api_seen={}".format(elapsed, active, dirty, len(components), state_api_seen))
        break

    now = time.time()
    if now >= next_log:
        unreal.log("[PCG-PIPE] waiting elapsed={:.1f}s active={} dirty={} comps={} state_api_seen={}".format(elapsed, active, dirty, len(components), state_api_seen))
        next_log = now + 5.0

    time.sleep(POLL_SECONDS)

unreal.log("[PCG-PIPE] Step3: run finish script")
runpy.run_path(FINISH_SCRIPT, run_name="__main__")
unreal.log("[PCG-PIPE] done")
