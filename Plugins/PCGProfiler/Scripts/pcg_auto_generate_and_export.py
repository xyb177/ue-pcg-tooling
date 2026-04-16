import time
import unreal

world = unreal.EditorLevelLibrary.get_editor_world()
if not world:
    unreal.log_error("[PCG-AUTO] No editor world.")
    raise SystemExit(2)

actors = unreal.EditorLevelLibrary.get_all_level_actors()
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

for i, comp in enumerate(pcg_components):
    try:
        comp.cleanup_local(True)
    except Exception as e:
        unreal.log_warning("[PCG-AUTO] cleanup_local failed on {}: {}".format(i, e))

for i, comp in enumerate(pcg_components):
    try:
        comp.generate_local(True)
    except Exception as e:
        unreal.log_warning("[PCG-AUTO] generate_local failed on {}: {}".format(i, e))

time.sleep(30)
unreal.SystemLibrary.execute_console_command(world, "PCGProfiler.ResetRun")
unreal.SystemLibrary.execute_console_command(world, "PCGProfiler.EndRun")
unreal.SystemLibrary.execute_console_command(world, "PCGProfiler.ExportJson")
unreal.log("[PCG-AUTO] Finished: cleanup -> generate -> wait(30s) -> reset -> end -> export")
