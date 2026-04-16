import time
import unreal

MAX_WAIT_SECONDS = 300.0
POLL_SECONDS = 0.5
STABLE_TICKS_REQUIRED = 10

editor_subsystem = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
world = editor_subsystem.get_editor_world() if editor_subsystem else None
if not world:
    unreal.log_error("[PCG-PROBE] No editor world.")
    raise SystemExit(2)

actors = actor_subsystem.get_all_level_actors() if actor_subsystem else []
components = []
for actor in actors:
    try:
        comps = actor.get_components_by_class(unreal.PCGComponent)
        if comps:
            components.extend(comps)
    except Exception:
        pass

if not components:
    unreal.log_warning("[PCG-PROBE] No PCG components found.")
    raise SystemExit(0)

unreal.log("开始PCG测试")
unreal.log("[PCG-PROBE] PCG components={}".format(len(components)))

for i, comp in enumerate(components):
    try:
        comp.cleanup_local(True)
    except Exception as e:
        unreal.log_warning("[PCG-PROBE] cleanup_local failed on {}: {}".format(i, e))

for i, comp in enumerate(components):
    try:
        comp.generate_local(True)
    except Exception:
        try:
            comp.generate(True)
        except Exception as e:
            unreal.log_warning("[PCG-PROBE] generate failed on {}: {}".format(i, e))

start_t = time.time()
stable_ticks = 0
next_log_t = start_t

while True:
    active = 0
    dirty = 0
    state_api_seen = False

    for comp in components:
        generating = None
        for name in ["is_generation_in_progress", "is_refresh_in_progress", "is_generating"]:
            fn = getattr(comp, name, None)
            if callable(fn):
                try:
                    generating = bool(fn())
                    state_api_seen = True
                    break
                except Exception:
                    pass

        is_dirty = None
        for name in ["is_dirty", "is_component_dirty"]:
            fn = getattr(comp, name, None)
            if callable(fn):
                try:
                    is_dirty = bool(fn())
                    state_api_seen = True
                    break
                except Exception:
                    pass

        if generating:
            active += 1
        if is_dirty:
            dirty += 1

    elapsed = time.time() - start_t

    if state_api_seen and active == 0 and dirty == 0:
        stable_ticks += 1
    elif state_api_seen:
        stable_ticks = 0

    if state_api_seen and stable_ticks >= STABLE_TICKS_REQUIRED:
        unreal.log("结束PCG测试")
        break

    if elapsed >= MAX_WAIT_SECONDS:
        unreal.log_warning("[PCG-PROBE] timeout: active={} dirty={}".format(active, dirty))
        unreal.log("结束PCG测试")
        break

    now = time.time()
    if now >= next_log_t:
        unreal.log("[PCG-PROBE] waiting... active={} dirty={} elapsed={:.1f}s".format(active, dirty, elapsed))
        next_log_t = now + 5.0

    time.sleep(POLL_SECONDS)
