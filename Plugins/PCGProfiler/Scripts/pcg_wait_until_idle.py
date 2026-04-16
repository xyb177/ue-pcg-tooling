import time
import unreal

MAX_WAIT_SECONDS = 180.0
POLL_SECONDS = 0.5
STABLE_TICKS_REQUIRED = 8
NO_STATE_API_FALLBACK_SECONDS = 25.0

editor_subsystem = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
world = editor_subsystem.get_editor_world() if editor_subsystem else None
if not world:
    unreal.log_error("[PCG-AUTO] No editor world.")
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
    unreal.log_warning("[PCG-AUTO] wait: no PCG components found, continue.")
    raise SystemExit(0)

status_getters = [
    "is_generating",
    "is_generation_in_progress",
    "is_refresh_in_progress",
]
dirty_getters = [
    "is_dirty",
    "is_component_dirty",
]


def _query_bool(comp, names):
    for name in names:
        attr = getattr(comp, name, None)
        if callable(attr):
            try:
                return bool(attr())
            except Exception:
                continue
    return None

stable_ticks = 0
state_api_seen = False
start = time.time()
next_log_time = start

while True:
    now = time.time()
    elapsed = now - start
    active = 0
    dirty = 0

    for comp in components:
        generating = _query_bool(comp, status_getters)
        comp_dirty = _query_bool(comp, dirty_getters)

        if generating is not None or comp_dirty is not None:
            state_api_seen = True

        if generating:
            active += 1
        if comp_dirty:
            dirty += 1

    if state_api_seen:
        if active == 0 and dirty == 0:
            stable_ticks += 1
        else:
            stable_ticks = 0

        if stable_ticks >= STABLE_TICKS_REQUIRED:
            unreal.log("[PCG-AUTO] wait complete: active=0 dirty=0 stable_ticks={}".format(stable_ticks))
            break
    else:
        if elapsed >= NO_STATE_API_FALLBACK_SECONDS:
            unreal.log_warning("[PCG-AUTO] wait fallback reached (no state API), elapsed={:.1f}s".format(elapsed))
            break

    if elapsed >= MAX_WAIT_SECONDS:
        unreal.log_warning("[PCG-AUTO] wait timeout: active={} dirty={} elapsed={:.1f}s".format(active, dirty, elapsed))
        break

    if now >= next_log_time:
        unreal.log("[PCG-AUTO] wait polling: active={} dirty={} elapsed={:.1f}s state_api_seen={}".format(active, dirty, elapsed, state_api_seen))
        next_log_time = now + 5.0

    time.sleep(POLL_SECONDS)
