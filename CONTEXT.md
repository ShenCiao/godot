# Project Context

This checkout is a custom Godot build for developing a paint&animation app like CSP or Krita.

## Build
Use "Build: Windows Debug" in .vscode/tasks.json

## Rendering

Rendering work in this repository targets the RD renderer path; GLES3 compatibility does not need to be preserved.

Canvas item Z indexing is intentionally narrowed to 256 global buckets (`-128` to `127`) for this custom build for less memory allocation for nested canvas groups.

## Arrangement 2D

An arrangement face is a finite or infinite 2D region separated by user-authored polylines.

A triangle result is a finite triangle mesh for a repaired 2D region: a point pool plus triangle index triples into that pool.
