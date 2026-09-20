#!/usr/bin/env python3
"""Numeric-only spatial change diagnostic for eight copied lossless originals.

This is offline analysis, not a capture tool or an OCR speed/quality benchmark.
It keeps one adjacent image pair in memory, writes no images, and never runs OCR.
Requires the existing Pillow and NumPy packages; does not install dependencies.
"""
import argparse
from collections import deque
import json
import os
from pathlib import Path
import sqlite3
import tempfile

import numpy as np
from PIL import Image


def sources(directory):
    root = directory.resolve(strict=True)
    with sqlite3.connect((root / "index.sqlite").as_uri() + "?mode=ro", uri=True) as connection:
        rows = connection.execute(
            "SELECT source_path,width,height FROM frames WHERE source_path<>'' ORDER BY timestamp_ms,id"
        ).fetchall()
    if len(rows) != 8:
        raise ValueError("Expected exactly eight retained source originals")
    result = []
    for relative, width, height in rows:
        path = (root / relative).resolve(strict=True)
        if not path.is_relative_to(root) or not path.is_file():
            raise ValueError("Source must be a file inside the selected copied dataset")
        if not (0 < width <= 16384 and 0 < height <= 16384 and width * height <= 32 * 1024 * 1024):
            raise ValueError("Source dimensions exceed the diagnostic bound")
        result.append((path, width, height))
    return result


def decode(source):
    path, width, height = source
    with Image.open(path) as image:
        if image.format != "WEBP" or image.size != (width, height):
            raise ValueError("Source metadata and WebP dimensions differ")
        return np.array(image.convert("RGBA"), dtype=np.uint8)


def rectangle_union_pixels(rectangles, width, height):
    # A frame-sized bool buffer is bounded and avoids double-counting overlapping
    # component boxes or their context. Rectangle right/bottom are exclusive.
    covered = np.zeros((height, width), dtype=np.bool_)
    for left, top, right, bottom in rectangles:
        covered[top:bottom, left:right] = True
    return int(np.count_nonzero(covered))


def components(grid, tile_width, tile_height, width, height):
    """Bounding rectangles of 4-neighbor components on the changed-tile grid."""
    visited = np.zeros_like(grid)
    result = []
    rows, columns = grid.shape
    for row, column in zip(*np.nonzero(grid)):
        if visited[row, column]:
            continue
        visited[row, column] = True
        pending = deque([(int(row), int(column))])
        first_row = last_row = int(row)
        first_column = last_column = int(column)
        tile_count = 0
        while pending:
            y, x = pending.popleft()
            tile_count += 1
            first_row, last_row = min(first_row, y), max(last_row, y)
            first_column, last_column = min(first_column, x), max(last_column, x)
            for dy, dx in ((-1, 0), (1, 0), (0, -1), (0, 1)):
                other_y, other_x = y + dy, x + dx
                if (0 <= other_y < rows and 0 <= other_x < columns and
                        grid[other_y, other_x] and not visited[other_y, other_x]):
                    visited[other_y, other_x] = True
                    pending.append((other_y, other_x))
        result.append(((first_column * tile_width, first_row * tile_height,
                        min(width, (last_column + 1) * tile_width),
                        min(height, (last_row + 1) * tile_height)), tile_count))
    return result


def tile_metrics(changed, tile_width, tile_height, context):
    height, width = changed.shape
    rows = (height + tile_height - 1) // tile_height
    columns = (width + tile_width - 1) // tile_width
    padded = np.pad(changed, ((0, rows * tile_height - height), (0, columns * tile_width - width)))
    grid = padded.reshape(rows, tile_height, columns, tile_width).any(axis=(1, 3))
    del padded
    footprints = []
    for row, column in zip(*np.nonzero(grid)):
        footprints.append((int(column) * tile_width, int(row) * tile_height,
                           min(width, (int(column) + 1) * tile_width),
                           min(height, (int(row) + 1) * tile_height)))
    tile_pixels = sum((right - left) * (bottom - top) for left, top, right, bottom in footprints)
    expanded_tiles = [(max(0, left - context), max(0, top - context),
                       min(width, right + context), min(height, bottom + context))
                      for left, top, right, bottom in footprints]
    groups = components(grid, tile_width, tile_height, width, height)
    boxes = [box for box, _ in groups]
    expanded_boxes = [(max(0, left - context), max(0, top - context),
                       min(width, right + context), min(height, bottom + context))
                      for left, top, right, bottom in boxes]
    total = width * height
    tile_context_pixels = rectangle_union_pixels(expanded_tiles, width, height)
    box_pixels = rectangle_union_pixels(boxes, width, height)
    box_context_pixels = rectangle_union_pixels(expanded_boxes, width, height)
    return {
        "tile_width": tile_width, "tile_height": tile_height,
        "total_tiles": int(grid.size), "changed_tiles": int(np.count_nonzero(grid)),
        "changed_tile_pixels": tile_pixels, "changed_tile_fraction": tile_pixels / total,
        "expanded_tile_union_pixels": tile_context_pixels,
        "expanded_tile_union_fraction": tile_context_pixels / total,
        "connected_components": len(groups),
        "largest_component_tiles": max((count for _, count in groups), default=0),
        "component_bbox_union_pixels": box_pixels,
        "component_bbox_union_fraction": box_pixels / total,
        "expanded_component_bbox_union_pixels": box_context_pixels,
        "expanded_component_bbox_union_fraction": box_context_pixels / total,
    }


def analyze(directory, context):
    inputs = sources(directory)
    pairs = []
    previous = decode(inputs[0])
    for index, source in enumerate(inputs[1:], start=1):
        current = decode(source)
        if previous.shape != current.shape:
            raise ValueError("Different dimensions cannot be compared without inventing an alignment")
        changed = np.any(previous != current, axis=2)
        height, width = changed.shape
        changed_pixels = int(np.count_nonzero(changed))
        if changed_pixels:
            ys = np.flatnonzero(changed.any(axis=1))
            xs = np.flatnonzero(changed.any(axis=0))
            bounding_width = int(xs[-1] - xs[0] + 1)
            bounding_height = int(ys[-1] - ys[0] + 1)
            expanded_bounding_width = min(width, int(xs[-1]) + 1 + context) - max(0, int(xs[0]) - context)
            expanded_bounding_height = min(height, int(ys[-1]) + 1 + context) - max(0, int(ys[0]) - context)
        else:
            bounding_width = bounding_height = expanded_bounding_width = expanded_bounding_height = 0
        total = width * height
        pairs.append({
            "pair": index - 1, "width": width, "height": height, "total_pixels": total,
            "changed_pixels": changed_pixels, "changed_pixel_fraction": changed_pixels / total,
            "single_bbox_width": bounding_width, "single_bbox_height": bounding_height,
            "single_bbox_pixels": bounding_width * bounding_height,
            "expanded_single_bbox_pixels": expanded_bounding_width * expanded_bounding_height,
            "tiles": [tile_metrics(changed, tile_width, tile_height, context)
                      for tile_width, tile_height in ((128, 64), (256, 128))],
        })
        previous = current
        del changed
    total_pixels = sum(pair["total_pixels"] for pair in pairs)
    aggregate = {key: sum(pair[key] for pair in pairs)
                 for key in ("total_pixels", "changed_pixels", "single_bbox_pixels", "expanded_single_bbox_pixels")}
    aggregate["changed_pixel_fraction"] = aggregate["changed_pixels"] / total_pixels
    aggregate["single_bbox_fraction"] = aggregate["single_bbox_pixels"] / total_pixels
    aggregate["expanded_single_bbox_fraction"] = aggregate["expanded_single_bbox_pixels"] / total_pixels
    aggregate["tiles"] = []
    for index in range(2):
        rows = [pair["tiles"][index] for pair in pairs]
        summary = {"tile_width": rows[0]["tile_width"], "tile_height": rows[0]["tile_height"]}
        for key in ("total_tiles", "changed_tiles", "connected_components", "changed_tile_pixels",
                    "expanded_tile_union_pixels", "component_bbox_union_pixels", "expanded_component_bbox_union_pixels"):
            summary[key] = sum(row[key] for row in rows)
        for key in ("changed_tile", "expanded_tile_union", "component_bbox_union", "expanded_component_bbox_union"):
            summary[key + "_fraction"] = summary[key + "_pixels"] / total_pixels
        aggregate["tiles"].append(summary)
    return {
        "source_frames": len(inputs), "adjacent_pairs": len(pairs), "context_pixels": context,
        "pixel_comparison": "any unequal decoded RGBA channel, with no threshold or image alignment",
        "component_definition": "4-neighbor connectivity on each changed-tile grid; not pixel or text components",
        "aggregate": aggregate, "pairs": pairs,
        "limits": [
            "Pixel/tile coverage estimates potential work regions, not OCR speed or recall quality.",
            "A changed pixel marks its whole tile; antialiasing, cursor movement and scrolling are not distinguished.",
            "Fixed context is not proven sufficient to preserve complete text lines or OCR page segmentation.",
            "A component bounding box includes holes and unchanged pixels; overlapping context is counted once.",
            "Exactly eight retained originals and seven consecutive transitions; not whole-day behavior.",
            "Input files are opened read-only; no recognized text, images or input paths are written to this report.",
        ],
    }


def save(path, report):
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=".tiles-", suffix=".json", dir=path.parent)
    try:
        with os.fdopen(descriptor, "w") as stream:
            json.dump(report, stream, indent=2)
            stream.write("\n")
        os.replace(temporary, path)
    finally:
        Path(temporary).unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--context", type=int, default=16)
    args = parser.parse_args()
    if not 0 <= args.context <= 256:
        parser.error("Context must be between 0 and 256 pixels")
    try:
        report = analyze(args.source_dir, args.context)
        save(args.output, report)
        print(json.dumps({"completed": True, "adjacent_pairs": report["adjacent_pairs"],
                          "aggregate": report["aggregate"]}))
        return 0
    except Exception as exception:
        # Do not include exception messages: image/database errors may name files.
        print(json.dumps({"completed": False, "error_type": type(exception).__name__}))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
