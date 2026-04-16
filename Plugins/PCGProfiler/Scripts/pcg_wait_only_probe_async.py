import unreal

STABLE_TICKS_REQUIRED = 10
MAX_TICKS = 60 * 60 * 5
NO_API_STABLE_TICKS_REQUIRED = 180  # ~3s @60fps

_state = {
    "stable_ticks": 0,
    "no_api_stable_ticks": 0,
    "tick_count": 0,
    "callback": None,
    "components": [],
}


def _stop(msg):
    cb = _state.get("callback")
    if cb is not None:
        try:
            unreal.unregister_slate_pre_tick_callback(cb)
        except Exception:
            pass
        _state["callback"] = None
    unreal.log(msg)


def _collect_components():
    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors = actor_subsystem.get_all_level_actors() if actor_subsystem else []
    comps = []
    for actor in actors:
        try:
            c = actor.get_components_by_class(unreal.PCGComponent)
            if c:
                comps.extend(c)
        except Exception:
            pass
    return comps


def _query_bool(comp, names):
    for n in names:
        fn = getattr(comp, n, None)
        if callable(fn):
            try:
                return True, bool(fn())
            except Exception:
                pass
    return False, False


def _tick(_dt):
    _state["tick_count"] += 1

    comps = _state["components"]
    if not comps:
        _stop("[PCG-PROBE-ASYNC] No PCG components, stop.")
        return

    active = 0
    dirty = 0
    state_api_seen = False

    for comp in comps:
        has_gen, is_gen = _query_bool(comp, ["is_generation_in_progress", "is_refresh_in_progress", "is_generating"])
        has_dirty, is_dirty = _query_bool(comp, ["is_dirty", "is_component_dirty"])
        state_api_seen = state_api_seen or has_gen or has_dirty
        if is_gen:
            active += 1
        if is_dirty:
            dirty += 1

    # Normal path when component exposes state APIs
    if state_api_seen and active == 0 and dirty == 0:
        _state["stable_ticks"] += 1
    elif state_api_seen:
        _state["stable_ticks"] = 0

    # Fallback path when no state API is available
    if not state_api_seen:
        if active == 0 and dirty == 0:
            _state["no_api_stable_ticks"] += 1
        else:
            _state["no_api_stable_ticks"] = 0

    if _state["tick_count"] % 120 == 0:
        unreal.log("[PCG-PROBE-ASYNC] ticking... active={} dirty={} stable={} no_api_stable={} state_api_seen={}".format(
            active, dirty, _state["stable_ticks"], _state["no_api_stable_ticks"], state_api_seen
        ))

    if state_api_seen and _state["stable_ticks"] >= STABLE_TICKS_REQUIRED:
        _stop("结束PCG测试")
        return

    if (not state_api_seen) and _state["no_api_stable_ticks"] >= NO_API_STABLE_TICKS_REQUIRED:
        _stop("结束PCG测试")
        return

    if _state["tick_count"] >= MAX_TICKS:
        _stop("[PCG-PROBE-ASYNC] timeout, stop.")


editor_subsystem = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = editor_subsystem.get_editor_world() if editor_subsystem else None
if not world:
    unreal.log_error("[PCG-PROBE-ASYNC] No editor world.")
    raise SystemExit(2)

comps = _collect_components()
if not comps:
    unreal.log_warning("[PCG-PROBE-ASYNC] No PCG components found.")
    raise SystemExit(0)

unreal.log("开始PCG测试")
unreal.log("[PCG-PROBE-ASYNC] components={}".format(len(comps)))

for i, comp in enumerate(comps):
    try:
        comp.cleanup_local(True)
    except Exception as e:
        unreal.log_warning("[PCG-PROBE-ASYNC] cleanup_local failed on {}: {}".format(i, e))

for i, comp in enumerate(comps):
    try:
        comp.generate_local(True)
    except Exception:
        try:
            comp.generate(True)
        except Exception as e:
            unreal.log_warning("[PCG-PROBE-ASYNC] generate failed on {}: {}".format(i, e))

_state["components"] = comps
_state["callback"] = unreal.register_slate_pre_tick_callback(_tick)
unreal.log("[PCG-PROBE-ASYNC] registered tick callback, non-blocking wait started")
