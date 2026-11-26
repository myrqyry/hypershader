#!/usr/bin/env python3
"""Independent-process real libobs audit of installed-style .effect filters.

Metrics are triage signals, not proof of quality. An unchanged frame may be
correct for a neutral filter; a flat frame may be intentional or input-dependent.
Each effect gets its own OBS/OpenGL process so compiler crashes do not erase
results from unrelated effects.
"""
import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
FLOAT_RE = r"[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?"
AUDIT_RE = re.compile(
    rf"SHADER_AUDIT samples=(\d+) mean_rgb=({FLOAT_RE}) delta_rgb=({FLOAT_RE})"
    rf" min_rgb=(\d+) max_rgb=(\d+) modified_frames=(\d+)"
    rf"(?: repeat_delta_rgb=({FLOAT_RE}))?"
)

def parse_audit_metrics(output):
    match = AUDIT_RE.search(output)
    if not match:
        return None
    samples, mean, delta, minimum, maximum, modified, repeat = match.groups()
    metrics = {
        "samples": int(samples),
        "mean_rgb": float(mean),
        "delta_rgb": float(delta),
        "min_rgb": int(minimum),
        "max_rgb": int(maximum),
        "modified_frames": int(modified),
    }
    if repeat is not None:
        metrics["repeat_delta_rgb"] = float(repeat)
    return metrics

def determinism_state(metrics):
    if not metrics or "repeat_delta_rgb" not in metrics:
        return "UNKNOWN"
    return "MATCH" if metrics["repeat_delta_rgb"] <= 0.05 else "DIVERGED"

def classify_metrics(metrics):
    if determinism_state(metrics) == "DIVERGED":
        return "POSSIBLE_NONDETERMINISTIC"
    if metrics["mean_rgb"] < 2:
        return "POSSIBLE_BLACK"
    if metrics["mean_rgb"] > 235:
        return "POSSIBLE_CLIPPED"
    if metrics["max_rgb"] - metrics["min_rgb"] < 12:
        return "POSSIBLE_FLAT"
    if metrics["delta_rgb"] < 1:
        return "NEUTRAL_OR_BYPASS"
    return "EFFECT_VISIBLE"


def inspect_effect(path, probe, module, data, logs):
    name = path.name
    command = ["xvfb-run", "-a", "-s", "-screen 0 1280x720x24",
               str(probe), str(module), str(data), str(path), "audit"]
    env = dict(os.environ)
    env["MESA_SHADER_CACHE_DISABLE"] = "true"
    env["LD_LIBRARY_PATH"] = str(Path.home()/".config/obs-studio/plugins/hypershader/bin/64bit") + \
                             ":" + env.get("LD_LIBRARY_PATH", "")
    begin = time.monotonic()
    try:
        result = subprocess.run(command, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True,
                                errors="replace", timeout=19, env=env)
        output = result.stdout
        rc = result.returncode
    except subprocess.TimeoutExpired as exc:
        raw=exc.stdout or b""
        output=raw.decode(errors="replace") if isinstance(raw, bytes) else raw
        rc=124
    log = logs / (path.stem + ".log")
    log.write_text(output)
    metrics = parse_audit_metrics(output)
    loaded = f"Loaded shader: shaders/root/{name}" in output
    report = {
        "name": name,
        "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        "exit": rc,
        "loaded": loaded,
        "seconds": round(time.monotonic()-begin, 2),
        "log": str(log),
    }
    if metrics:
        report["pixels"] = metrics
        report["determinism"] = determinism_state(metrics)
    if rc != 0:
        report["status"] = "CRASH_OR_TIMEOUT" if rc in (-11, 124, 139) else "PROCESS_FAILED"
    elif not loaded:
        report["status"] = "LOAD_FAILED"
    elif not metrics:
        report["status"] = "NO_PIXEL_READBACK"
        report["determinism"] = "UNKNOWN"
    else:
        report["status"] = classify_metrics(report["pixels"])
    return report

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--module", type=Path, default=ROOT/"build_x86_64/hypershader.so")
    parser.add_argument("--data", type=Path, default=ROOT/"data")
    parser.add_argument("--output", type=Path, default=Path("/tmp/hypershader-library-quality.json"))
    parser.add_argument("--jobs", type=int, default=2)
    args=parser.parse_args()
    files=sorted((args.data/"shaders/root").glob("*.effect"))
    if not files: raise SystemExit("No shader library found")
    logs=args.output.parent / (args.output.stem + "-logs")
    logs.mkdir(parents=True, exist_ok=True)
    probe=args.output.parent / (args.output.stem + "-probe")
    inc=subprocess.check_output(["pkg-config","--variable=includedir","libobs"],text=True).strip()
    libs=subprocess.check_output(["pkg-config","--libs","libobs"],text=True).split()
    subprocess.run(["g++","-std=c++17","-O2","-w",f"-I{inc}/obs",
                    str(ROOT/"scripts/verify-stdlib-loader.cpp"),
                    "-o",str(probe),*libs],check=True)
    rows=[]
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        jobs=[pool.submit(inspect_effect,p,probe,args.module,args.data,logs) for p in files]
        for job in concurrent.futures.as_completed(jobs):
            result=job.result()
            rows.append(result)
            print(f"{result['status']:20s} {result['name']:39s} "
                  f"{result.get('pixels',{}).get('mean_rgb','---')} "
             f"det={result.get('determinism','UNKNOWN')}",flush=True)
    rows.sort(key=lambda r:r["name"])
    counts={}
    for r in rows:counts[r["status"]]=counts.get(r["status"],0)+1
    args.output.write_text(json.dumps({"scope":"root/*.effect",
                                       "method":"real libobs filter / OpenGL Mesa",
                                       "caveat":"One fixture; output signals need human/source review. "
                                                "Determinism compares twin instances with frozen host time.",
                                       "totals":counts,"shaders":rows},indent=2)+"\n")
    print("AUDIT_SUMMARY",json.dumps(counts,sort_keys=True))
    print("AUDIT_JSON",args.output)
    return 0
if __name__=="__main__":
    sys.exit(main())
