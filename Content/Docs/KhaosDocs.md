# Khaos Docs

Markdown files under any `Content` folder show up in the Content Browser as documents. Nothing is
imported and nothing becomes a `.uasset`.

Put a `.md` wherever it belongs — beside the assets it describes, or in a `Docs` folder if you
prefer to keep them together. Both work; the Content Browser shows the file where it sits on disk.

This page is one of them, at `Plugins/KhaosDocs/Content/Docs/KhaosDocs.md`.

## Scope

The project's own `Content` and every project plugin's `Content` are scanned. Engine content is
not — there are several hundred engine plugins and none of their documentation is yours to
maintain.

There is nothing to register. A plugin participates by having a `.md` file somewhere in its
`Content` folder.

## Working with a document

- Double-click opens the document editor: outline, preview, and raw markdown side by side.
  Editing the markdown updates the preview. Save writes the file.
- Right-click any folder and choose Create Document for a new `.md`. The name you type becomes
  its first heading.
- Rename, move, copy and delete act on the file.

Open Externally hands the file to whatever your system has registered for `.md`.

If you edit a document outside the editor while it is also open inside, saving asks before
overwriting.

## How it works

The Content Browser in UE5 accepts data sources beyond the asset registry. This plugin registers
one for the `md` extension, mounts each project content root into it, and activates it. The engine
does the same for `.py` in the Python plugin.

Directory scanning filters by extension as it walks, so mounting a whole content root costs a
directory walk and finds nothing but markdown.

Opening a document wraps it in a transient object so it can drive a normal asset editor. That
object is never saved and never enters the asset registry. Saving writes the markdown file.

Files under `Content` that are not assets are skipped by the cook, so documentation does not reach
a packaged build.

## Limits

The renderer handles a subset of markdown. [Authoring](Authoring.md) lists what it covers.
