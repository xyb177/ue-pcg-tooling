import os
import time
import unreal

MAX_WAIT_SECONDS = 600.0
POLL_SECONDS = 0.5
IDLE_STABLE_TICKS_REQUIRED = 3
STABLE_TICKS_REQUIRED = 6
NO_STATE_API_GRACE_SECONDS = 25.0
NO_STATE_API_QUIET_SECONDS = 8.0
OUT_DIR = r"F:/Unreal Projects/ElectricDreamsEnv/Saved/Profiling/PCG"


def log(msg):
    unreal.log("[PCG-ONECLICK] {}".format(msg))


def warn(msg):
    unreal.log_warning("[PCG-ONECLICK] {}".format(msg))


def err(msg):
    unreal.log_error("[PCG-ONECLICK] {}".format(msg))


def exec_cmd(world, cmd):
    unreal.SystemLibrary.execute_console_command(world, cmd)
    log("Cmd: {}".format(cmd))


def call_first(obj, names, default=False):
    for n in names:
        fn = getattr(obj, n, None)
        if callable(fn):
            try:
                return bool(fn()), n
            except Exception:
                pass
    return default, None


def get_profiler_subsystem():
    cls = getattr(unreal, "PCGProfilerSubsystem", None)
    if not cls:
        return None
    try:
        return unreal.get_engine_subsystem(cls)
    except Exception:
        return None


def collect_pcg_components(actor_subsystem):
    actors = actor_subsystem.get_all_level_actors()
    comps = []
    for actor in actors:
        try:
            found = actor.get_components_by_class(unreal.PCGComponent)
            if found:
                comps.extend(found)
        except Exception:
            pass
    return comps


class _PCGOneClickJob:
    def __init__(self):
        self.editor_subsystem = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
        self.actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        self.profiler_subsystem = get_profiler_subsystem()
        self.world = self.editor_subsystem.get_editor_world() if self.editor_subsystem else None

        self.started_at = time.time()
        self.last_poll = 0.0
        self.last_log = 0.0

        self.idle_stable_ticks = 0
        self.state_stable_ticks = 0
        self.state_api_seen = False

        self.done = False
        self.tick_handle = None

        self.components = []
        self.last_component_count = -1
        self.last_component_count_change = self.started_at

    def start(self):
        if not self.editor_subsystem or not self.actor_subsystem:
            err("Editor subsystem unavailable")
            return False
        if not self.world:
            err("No editor world")
            return False

        os.makedirs(OUT_DIR, exist_ok=True)

        self.components = collect_pcg_components(self.actor_subsystem)
        self.last_component_count = len(self.components)
        self.last_component_count_change = time.time()

        log("Found PCG components: {}".format(len(self.components)))
        if not self.components:
            warn("No PCG components found in current level")

        exec_cmd(self.world, "PCGProfiler.StartRun")

        clean_ok = 0
        for c in self.components:
            for name in ["cleanup_local", "cleanup", "cleanup_generated"]:
                fn = getattr(c, name, None)
                if callable(fn):
                    try:
                        fn(True)
                        clean_ok += 1
                        break
                    except Exception:
                        pass
        log("Cleanup triggered on {} components".format(clean_ok))

        gen_ok = 0
        for c in self.components:
            for name in ["generate_local", "generate", "refresh"]:
                fn = getattr(c, name, None)
                if callable(fn):
                    try:
                        fn(True)
                        gen_ok += 1
                        break
                    except Exception:
                        pass
        log("Generate triggered on {} components".format(gen_ok))

        if self.profiler_subsystem:
            log("C++ subsystem detected. Will poll IsRunIdle() on main thread.")
        else:
            warn("PCGProfilerSubsystem unavailable in Python; using fallback wait.")

        log("Async wait started. Editor remains responsive.")
        self.last_poll = time.time()
        self.last_log = time.time()
        return True

    def finalize(self, reason):
        if self.done:
            return
        self.done = True
        log("Finalize: {}".format(reason))
        exec_cmd(self.world, "PCGProfiler.EndRun")
        exec_cmd(self.world, "PCGProfiler.ExportJson")
        log("Done. JSON dir: {}".format(OUT_DIR))
        self._unregister_tick()
        try:
            setattr(unreal, "_pcg_oneclick_job", None)
        except Exception:
            pass

    def _unregister_tick(self):
        if self.tick_handle:
            try:
                unreal.unregister_slate_post_tick_callback(self.tick_handle)
            except Exception:
                pass
            self.tick_handle = None

    def on_tick(self, dt):
        if self.done:
            return

        now = time.time()
        elapsed = now - self.started_at

        if elapsed >= MAX_WAIT_SECONDS:
            warn("Timeout reached ({:.1f}s). Exporting current data.".format(elapsed))
            self.finalize("timeout")
            return

        if (now - self.last_poll) < POLL_SECONDS:
            return
        self.last_poll = now

        # Preferred path: C++ subsystem idle check on main thread.
        is_idle = None
        active_count = None
        if self.profiler_subsystem:
            try:
                is_idle = bool(self.profiler_subsystem.is_run_idle())
                active_count = int(self.profiler_subsystem.get_active_pcg_component_count())
            except Exception as e:
                warn("IsRunIdle/GetActivePCGComponentCount failed: {}".format(e))
                is_idle = None

            if is_idle is True:
                self.idle_stable_ticks += 1
            elif is_idle is False:
                self.idle_stable_ticks = 0

            if self.idle_stable_ticks >= IDLE_STABLE_TICKS_REQUIRED:
                self.finalize("stable_by_cpp_idle")
                return

        self.components = collect_pcg_components(self.actor_subsystem)
        comp_count = len(self.components)
        if comp_count != self.last_component_count:
            self.last_component_count = comp_count
            self.last_component_count_change = now

        active = 0
        dirty = 0
        for c in self.components:
            generating, g_api = call_first(c, ["is_generation_in_progress", "is_refresh_in_progress", "is_generating"], False)
            comp_dirty, d_api = call_first(c, ["is_dirty", "is_component_dirty"], False)
            if g_api or d_api:
                self.state_api_seen = True
            if generating:
                active += 1
            if comp_dirty:
                dirty += 1

        if self.state_api_seen and active == 0 and dirty == 0:
            self.state_stable_ticks += 1
        elif self.state_api_seen:
            self.state_stable_ticks = 0

        if (now - self.last_log) >= 5.0:
            log(
                "Waiting... elapsed={:.1f}s comps={} active={} dirty={} state_stable_ticks={} state_api_seen={} cpp_idle={} cpp_active={} cpp_idle_ticks={}".format(
                    elapsed,
                    comp_count,
                    active,
                    dirty,
                    self.state_stable_ticks,
                    self.state_api_seen,
                    is_idle,
                    active_count,
                    self.idle_stable_ticks,
                )
            )
            self.last_log = now

        if self.state_api_seen and self.state_stable_ticks >= STABLE_TICKS_REQUIRED:
            self.finalize("stable_by_state")
            return

        # Quiet fallback is only allowed when C++ subsystem is unavailable.
        # If subsystem exists, trust IsRunIdle/GetActivePCGComponentCount and do not early-finalize.
        if (self.profiler_subsystem is None) and (not self.state_api_seen) and elapsed >= NO_STATE_API_GRACE_SECONDS:
            quiet_for = now - self.last_component_count_change
            if quiet_for >= NO_STATE_API_QUIET_SECONDS:
                warn(
                    "No PCG state API in Python; fallback finalize after quiet period (quiet_for={:.1f}s, comps={}).".format(
                        quiet_for, comp_count
                    )
                )
                self.finalize("stable_by_quiet_fallback")


def _cancel_existing_job():
    job = getattr(unreal, "_pcg_oneclick_job", None)
    if job:
        warn("Found existing PCG one-click job; cancelling it first.")
        try:
            job.finalize("cancel_previous")
        except Exception:
            pass


def main():
    _cancel_existing_job()

    job = _PCGOneClickJob()
    if not job.start():
        return

    try:
        job.tick_handle = unreal.register_slate_post_tick_callback(job.on_tick)
    except Exception as e:
        err("Failed to register tick callback: {}".format(e))
        return

    setattr(unreal, "_pcg_oneclick_job", job)
    log("Registered tick callback: {}".format(job.tick_handle))


main()
