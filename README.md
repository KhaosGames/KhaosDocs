# KhaosDocs

Markdown documentation shown in the Unreal Content Browser, without turning it into assets.

Put a `.md` or `.txt` anywhere under a `Content` folder and it appears in the Content Browser, where
it sits on disk. There is nothing to register. The files stay plain text, so they diff in git and
open in any editor.

`.md` and `.txt` share a single Document type and are both rendered as markdown. New documents are
created as `.md`.

The documentation window additionally picks up a `Docs` folder beside the `.uplugin` and any
markdown at the plugin root, such as `README.md`. Those cannot go in the Content Browser — a mount
must be a directory inside a content root — but they open in the same editor.

The project and its own plugins are scanned. Engine content is not.

## What it adds

- Content Browser entries for every `.md` under a project content root.
- A document editor with an outline, a preview, and the raw markdown, saving back to the file.
- A Create Document entry in the right-click menu of any folder.
- A Docs toolbar button opening a table of contents across every plugin in the project.

## Requirements

Unreal Engine 5.8, and the engine's `ContentBrowserFileDataSource` plugin, which this one enables
as a dependency.

## Installing

```bash
git submodule add https://github.com/KhaosGames/KhaosDocs Plugins/KhaosDocs
```

Regenerate project files and build. The plugin is editor-only and contributes nothing to a
packaged build.

## How it works

The UE5 Content Browser accepts data sources beyond the asset registry. This plugin registers a
`UContentBrowserFileDataSource` for the `md` extension, mounts each project content root into it,
then activates it — registering alone leaves a data source available but inert. The engine uses
the same mechanism for `.py` in `PythonScriptPlugin` and `.po` in `PortableObjectFileDataSource`.

Whole roots are mounted rather than a reserved subfolder. `FContentBrowserFileDataDiscovery`
filters files by registered extension while walking, so this is a directory scan that collects
only markdown.

Opening a document wraps it in a transient `UObject` so it can drive an `FAssetEditorToolkit`.
That object is never saved to a package and never enters the asset registry. It exists while the
editor is open, and saving writes the `.md`.

## Status

Early. The renderer handles headings, emphasis, code, lists, quotes, rules and links. Tables and
images are not done. See `Content/Docs/Authoring.md`.
