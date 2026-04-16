import unreal

editor_subsystem = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = editor_subsystem.get_editor_world() if editor_subsystem else None
if not world:
    unreal.log_error("[PCG-AUTO] No editor world.")
    raise SystemExit(2)

unreal.SystemLibrary.execute_console_command(world, "PCGProfiler.EndRun")
unreal.SystemLibrary.execute_console_command(world, "PCGProfiler.ExportJson")
unreal.log("[PCG-AUTO] Finish run: end -> export")
