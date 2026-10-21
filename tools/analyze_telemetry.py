#!/usr/bin/env python3
"""Summarize independent DistantStreaming CSV logs (standard library only)."""
import argparse
import csv
import pathlib
import statistics

def numbers(rows, key):
    return [float(r[key]) for r in rows if r.get(key) not in ("", None)]
def measure(rows, key, op, default=0):
    values=numbers(rows,key)
    return round(op(values),2) if values else default
def summarize(path):
    with path.open(encoding="utf-8", newline="") as f:
        rows=list(csv.DictReader(f))
    if not rows:
        return {"file":str(path),"samples":0}
    return {
        "file":str(path),
        "samples":len(rows),
        "avg_window_p99_ms":measure(rows,"frame_p99_ms",statistics.mean),
        "worst_window_p99_ms":measure(rows,"frame_p99_ms",max),
        "peak_private_mib":measure(rows,"private_mib",max),
        "peak_stream_mib":measure(rows,"stream_mib",max),
        "peak_stream_budget_mib":measure(rows,"stream_budget_mib",max),
        "min_virtual_available_mib":measure(rows,"virtual_available_mib",min),
        "address_guard_samples":int(measure(rows,"address_space_guard",sum)),
        "issued_speculative_requests":int(measure(rows,"issued",sum)),
        "total_load_calls":int(measure(rows,"load_calls",sum)),
        "archive_chooser_calls":int(measure(rows,"archive_calls",sum)),
        "archive_samples":int(measure(rows,"archive_samples",sum)),
        "archive_peak_us":measure(rows,"archive_peak_us",max),
        "archive_slow_samples":int(measure(rows,"archive_slow_samples",sum)),
        "dynamic_warm_requests":int(measure(rows,"dynamic_issued",sum)),
        "dynamic_deferred":int(measure(rows,"dynamic_deferred",sum)),
        "max_load_call_us":measure(rows,"load_peak_us",max),
        "vanilla_models_ready":int(measure(rows,"ready",sum)),
        "worst_window_readiness_p95_ms":measure(rows,"readiness_p95_ms",max),
        "worst_window_queue_p95_ms":measure(rows,"queue_p95_ms",max),
        "world_priority_requests":int(measure(rows,"world_priority_issued",sum)),
        "camera_priority_requests":int(measure(rows,"camera_requests",sum)),
        "camera_blocking_flushes":int(measure(rows,"camera_flushes",sum)),
        "camera_blocking_total_ms":round(measure(rows,"camera_block_total_us",sum)/1000,2),
        "camera_blocking_peak_ms":round(measure(rows,"camera_block_peak_us",max)/1000,2),
        "camera_render_instances":int(measure(rows,"camera_instances",sum)),
        "cleanup_render_instances":int(measure(rows,"cleanup_instances",sum)),
        "cleanup_models":int(measure(rows,"cleanup_models",sum)),
        "cleanup_textures":int(measure(rows,"cleanup_textures",sum)),
        "cleanup_stream_accounted_mib":round(measure(rows,"cleanup_stream_bytes",sum)/1048576,2),
        "cleanup_peak_ms":round(measure(rows,"cleanup_peak_us",max)/1000,2),
        "cleanup_budget_stops":int(measure(rows,"cleanup_budget_stops",sum)),
        "peak_readiness_pending":int(measure(rows,"readiness_pending",max)),
        "vehicle_models_ready":int(measure(rows,"ready_vehicle",sum)),
        "ped_models_ready":int(measure(rows,"ready_ped",sum)),
        "lost_hook_events":int(measure(rows,"lost_events",sum)),
        "coalesced_requests":int(measure(rows,"coalesced_requests",sum)),
        "ignored_resource_requests":int(measure(rows,"ignored_resource_requests",sum)),
        "dropped_log_lines":measure(rows,"dropped_log_lines",max),
    }
def main():
    p=argparse.ArgumentParser()
    p.add_argument("csv",type=pathlib.Path,nargs="+")
    args=p.parse_args()
    for path in args.csv:
        print(summarize(path))
    print("Warning: per-window p99 means are not global frame-time p99.")
    print("Latency state transitions are sampled at the scheduler cadence.")
if __name__=="__main__":
    main()
