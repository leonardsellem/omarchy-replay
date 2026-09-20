#!/usr/bin/env python3
"""Bounded local OCR comparison. Public reports contain no recognized screen text.

The controller keeps worker text in memory solely for numeric quality comparison.
No screenshots or text are uploaded, and existing history is opened read-only.
"""
import argparse
import collections
import ctypes
import ctypes.util
import gc
import hashlib
import importlib.metadata
import json
import os
from pathlib import Path
import platform
import re
import resource
import selectors
import signal
import sqlite3
import statistics
import subprocess
import sys
import tempfile
import time


MODELS = {
    "en_PP-OCRv3_det_mobile.onnx": "ea07c15d38ac40cd69da3c493444ec75b44ff23840553ff8ba102c1219ed39c2",
    "en_PP-OCRv4_rec_mobile.onnx": "e8770c967605983d1570cdf5352041dfb68fa0c21664f49f47b155abd3e0e318",
    "ch_ppocr_mobile_v2.0_cls_mobile.onnx": "e47acedf663230f8863ff1ab0e64dd2d82b838fceb5957146dab185a89d6215c",
}
ENGINES = ("tesseract-auto", "tesseract-sparse", "rapidocr-mobile", "rapidocr-det-auto")


def digest(path):
    with open(path, "rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def memory(pid):
    result = {}
    try:
        for line in Path(f"/proc/{pid}/smaps_rollup").read_text().splitlines():
            key, _, value = line.partition(":")
            if key in ("Rss", "Pss"):
                result[key.lower() + "_mib"] = int(value.split()[0]) / 1024
        result["threads"] = len(list(Path(f"/proc/{pid}/task").iterdir()))
    except (OSError, ValueError, IndexError):
        pass
    return result


def trim():
    gc.collect()
    libc = ctypes.CDLL(None)
    if hasattr(libc, "malloc_trim"):
        libc.malloc_trim(0)


class Tesseract:
    def __init__(self, options):
        lib = ctypes.CDLL(ctypes.util.find_library("tesseract"))
        self.lib = lib
        pointer, integer = ctypes.c_void_p, ctypes.c_int
        signatures = {
            "TessVersion": (ctypes.c_char_p, []),
            "TessBaseAPICreate": (pointer, []),
            "TessBaseAPIInit2": (integer, [pointer, ctypes.c_char_p, ctypes.c_char_p, integer]),
            "TessBaseAPISetPageSegMode": (None, [pointer, integer]),
            "TessBaseAPISetImage": (None, [pointer, ctypes.c_char_p, integer, integer, integer, integer]),
            "TessBaseAPIRecognize": (integer, [pointer, pointer]),
            "TessBaseAPIGetUTF8Text": (pointer, [pointer]),
            "TessDeleteText": (None, [pointer]),
            "TessBaseAPIClear": (None, [pointer]),
            "TessBaseAPIDelete": (None, [pointer]),
            "TessBaseAPIGetIterator": (pointer, [pointer]),
            "TessResultIteratorGetPageIterator": (pointer, [pointer]),
            "TessResultIteratorNext": (integer, [pointer, integer]),
            "TessResultIteratorDelete": (None, [pointer]),
            "TessPageIteratorBoundingBox": (integer, [pointer, integer] + [ctypes.POINTER(integer)] * 4),
        }
        for name, (restype, argtypes) in signatures.items():
            function = getattr(lib, name)
            function.restype, function.argtypes = restype, argtypes
        self.api = lib.TessBaseAPICreate()
        if lib.TessBaseAPIInit2(self.api, os.fsencode(options.tessdata), b"eng", 1):
            raise RuntimeError("Tesseract initialization failed")
        self.psm = 3 if options.engine == "tesseract-auto" else 11
        lib.TessBaseAPISetPageSegMode(self.api, self.psm)
        self.description = {"runtime": lib.TessVersion().decode(), "page_segmentation_mode": self.psm,
                            "ocr_engine_mode": 1, "upstream_input_resize": False}

    def recognize(self, image):
        width, height = image.size
        pixels = image.tobytes()
        self.lib.TessBaseAPISetImage(self.api, pixels, width, height, 4, width * 4)
        if self.lib.TessBaseAPIRecognize(self.api, None):
            raise RuntimeError("Tesseract recognition failed")
        pointer = self.lib.TessBaseAPIGetUTF8Text(self.api)
        text = ctypes.string_at(pointer).decode("utf-8", "replace") if pointer else ""
        if pointer:
            self.lib.TessDeleteText(pointer)
        boxes = []
        iterator = self.lib.TessBaseAPIGetIterator(self.api)
        if iterator:
            page = self.lib.TessResultIteratorGetPageIterator(iterator)
            while True:
                bounds = [ctypes.c_int() for _ in range(4)]
                if self.lib.TessPageIteratorBoundingBox(page, 2, *(ctypes.byref(x) for x in bounds)):
                    boxes.append([x.value for x in bounds])
                if not self.lib.TessResultIteratorNext(iterator, 2):
                    break
            self.lib.TessResultIteratorDelete(iterator)
        self.lib.TessBaseAPIClear(self.api)
        return text, boxes, {}

    def close(self):
        self.lib.TessBaseAPIDelete(self.api)


class Rapid:
    def __init__(self, options):
        import cv2
        import numpy as np
        from rapidocr import RapidOCR, LangDet, LangRec, ModelType, OCRVersion
        cv2.setNumThreads(1)
        self.np = np
        self.detector_shape = None
        directory = options.models
        self.detector_limit = 2000 if options.engine == "rapidocr-det-auto" else None
        self.engine = RapidOCR(params={
            "Global.log_level": "critical", "Global.use_cls": False,
            "Global.use_preprocess_img": False, "Global.use_vertical_padding": False,
            "Global.model_root_dir": str(directory),
            "Det.lang_type": LangDet.EN, "Rec.lang_type": LangRec.EN,
            "Det.model_type": ModelType.MOBILE, "Rec.model_type": ModelType.MOBILE,
            "Det.ocr_version": OCRVersion.PPOCRV4, "Rec.ocr_version": OCRVersion.PPOCRV4,
            "Det.model_path": str(directory / "en_PP-OCRv3_det_mobile.onnx"),
            "Rec.model_path": str(directory / "en_PP-OCRv4_rec_mobile.onnx"),
            "Cls.model_path": str(directory / "ch_ppocr_mobile_v2.0_cls_mobile.onnx"),
            # min30 preserves screen size. Upstream max mode chooses its own
            # 960/1500/2000 limit; record the actual inference tensor below.
            "Det.limit_type": "max" if self.detector_limit else "min",
            "Det.limit_side_len": self.detector_limit or 30,
            "EngineConfig.onnxruntime.intra_op_num_threads": 1,
            "EngineConfig.onnxruntime.inter_op_num_threads": 1,
            "EngineConfig.onnxruntime.enable_cpu_mem_arena": False,
        })
        sessions = [component.session.session for component in
                    (self.engine.text_det, self.engine.text_rec, self.engine.text_cls)]
        assert all(s.get_providers() == ["CPUExecutionProvider"] for s in sessions)
        assert all(s.get_session_options().intra_op_num_threads == 1 and
                   s.get_session_options().inter_op_num_threads == 1 for s in sessions)
        self.description = {
            "runtime": importlib.metadata.version("onnxruntime"),
            "wrapper": importlib.metadata.version("rapidocr"),
            "upstream_input_resize": False, "detector_alignment": 32,
            "detector_policy": "upstream-adaptive-max" if self.detector_limit else "original-size-min30",
            "detector_max_side": self.detector_limit, "recognition_crops_from_original": True,
            "recognition_crop_height": 48, "classification_enabled": False,
            "classification_model_initialized": True, "cpu_memory_arena": False,
            "intra_threads": 1, "inter_threads": 1, "opencv_threads": cv2.getNumThreads(),
            "providers": sessions[0].get_providers(),
        }
        detector_session = self.engine.text_det.session
        def record_detector_shape(tensor):
            self.detector_shape = list(tensor.shape)
            return detector_session(tensor)
        self.engine.text_det.session = record_detector_shape

    def recognize(self, image):
        pixels = self.np.asarray(image.convert("RGB"))[:, :, ::-1].copy()
        result = self.engine(pixels, use_cls=False)
        text = "\n".join(getattr(result, "txts", None) or ())
        boxes = []
        if result.boxes is not None:
            for polygon in result.boxes:
                boxes.append([float(polygon[:, 0].min()), float(polygon[:, 1].min()),
                              float(polygon[:, 0].max()), float(polygon[:, 1].max())])
        times = getattr(result, "elapse_list", None) or []
        return text, boxes, {"stage_wall_ms": [None if x is None else float(x) * 1000 for x in times],
            "detector_tensor_shape": self.detector_shape}

    def close(self):
        del self.engine


def emit(value):
    # Worker stdout is a private controller pipe, never a report/log file.
    print(json.dumps(value, separators=(",", ":")), flush=True)


def worker(options):
    resource.setrlimit(resource.RLIMIT_AS, (options.memory_mib * 1024 * 1024,) * 2)
    if options.affinity_cpu is not None:
        os.sched_setaffinity(0, {options.affinity_cpu})
    from PIL import Image
    jobs = json.loads(options.manifest.read_text())
    before = memory(os.getpid())
    wall, cpu = time.perf_counter(), time.process_time()
    engine = Rapid(options) if options.engine.startswith("rapidocr-") else Tesseract(options)
    init = {"wall_ms": (time.perf_counter() - wall) * 1000,
            "cpu_ms": (time.process_time() - cpu) * 1000,
            "worker_script_sha256": digest(Path(__file__)), "before": before, "after": memory(os.getpid()), "description": engine.description}
    emit({"event": "init", **init})
    for index, job in enumerate(jobs):
        emit({"event": "begin", "job": index})
        wall, cpu = time.perf_counter(), time.process_time()
        with Image.open(job["path"]) as source:
            image = source.convert("RGBA")
        if image.size != (job["width"], job["height"]):
            raise RuntimeError("Unexpected image dimensions")
        decode = {"wall_ms": (time.perf_counter() - wall) * 1000,
                  "cpu_ms": (time.process_time() - cpu) * 1000}
        wall, cpu = time.perf_counter(), time.process_time()
        text, boxes, stages = engine.recognize(image)
        measurement = {"event": "job", "job": index, "decode": decode,
            "wall_ms": (time.perf_counter() - wall) * 1000,
            "cpu_ms": (time.process_time() - cpu) * 1000,
            "line_count": len(boxes), "invalid_boxes": sum(
                not (0 <= x1 < x2 <= image.width and 0 <= y1 < y2 <= image.height)
                for x1, y1, x2, y2 in boxes), **stages, "text": text}
        if len(text) > 1024 * 1024:
            raise RuntimeError("Recognized text limit exceeded")
        emit(measurement)
        del image, text, boxes, measurement
    gc.collect()
    before_trim = memory(os.getpid())
    trim()
    cpu = time.process_time()
    time.sleep(0.5)
    emit({"event": "idle", "before_trim": before_trim, "after_trim": memory(os.getpid()),
          "idle_cpu_ms_over_500ms": (time.process_time() - cpu) * 1000,
          "peak_rss_mib": resource.getrusage(resource.RUSAGE_SELF).ru_maxrss / 1024})
    engine.close()


def measured_worker(options, manifest, engine):
    command = [sys.executable, str(Path(__file__).resolve()), "--worker", "--engine", engine,
        "--manifest", str(manifest), "--models", str(options.models), "--tessdata", str(options.tessdata),
        "--memory-mib", str(options.memory_mib)]
    if options.affinity_cpu is not None:
        command += ["--affinity-cpu", str(options.affinity_cpu)]
    environment = dict(os.environ, OMP_THREAD_LIMIT="1", OMP_NUM_THREADS="1", OPENBLAS_NUM_THREADS="1",
                       MKL_NUM_THREADS="1", NUMEXPR_NUM_THREADS="1")
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                               env=environment, start_new_session=True)
    selector = selectors.DefaultSelector()
    os.set_blocking(process.stdout.fileno(), False)
    selector.register(process.stdout, selectors.EVENT_READ)
    report = {"engine": engine, "jobs": [], "peak": {}, "memory_samples": 0}
    texts = []
    pending = bytearray()
    job = None
    peaks = {}
    started = time.monotonic()
    try:
        while process.poll() is None or selector.get_map():
            if time.monotonic() - started > options.timeout:
                raise TimeoutError("Candidate exceeded bounded runtime")
            sample = memory(process.pid)
            if sample:
                report["memory_samples"] += 1
                for key, value in sample.items():
                    report["peak"][key] = max(value, report["peak"].get(key, 0))
                    if job is not None:
                        current = peaks.setdefault(job, {})
                        current[key] = max(value, current.get(key, 0))
            for key, _ in selector.select(0.05):
                data = key.fileobj.read()
                if data == b"":
                    selector.unregister(key.fileobj)
                    continue
                if not data:
                    continue
                pending.extend(data)
                if len(pending) > 4 * 1024 * 1024:
                    raise RuntimeError("Worker message limit exceeded")
                while b"\n" in pending:
                    line, _, rest = pending.partition(b"\n")
                    pending[:] = rest
                    message = json.loads(line)
                    event = message.pop("event")
                    if event == "begin":
                        job = message["job"]
                    elif event == "job":
                        texts.append(message.pop("text"))
                        message["sampled_peak"] = peaks.get(job, {})
                        report["jobs"].append(message)
                        job = None
                    else:
                        report[event] = message
        if process.wait() or "idle" not in report:
            raise RuntimeError("Candidate failed; private diagnostics suppressed")
        report["process_wall_ms"] = (time.monotonic() - started) * 1000
        return report, texts
    except Exception as error:
        report.setdefault("failure", {"type": type(error).__name__})
        report["exit_code"] = process.poll()
        report["last_started_job"] = job
        save(options.output.with_name(options.output.stem + "-failed-" + engine + ".json"), report)
        raise
    finally:
        if process.poll() is None:
            os.killpg(process.pid, signal.SIGTERM)
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
        selector.close()
        process.stdout.close()


def exact(text, term):
    boundary = r"[\w-]" if "-" in term else r"\w"
    return bool(re.search(r"(?<!" + boundary + ")" + re.escape(term) + r"(?!" + boundary + ")", text, re.IGNORECASE))


def agreement(baseline, candidate):
    a = collections.Counter(re.findall(r"\w+", baseline.casefold()))
    b = collections.Counter(re.findall(r"\w+", candidate.casefold()))
    common = sum((a & b).values())
    return {"baseline_tokens": sum(a.values()), "candidate_tokens": sum(b.values()), "common_tokens": common,
            "baseline_token_recall": common / sum(a.values()) if a else None,
            "candidate_token_precision": common / sum(b.values()) if b else None}


def load_jobs(options):
    jobs = []
    if options.manifest:
        for entry in json.loads(options.manifest.read_text()):
            path = (options.manifest.parent / entry["path"]).resolve()
            if not path.is_relative_to(options.manifest.parent.resolve()):
                raise ValueError("Fixture path escapes its directory")
            jobs.append({**entry, "path": str(path), "private": False})
    if options.private_dir:
        root = options.private_dir.resolve()
        connection = sqlite3.connect((root / "index.sqlite").as_uri() + "?mode=ro", uri=True)
        try:
            rows = connection.execute("SELECT source_path,width,height FROM frames WHERE source_path<>'' ORDER BY timestamp_ms,id").fetchall()
        finally:
            connection.close()
        if len(rows) != 8:
            raise ValueError("Authorized private corpus must contain exactly eight retained originals")
        for index, (relative, width, height) in enumerate(rows):
            path = (root / relative).resolve()
            if not path.is_relative_to(root):
                raise ValueError("Private source path escapes authorized copied dataset")
            jobs.append({"path": str(path), "suite": "private4k", "source_index": index,
                         "width": width, "height": height, "expected": [], "private": True})
    if not jobs:
        raise ValueError("Provide a synthetic manifest and/or authorized copied private corpus")
    for job in jobs:
        path = Path(job["path"])
        if not 0 < path.stat().st_size <= 128 * 1024 * 1024 or not 0 < job["width"] * job["height"] <= 32 * 1024 * 1024:
            raise ValueError("Input size limit exceeded")
        job["sha256"] = digest(path)
    return jobs


def summarize(jobs, report):
    suites = {}
    for suite in sorted({job["suite"] for job in jobs}):
        selected = [result for job, result in zip(jobs, report["jobs"]) if job["suite"] == suite]
        result = {"images": len(selected), "ocr_cpu_ms_total": sum(x["cpu_ms"] for x in selected),
                  "ocr_wall_ms_total": sum(x["wall_ms"] for x in selected),
                  "ocr_cpu_ms_median": statistics.median(x["cpu_ms"] for x in selected),
                  "ocr_wall_ms_median": statistics.median(x["wall_ms"] for x in selected),
                  "invalid_boxes": sum(x["invalid_boxes"] for x in selected)}
        for key in ("expected", "found", "unexpected", "lost_baseline", "gained_baseline"):
            result[key + "_identifiers"] = sum(x.get("quality", {}).get(key, 0) for x in selected)
        agreements = [x["agreement"] for x in selected]
        for key in ("baseline_tokens", "candidate_tokens", "common_tokens"):
            result[key] = sum(x[key] for x in agreements)
        if result["baseline_tokens"]:
            result["baseline_token_recall"] = result["common_tokens"] / result["baseline_tokens"]
        if result["candidate_tokens"]:
            result["candidate_token_precision"] = result["common_tokens"] / result["candidate_tokens"]
        suites[suite] = result
    return suites


def save(path, report):
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(report, indent=2) + "\n")
    temporary.chmod(0o600)
    temporary.replace(path)


def main(options):
    if options.output.exists():
        raise FileExistsError("Refusing to overwrite an earlier experiment")
    jobs = load_jobs(options)
    verified = []
    for filename, expected in MODELS.items():
        path = options.models / filename
        actual = digest(path)
        if actual != expected:
            raise ValueError("Model checksum differs from pinned upstream manifest")
        verified.append({"name": filename, "sha256": actual, "bytes": path.stat().st_size})
    system_model = options.tessdata / "eng.traineddata"
    baseline_model = {"sha256": digest(system_model), "bytes": system_model.stat().st_size}
    options.output.parent.mkdir(parents=True, exist_ok=True)
    cpu_model = next((line.partition(":")[2].strip() for line in Path("/proc/cpuinfo").read_text().splitlines()
                      if line.startswith("model name")), "unknown")
    report = {"completed": False, "hardware": {"cpu_model": cpu_model, "architecture": platform.machine(),
        "logical_cpus": os.cpu_count(), "available_cpus": sorted(os.sched_getaffinity(0)), "affinity_cpu": options.affinity_cpu},
        "python": platform.python_version(), "packages": {d.metadata["Name"]: d.version for d in importlib.metadata.distributions()},
        "models": verified, "system_tesseract_model": baseline_model, "memory_limit_mib": options.memory_mib,
        "script_sha256": digest(Path(__file__)),
        "inputs": [{k: v for k, v in job.items() if k not in ("path", "expected")} for job in jobs],
        "rounds": [], "limits": [
            "All images enter at original resolution. Adaptive-max detection downsizes the detector input; recognition uses original-image crops normalized by the model. Detector dimensions align to 32.",
            "Fresh process per engine/round; initialized model is reused across images. Initialization measured separately.",
            "CPU work is unpaced; inferred budget capacity is not sustained capture/worker proof.",
            "Private token agreement with Tesseract is not ground-truth accuracy; recognized text remains in controller memory only.",
            "Peak PSS is sampled at intervals up to 50ms and can miss spikes. Kernel peak RSS is also recorded.",
            "Python wrapper memory is part of this experiment, not a forecast of native integrated-engine memory.",
            "This is one host; page-cache, clock and external workload effects remain."]}
    universe = {term for job in jobs if not job["private"] for term in job["expected"]}
    save(options.output, report)
    with tempfile.TemporaryDirectory(prefix="ocr-manifest-", dir=options.output.parent) as directory:
        manifest = Path(directory) / "jobs.json"
        manifest.write_text(json.dumps(jobs)); manifest.chmod(0o600)
        baseline = None
        for round_index in range(options.rounds):
            order = [engine for engine in options.engines if not (round_index and options.repeat_candidates_only and engine.startswith("tesseract-"))]
            if round_index % 2:
                order.reverse()
            measured = {}; texts = {}
            for engine in order:
                print(f"Round {round_index + 1}/{options.rounds}: {engine} ({len(jobs)} images)", flush=True)
                measured[engine], texts[engine] = measured_worker(options, manifest, engine)
                save(options.output.with_name(options.output.stem + f"-round{round_index + 1}-" + engine + ".json"), measured[engine])
            if "tesseract-auto" in texts:
                baseline = texts["tesseract-auto"]
            for engine, result in measured.items():
                for index, job in enumerate(jobs):
                    value = result["jobs"][index]
                    value["agreement"] = agreement(baseline[index], texts[engine][index])
                    if not job["private"]:
                        expected = set(job["expected"])
                        value["quality"] = {"expected": len(expected),
                            "found": sum(exact(texts[engine][index], term) for term in expected),
                            "unexpected": sum(exact(texts[engine][index], term) for term in universe - expected),
                            "lost_baseline": sum(exact(baseline[index], term) and not exact(texts[engine][index], term) for term in expected),
                            "gained_baseline": sum(not exact(baseline[index], term) and exact(texts[engine][index], term) for term in expected)}
                result["suites"] = summarize(jobs, result)
            report["rounds"].append({"order": order, "engines": measured})
            save(options.output, report)
            texts.clear()
    report["inputs_unchanged"] = all(digest(job["path"]) == job["sha256"] for job in jobs)
    report["system_model_unchanged"] = digest(system_model) == baseline_model["sha256"]
    report["completed"] = True
    save(options.output, report)
    print(json.dumps({"completed": True, "rounds": len(report["rounds"]), "images_per_engine": len(jobs),
                      "inputs_unchanged": report["inputs_unchanged"], "output": str(options.output)}))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--private-dir", type=Path)
    parser.add_argument("--models", type=Path, required=True)
    parser.add_argument("--tessdata", type=Path, default=Path("/usr/share/tessdata"))
    parser.add_argument("--output", type=Path)
    parser.add_argument("--rounds", type=int, default=2)
    parser.add_argument("--repeat-candidates-only", action="store_true", help="Reuse round-one Tesseract text in memory; repeat only neural candidates")
    parser.add_argument("--timeout", type=int, default=300)
    parser.add_argument("--memory-mib", type=int, default=3072, help="Per-worker address-space ceiling (RLIMIT_AS), not an RSS limit")
    parser.add_argument("--affinity-cpu", type=int)
    parser.add_argument("--engines", choices=ENGINES, nargs="+",
                        default=["tesseract-auto", "tesseract-sparse", "rapidocr-det-auto"])
    parser.add_argument("--worker", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--engine", choices=ENGINES, help=argparse.SUPPRESS)
    args = parser.parse_args()
    if not 1 <= args.rounds <= 4 or not 1 <= args.timeout <= 1200 or not 256 <= args.memory_mib <= 8192:
        parser.error("Use 1–4 rounds, 1–1200s timeout, 256–8192MiB worker memory bound")
    if args.repeat_candidates_only and not any(x.startswith("rapidocr-") for x in args.engines):
        parser.error("Candidate-only repeats require at least one RapidOCR engine")
    if args.affinity_cpu is not None and args.affinity_cpu not in os.sched_getaffinity(0):
        parser.error("Selected CPU is outside this process's allowed affinity")
    try:
        if args.worker:
            if not args.manifest or not args.engine:
                parser.error("Worker requires a manifest and engine")
            worker(args)
        else:
            if not args.output or "tesseract-auto" not in args.engines or len(args.engines) != len(set(args.engines)):
                parser.error("Provide an output and unique engines including tesseract-auto")
            main(args)
    except Exception as error:
        # Never include library exception text: it can contain input paths/text.
        if args.worker:
            emit({"event": "failure", "type": type(error).__name__})
        print(f"OCR comparison failed ({type(error).__name__}); private diagnostics suppressed", file=sys.stderr)
        sys.exit(1)
