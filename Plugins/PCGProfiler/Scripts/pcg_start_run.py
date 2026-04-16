import unreal

editor_subsystem = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
world = editor_subsystem.get_editor_world() if editor_subsystem else None
if not world:
    unreal.log_error("[PCG-AUTO] No editor world.")
    raise SystemExit(2)

unreal.SystemLibrary.execute_console_command(world, "PCGProfiler.ResetRun")

actors = actor_subsystem.get_all_level_actors() if actor_subsystem else []
pcg_components = []
for actor in actors:
    try:
        comps = actor.get_components_by_class(unreal.PCGComponent)
        if comps:
            pcg_components.extend(comps)
    except Exception:
        pass

unreal.log("[PCG-AUTO] PCG components found: {}".format(len(pcg_components)))
if not pcg_components:
    unreal.log_error("[PCG-AUTO] No PCG components found.")
    raise SystemExit(2)


def _call(comp, name, *args):
    fn = getattr(comp, name, None)
    if callable(fn):
        try:
            return True, fn(*args)
        except Exception as e:
            unreal.log_warning("[PCG-AUTO] {} failed: {}".format(name, e))
            return True, None
    return False, None

cleanup_ok = 0
generate_ok = 0
task_ids = []

for comp in pcg_components:
    called, _ = _call(comp, "cleanup_local", True)
    if not called:
        _call(comp, "cleanup", True)
    cleanup_ok += 1

for comp in pcg_components:
    called_task, task_id = _call(comp, "generate_local_get_task_id", True)
    if called_task and task_id is not None:
        try:
            if int(task_id) >= 0:
                task_ids.append(int(task_id))
        except Exception:
            pass

    called_generate, _ = _call(comp, "generate_local", True)
    if not called_generate:
        _call(comp, "generate", True)

    _call(comp, "refresh", unreal.PCGChangeType.NONE, True)
    generate_ok += 1

unreal.log("[PCG-AUTO] Start run finished: reset -> cleanup({}) -> generate({}) -> task_ids={}".format(cleanup_ok, generate_ok, len(task_ids)))
if task_ids:
    unreal.log("[PCG-AUTO] task_ids sample: {}".format(task_ids[:8]))
