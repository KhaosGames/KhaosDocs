# KhaosDocs

[![CI](https://github.com/KhaosSystems/KhaosDocs/actions/workflows/ci.yml/badge.svg)](https://github.com/KhaosSystems/KhaosDocs/actions/workflows/ci.yml)
[![Tests](https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/KhaosSystems/KhaosDocs/badges/tests.json)](https://github.com/KhaosSystems/KhaosDocs/actions/workflows/ci.yml)
![Unreal Engine](https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/KhaosSystems/KhaosDocs/badges/unreal.json)
![Lines of code](https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/KhaosSystems/KhaosDocs/badges/loc.json)
[![Release](https://img.shields.io/github/v/release/KhaosSystems/KhaosDocs)](https://github.com/KhaosSystems/KhaosDocs/releases)

Markdown documentation in the Unreal Content Browser. Any `.md` or `.txt` under a `Content` folder
shows up where it sits on disk and opens in an in-editor viewer and editor, with a table of
contents across the project and its plugins. The files stay plain text, so they diff in git.

## Install

```bash
git submodule add -b release https://github.com/KhaosSystems/KhaosDocs Plugins/KhaosDocs
```

Unreal Engine 5.8. Editor-only; it adds nothing to a packaged game.

## Docs

[KhaosDocs](Content/Docs/KhaosDocs.md) and [Authoring](Content/Docs/Authoring.md), which also open
in the editor's documentation window.

## Development

Tests are automation specs in `Source/KhaosDocsEditor/Private/Tests`. CI and releases run on
[KhaosSystems/unreal-actions](https://github.com/KhaosSystems/unreal-actions); push a `vX.Y.Z` tag
to release.
