#!/usr/bin/env python3
"""Independently compare the source PLY with meshoptimizer's decoded buffers."""

import argparse
from pathlib import Path
import sys

import numpy as np
import trimesh


VERTEX_DTYPE = np.dtype(
    [
        ("position", "<f4", (3,)),
        ("color", "u1", (4,)),
    ],
    align=False,
)


def canonical_faces(faces: np.ndarray) -> np.ndarray:
    """Sort complete oriented triangles without rotating their three indices."""
    faces = np.ascontiguousarray(faces, dtype=np.uint32)
    order = np.lexsort((faces[:, 2], faces[:, 1], faces[:, 0]))
    return faces[order]


def result(value: bool) -> str:
    return "PASS" if value else "FAIL"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source_ply", type=Path)
    parser.add_argument("decoded_vertices", type=Path)
    parser.add_argument("decoded_indices", type=Path)
    parser.add_argument("report", type=Path)
    args = parser.parse_args()

    # process=False is important: Trimesh must inspect the PLY as stored instead
    # of merging vertices, repairing topology, or otherwise changing the mesh.
    source = trimesh.load_mesh(args.source_ply, process=False, maintain_order=True)
    if not isinstance(source, trimesh.Trimesh):
        raise TypeError("the source did not load as one Trimesh mesh")

    decoded_records = np.fromfile(args.decoded_vertices, dtype=VERTEX_DTYPE)
    decoded_faces = np.fromfile(args.decoded_indices, dtype="<u4")
    if decoded_faces.size % 3 != 0:
        raise ValueError("decoded index buffer is not divisible into triangles")
    decoded_faces = decoded_faces.reshape((-1, 3))

    decoded_positions = decoded_records["position"]
    decoded_colors = decoded_records["color"]
    source_positions = np.asarray(source.vertices, dtype=np.float32)
    source_colors = np.asarray(source.visual.vertex_colors, dtype=np.uint8)
    source_faces = np.asarray(source.faces, dtype=np.uint32)

    count_equal = (
        source_positions.shape == decoded_positions.shape
        and source_colors.shape == decoded_colors.shape
        and source_faces.shape == decoded_faces.shape
    )
    positions_equal = np.array_equal(source_positions, decoded_positions)
    colors_equal = np.array_equal(source_colors, decoded_colors)
    oriented_faces_equal = np.array_equal(canonical_faces(source_faces), canonical_faces(decoded_faces))

    decoded = trimesh.Trimesh(
        vertices=decoded_positions.astype(np.float64),
        faces=decoded_faces,
        vertex_colors=decoded_colors,
        process=False,
        maintain_order=True,
    )
    bounds_equal = np.array_equal(np.asarray(source.bounds, dtype=np.float32), np.asarray(decoded.bounds, dtype=np.float32))
    area_equal = bool(np.isclose(source.area, decoded.area, rtol=1e-12, atol=1e-12))
    volume_equal = bool(np.isclose(source.volume, decoded.volume, rtol=1e-12, atol=1e-12))
    overall = count_equal and positions_equal and colors_equal and oriented_faces_equal and bounds_equal and area_equal and volume_equal

    lines = [
        "Independent Trimesh validation",
        "==============================",
        f"trimesh_version={trimesh.__version__}",
        f"numpy_version={np.__version__}",
        f"source_vertices={len(source.vertices)}",
        f"decoded_vertices={len(decoded.vertices)}",
        f"source_faces={len(source.faces)}",
        f"decoded_faces={len(decoded.faces)}",
        f"counts_equal={result(count_equal)}",
        f"positions_exact={result(positions_equal)}",
        f"colors_exact={result(colors_equal)}",
        f"oriented_face_set_equal={result(oriented_faces_equal)}",
        f"bounds_equal={result(bounds_equal)}",
        f"surface_area_equal={result(area_equal)}",
        f"volume_equal={result(volume_equal)}",
        f"overall_trimesh_validation={result(overall)}",
    ]
    text = "\n".join(lines) + "\n"
    args.report.write_text(text, encoding="utf-8")

    print("\n5. INDEPENDENT TRIMESH CHECK")
    print(f"   Positions exactly equal       : {result(positions_equal)}")
    print(f"   RGBA colors exactly equal     : {result(colors_equal)}")
    print(f"   Oriented triangles equal      : {result(oriented_faces_equal)}")
    print(f"   Bounds, area, and volume      : {result(bounds_equal and area_equal and volume_equal)}")
    print(f"   Overall Trimesh validation    : {result(overall)}")
    return 0 if overall else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:  # Make setup or input errors understandable.
        print(f"Trimesh validation error: {exc}", file=sys.stderr)
        sys.exit(1)
