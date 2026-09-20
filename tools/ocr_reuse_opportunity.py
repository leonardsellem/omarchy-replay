#!/usr/bin/env python3
"""Read-only, bounded exact-pixel opportunity counts; never runs OCR.

Whole-frame counts are upper bounds: historical rows lack the new full-OCR
provenance/profile, and a real cache can only contain verified complete donors.
Padded line-patch counts describe candidates for future work, not safe reuse:
they do not prove segmentation, reading order, deletion, or scroll coverage.
No pixels, text, paths, hashes, or original timestamps enter the report.
"""

import argparse
from collections import OrderedDict
from contextlib import closing
import hashlib
import json
from pathlib import Path
import resource
import sqlite3
import time

from PIL import Image


MAX_PIXELS = 32 * 1024 * 1024
MAX_LINE_PIXELS = 1024 * 1024
MAX_LINES = 8000
FULL_CACHE_LIMIT = 256
PATCH_CACHE_LIMIT = 20000
Image.MAX_IMAGE_PIXELS = MAX_PIXELS


def pixel_key(image, box=None):
    """Hash dimensions and exact RGBA rows without a second full raw buffer."""
    left, top, right, bottom = box or (0, 0, image.width, image.height)
    digest = hashlib.sha256(f"{right-left}x{bottom-top}".encode("ascii"))
    for y in range(top, bottom, 32):
        # Image.__exit__ does not release the native pixel buffer in Pillow.
        with closing(image.crop((left, y, right, min(y + 32, bottom)))) as stripe:
            digest.update(stripe.tobytes())
    return digest.digest()


def put_bounded(cache, key, value, bound):
    cache[key] = value
    cache.move_to_end(key)
    if len(cache) > bound:
        cache.popitem(last=False)


def read_image(directory, relative, width, height):
    if width <= 0 or height <= 0 or width * height > MAX_PIXELS:
        raise ValueError("source geometry exceeds the decode limit")
    path = (directory / relative).resolve(strict=True)
    roots = [(directory / name).resolve() for name in ("media", "staging")]
    if not any(path.is_relative_to(root) for root in roots):
        raise ValueError("source is outside the dataset media directories")
    if not 0 < path.stat().st_size <= width * height * 5 + 1024 * 1024:
        raise ValueError("compressed source exceeds the decode limit")
    with closing(Image.open(path)) as source:
        if source.format != "WEBP" or source.size != (width, height):
            raise ValueError("source format or dimensions disagree with the index")
        return source.convert("RGBA")


def line_boxes(serialized, width, height):
    if serialized is None:
        return []
    if len(serialized.encode("utf-8")) > 3 * 1024 * 1024:
        raise ValueError("stored geometry exceeds its per-frame bound")
    lines = json.loads(serialized)
    if not isinstance(lines, list) or len(lines) > MAX_LINES:
        raise ValueError("stored geometry exceeds its line bound")
    boxes = []
    for line in lines:
        if not isinstance(line, list) or len(line) != 5:
            continue
        x, y, w, h = line[:4]
        if any(type(value) is not int for value in (x, y, w, h)):
            continue
        if x < 0 or y < 0 or w <= 0 or h <= 0 or x + w > width or y + h > height:
            continue
        # Four pixels of original context, never a resized or perceptual key.
        box = (max(0, x - 4), max(0, y - 4), min(width, x + w + 4), min(height, y + h + 4))
        if (box[2] - box[0]) * (box[3] - box[1]) <= MAX_LINE_PIXELS:
            boxes.append(box)
    return boxes


def measure(directory, max_frames, include_lines):
    started = time.monotonic()
    cpu_started = time.process_time()
    full_cache, patch_cache = OrderedDict(), OrderedDict()
    totals = dict(frames=0, original_pixels=0, whole_frame_candidates=0,
                  whole_frame_candidate_pixels=0, line_patches=0,
                  line_patch_candidates=0, same_position_line_patch_candidates=0,
                  moved_line_patch_candidates=0, line_patch_candidate_area_sum=0,
                  frames_without_geometry=0)
    rows_report = []
    db = sqlite3.connect((directory / "index.sqlite").as_uri() + "?mode=ro", uri=True)
    try:
        db.execute("PRAGMA query_only=ON")
        db.execute("PRAGMA cache_size=-2048")
        # Materialize bounded source metadata only. Fetch geometry one frame at
        # a time below, without holding a read transaction across decoding.
        have_geometry = db.execute("SELECT 1 FROM sqlite_master WHERE name='frame_ocr_geometry'").fetchone()
        rows = db.execute("SELECT id,codec,path,source_path,width,height FROM frames "
                          "ORDER BY timestamp_ms,id LIMIT ?", (max_frames + 1,)).fetchall()
        if len(rows) > max_frames:
            raise ValueError("dataset exceeds --max-frames; no partial report was written")
    finally:
        db.close()
    for position, (frame_id, codec, path, source, width, height) in enumerate(rows):
        relative = path if codec == "webp" else source
        if not relative:
            raise ValueError("an exact retained original is unavailable; no lossy fallback is allowed")
        # A separate short read means text/geometry RAM stays bounded to one
        # frame, including when every stored frame reaches its 3 MiB limit.
        serialized = None
        if have_geometry and include_lines:
            with sqlite3.connect((directory / "index.sqlite").as_uri() + "?mode=ro", uri=True) as geometry_db:
                entry = geometry_db.execute("SELECT lines_json FROM frame_ocr_geometry WHERE frame_id=? "
                                            "AND length(CAST(lines_json AS BLOB))<=3145728", (frame_id,)).fetchone()
                if entry is not None:
                    serialized = entry[0]
            geometry_db.close()
        boxes = line_boxes(serialized, width, height) if include_lines else []
        frame_has_geometry = serialized is not None
        del serialized
        with closing(read_image(directory, relative, width, height)) as image:
            whole = pixel_key(image)
            repeated = whole in full_cache
            put_bounded(full_cache, whole, position, FULL_CACHE_LIMIT)
            frame_stats = dict(position=position, pixels=width * height,
                               whole_frame_candidate=int(repeated), line_patches=len(boxes),
                               same_position_line_patch_candidates=0, moved_line_patch_candidates=0,
                               line_patch_candidate_area_sum=0)
            pending = []
            for box in boxes:
                key = pixel_key(image, box)
                prior = patch_cache.get(key)
                if prior is not None:
                    field = "same_position_line_patch_candidates" if prior == box else "moved_line_patch_candidates"
                    frame_stats[field] += 1
                    frame_stats["line_patch_candidate_area_sum"] += (box[2] - box[0]) * (box[3] - box[1])
                pending.append((key, box))
            # Never count repeated lines within the current image as a prior
            # frame reuse opportunity. Only prior frames populate these lookups.
            for key, box in pending:
                put_bounded(patch_cache, key, box, PATCH_CACHE_LIMIT)
        totals["frames"] += 1
        totals["original_pixels"] += width * height
        totals["whole_frame_candidates"] += int(repeated)
        totals["whole_frame_candidate_pixels"] += width * height if repeated else 0
        totals["frames_without_geometry"] += int(include_lines and not frame_has_geometry)
        for key in ("line_patches", "same_position_line_patch_candidates", "moved_line_patch_candidates", "line_patch_candidate_area_sum"):
            totals[key] += frame_stats[key]
        rows_report.append(frame_stats)
    totals["line_patch_candidates"] = totals["same_position_line_patch_candidates"] + totals["moved_line_patch_candidates"]
    return dict(schema_version=1, exact_rgba=1, ocr_performed=0, whole_frame_cache_limit=FULL_CACHE_LIMIT,
                line_patch_cache_limit=PATCH_CACHE_LIMIT, line_padding_pixels=4,
                line_position_uses_latest_donor=1,
                line_candidates_are_not_reuse_proof=1, patch_area_sum_may_overlap=1,
                historical_full_ocr_provenance_verified=0, totals=totals, frames=rows_report,
                elapsed_seconds=time.monotonic() - started, process_cpu_seconds=time.process_time() - cpu_started,
                process_peak_rss_kib=resource.getrusage(resource.RUSAGE_SELF).ru_maxrss)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--max-frames", type=int, default=4096)
    parser.add_argument("--line-patches", action="store_true", help="also count exact padded stored-line patch candidates")
    args = parser.parse_args()
    if not 1 <= args.max_frames <= 10000:
        parser.error("--max-frames must be between 1 and 10000")
    if args.output.exists():
        parser.error("output already exists")
    try:
        report = measure(args.directory.resolve(strict=True), args.max_frames, args.line_patches)
        # Exclusive creation prevents accidentally overwriting private inputs.
        with args.output.open("x", encoding="utf-8") as stream:
            json.dump(report, stream, indent=2)
            stream.write("\n")
        print(json.dumps(report["totals"], sort_keys=True))
    except Exception as error:
        # Exception details may include a private source path or malformed text.
        print(json.dumps({"error_type": type(error).__name__, "completed": 0}))
        raise SystemExit(1) from None


if __name__ == "__main__":
    main()
