import unreal

job = getattr(unreal, "_pcg_oneclick_job", None)
if not job:
    unreal.log("[PCG-ONECLICK] No running job.")
else:
    unreal.log_warning("[PCG-ONECLICK] Stopping running job...")
    try:
        job.finalize("manual_stop")
    except Exception as e:
        unreal.log_error("[PCG-ONECLICK] Stop failed: {}".format(e))
