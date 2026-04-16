import time
import unreal

MAX_WAIT_SECONDS = 180.0
POLL_SECONDS = 0.5
STABLE_TICKS_REQUIRED = 8

editor_subsystem = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
world = editor_subsystem.get_editor_world() if editor_subsystem else None
if not world:
    unreal.log_error('[PCG-ONE] No editor world.')
    raise SystemExit(2)

unreal.SystemLibrary.execute_console_command(world, 'PCGProfiler.ResetRun')

actors = actor_subsystem.get_all_level_actors() if actor_subsystem else []
components = []
for actor in actors:
    try:
        comps = actor.get_components_by_class(unreal.PCGComponent)
        if comps:
            components.extend(comps)
    except Exception:
        pass

unreal.log('[PCG-ONE] components={}'.format(len(components)))

for c in components:
    try:
        c.cleanup_local(True)
    except Exception:
        pass

for c in components:
    try:
        c.generate_local(True)
    except Exception:
        try:
            c.generate(True)
        except Exception:
            pass

stable_ticks = 0
start = time.time()
while True:
    active = 0
    dirty = 0
    has_state_api = False
    for c in components:
        gen = None
        for n in ['is_generation_in_progress', 'is_refresh_in_progress', 'is_generating']:
            fn = getattr(c, n, None)
            if callable(fn):
                try:
                    gen = bool(fn())
                    has_state_api = True
                    break
                except Exception:
                    pass
        d = None
        for n in ['is_dirty', 'is_component_dirty']:
            fn = getattr(c, n, None)
            if callable(fn):
                try:
                    d = bool(fn())
                    has_state_api = True
                    break
                except Exception:
                    pass
        if gen:
            active += 1
        if d:
            dirty += 1

    if has_state_api and active == 0 and dirty == 0:
        stable_ticks += 1
    elif has_state_api:
        stable_ticks = 0

    elapsed = time.time() - start
    if has_state_api and stable_ticks >= STABLE_TICKS_REQUIRED:
        unreal.log('[PCG-ONE] wait done elapsed={:.1f}s'.format(elapsed))
        break
    if elapsed >= MAX_WAIT_SECONDS:
        unreal.log_warning('[PCG-ONE] wait timeout elapsed={:.1f}s active={} dirty={}'.format(elapsed, active, dirty))
        break
    time.sleep(POLL_SECONDS)

unreal.SystemLibrary.execute_console_command(world, 'PCGProfiler.EndRun')
unreal.SystemLibrary.execute_console_command(world, 'PCGProfiler.ExportJson')
unreal.log('[PCG-ONE] end/export done')

