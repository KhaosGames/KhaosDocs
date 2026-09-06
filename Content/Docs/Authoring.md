# Authoring

Documents are ordinary markdown. The renderer covers a subset; anything outside it falls through
as plain text rather than failing, so a file that reads well on GitHub will read acceptably here.

`.md` and `.txt` are both recognised. There is no distinction between them — one Document type, one
renderer. A plain `.txt` with no markup just comes out as paragraphs, which is usually what you
want, and a `.txt` that happens to use markdown gets it rendered.

## What renders

- Headings, `#` through `###`. Deeper levels render at the same size as `###`.
- `**bold**` and `*italic*`, and the `__` and `_` forms.
- Inline code with backticks.
- Code blocks, fenced or indented four spaces. The language tag on a fence is ignored.
- Bullet lists with `-`, `*` or `+`, and numbered lists. Your numbering is kept as written.
- Block quotes with `>`.
- Horizontal rules.
- Links, `[text](url)`.

Underscores inside a word do not start emphasis, so `snake_case_name` survives intact.

## What does not

Tables and images. Both render as their source text. Tables are the more likely of the two to
matter, and are the next thing worth adding.

A `---` line is read as a horizontal rule, so YAML front matter at the top of a file will show up
as two rules with the keys between them.

## Links

Where a link points decides what it does:

- `http://` and `https://` open in your browser.
- A relative path such as `[the overview](KhaosDocs.md)` opens that document. In the docs window
  it navigates in place; in the document editor it opens the other document.
- A trailing `#anchor` is dropped before the path is resolved, so GitHub-style anchors do no harm,
  but they also do not jump anywhere.

Relative links resolve against the folder holding the document, which is what GitHub does, so one
link works in both places.

## Titles

The first `#` heading becomes the document's title in the table of contents. Without one, the file
name is used. Worth the one line.

## Where files go

Beside the assets they describe:

```
Content/UI/
    WBP_MainMenu.uasset
    MainMenu.md
```

Collected under Content, if that suits the plugin better:

```
Content/Docs/
    Overview.md
    Systems/Input.md
```

Or outside Content entirely, next to the `.uplugin`:

```
MyPlugin.uplugin
README.md
Docs/
    Architecture.md
```

The first two appear in the Content Browser. All three appear in the documentation window and open
in the same editor.

The table of contents groups by plugin and shows the containing folder next to each title, so
several files named `Readme` stay distinguishable. A file at the plugin root shows no folder.
