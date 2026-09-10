# Khaos Docs

Documents under any `Content` folder show up in the Content Browser. Nothing is imported and
nothing becomes a `.uasset`.

`.md` and `.txt` are both recognised, and there is no distinction between them: one Document type,
one renderer, markdown in both cases. A `.txt` with no markup simply comes out as plain paragraphs.
New documents are created as `.md`.

Put a `.md` wherever it belongs — beside the assets it describes, or in a `Docs` folder if you
prefer to keep them together. Both work; the Content Browser shows the file where it sits on disk.

This page is one of them, at `Plugins/KhaosDocs/Content/Docs/KhaosDocs.md`.

## Where documents are found

Three places per plugin, and the same three for the project itself:

- Anywhere under `Content`, recursively.
- A `Docs` folder beside the `.uplugin`, recursively.
- Documents sitting directly beside the `.uplugin` — `README.md`, `CHANGELOG.md` and the like.

That last one is not searched recursively, so `Source`, `Binaries`, `Intermediate` and any
vendored third-party readme are left alone.

Only the `Content` files appear in the Content Browser; a mount has to be a directory inside a
content root, and files outside `Content` are not content. The other two show up in the
documentation window and open in the same editor.

Engine content is not scanned at all. There are several hundred engine plugins and none of their
documentation is yours to maintain.

There is nothing to register. A plugin participates by having a `.md` file in one of those places.

## The documentation window

The Docs button on the level editor toolbar opens it, and so does double-clicking a document in
the Content Browser. The left side is a table of contents: the project and each plugin, the
folders under them, and the documents. The right side shows the selected document.

Reading and editing happen in the same place. The View / Edit switch above the document turns
the right side into the markdown source with a live preview beside it; Save writes the file,
Reload re-reads it, and Ctrl+S works while editing. Switching to another document with unsaved
edits asks first. Double-clicking a document in the table of contents starts editing it.

An outline of the document's headings sits to the right of the page; clicking one scrolls to
it. The header also offers Open Externally, which hands the file to whatever your system has
registered for `.md`, a button that hides that outline, and a button at its far left that hides
the table of contents for reading.

Right-click a document in the table of contents to add it to Favorites, which are listed in
their own group at the top. The same menu offers Show in Content Browser, Show in Explorer and
Copy Path. The window reopens on the document you last had showing.

In the Content Browser, right-click any folder and choose Create Document for a new `.md`. The
name you type becomes its first heading. Rename, move, copy and delete act on the file.

If you edit a document outside Unreal while it is also open inside, saving asks before
overwriting.

## How it works

The Content Browser in UE5 accepts data sources beyond the asset registry. This plugin registers
one for the `md` extension, mounts each project content root into it, and activates it. The engine
does the same for `.py` in the Python plugin.

Directory scanning filters by extension as it walks, so mounting a whole content root costs a
directory walk and finds nothing but markdown.

Opening a document wraps it in a transient object. The Content Browser will only draw a class
thumbnail for a type that resolves to a real class, so the file type is registered under that
object's class; nothing is ever loaded through it, saved, or entered into the asset registry.
Saving writes the markdown file.

Files under `Content` that are not assets are skipped by the cook, so documentation does not reach
a packaged build.

## Limits

The renderer handles a subset of markdown. [Authoring](Authoring.md) lists what it covers.
