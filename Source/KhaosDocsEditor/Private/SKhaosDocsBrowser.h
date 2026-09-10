// Copyright Khaos Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Types/SlateEnums.h"
#include "Widgets/SCompoundWidget.h"

class ITableRow;
class SKhaosDocsDocumentEditor;
class SSearchBox;
class STableViewBase;
template <typename ItemType> class STreeView;

/** A node in the table of contents: an owner (project or plugin), a folder, or a document. */
struct FDocsTreeItem
{
	enum class EType : uint8
	{
		Owner,
		Folder,
		Document,

		/** The group of favourited documents at the top of the tree. */
		Favorites,
	};

	EType Type = EType::Document;

	FString Label;

	/** Documents: absolute path to the file. */
	FString FilePath;

	/** Owners and folders: absolute path to the directory. */
	FString DirPath;

	/** Content Browser path of the owning content root, e.g. "/KhaosUI". */
	FString VirtualPath;

	/** Absolute path of the owner's Content folder. Only files under it exist in the Content Browser. */
	FString ContentDiskPath;

	/** Documents: name of the owning project or plugin, shown beside favourites. */
	FString OwnerLabel;

	TArray<TSharedPtr<FDocsTreeItem>> Children;
	TWeakPtr<FDocsTreeItem> Parent;

	bool IsDocument() const { return Type == EType::Document; }
};

/**
 * The documentation window: a table of contents across the project and every plugin on the
 * left, and the document editor on the right. Selecting a document shows it; the editor's own
 * View/Edit switch turns the right pane into a source editor with a live preview.
 */
class SKhaosDocsBrowser : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SKhaosDocsBrowser) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Rescan every docs root from disk and rebuild the tree, keeping the current document shown. */
	void Refresh();

	/**
	 * Show a document, selecting it in the table of contents when it is listed there. Files
	 * outside every root still open, they just have no tree entry. Optionally starts editing.
	 */
	void ShowDocument(const FString& InFilePath, bool bEdit = false);

	/** Whether the window may close now. Prompts to save unsaved edits; false if the user cancels. */
	bool CanClose();

private:
	/** One discovered document, with its title cached so filtering never touches the disk. */
	struct FCachedDoc
	{
		FString FilePath;
		FString Title;

		/** Folder holding the document, relative to the owner's base folder. Empty at the root. */
		FString RelativeDir;
	};

	/** A docs root and the documents found under it. */
	struct FCachedRoot
	{
		FString Owner;
		FString VirtualPath;
		FString BaseDir;
		FString ContentDiskPath;
		TArray<FCachedDoc> Docs;
	};

	/** Walk the docs roots and refill the cache. Reads every document's first heading. */
	void RescanDocuments();

	/** Rebuild the tree from the cache, applying the current filter. Does no file IO. */
	void RebuildTree();

	TSharedPtr<FDocsTreeItem> FindDocumentItem(const FString& InFilePath) const;
	void ExpandAll();
	void ExpandTo(const TSharedPtr<FDocsTreeItem>& InItem);

	TSharedRef<ITableRow> OnGenerateRow(TSharedPtr<FDocsTreeItem> InItem, const TSharedRef<STableViewBase>& OwnerTable);
	void OnGetChildren(TSharedPtr<FDocsTreeItem> InItem, TArray<TSharedPtr<FDocsTreeItem>>& OutChildren);
	void OnSelectionChanged(TSharedPtr<FDocsTreeItem> InItem, ESelectInfo::Type SelectInfo);
	void OnMouseDoubleClick(TSharedPtr<FDocsTreeItem> InItem);
	void OnFilterTextChanged(const FText& InText);
	TSharedPtr<SWidget> OnContextMenuOpening();
	FReply OnRefreshClicked();

	/** The editor followed a link, or a host asked for a file. */
	void OnNavigate(const FString& InFilePath);
	void OnDocumentSaved();

	void ShowInContentBrowser(const FDocsTreeItem& InItem) const;
	bool CanShowInContentBrowser(const FDocsTreeItem& InItem) const;

	void ToggleSidebar();

	/** Favourites and the last shown document persist per user in the project's editor settings. */
	void LoadSettings();
	void SaveSettings() const;
	bool IsFavorite(const FString& InFilePath) const;
	void ToggleFavorite(const FString& InFilePath);

	/** The first document in the tree, for when nothing better is known to show. */
	TSharedPtr<FDocsTreeItem> FindFirstDocumentItem() const;

	TSharedPtr<STreeView<TSharedPtr<FDocsTreeItem>>> TreeView;
	TSharedPtr<SSearchBox> SearchBox;
	TSharedPtr<SKhaosDocsDocumentEditor> Editor;

	bool bSidebarVisible = true;

	TArray<TSharedPtr<FDocsTreeItem>> RootItems;

	/** Disk scan results, refreshed only by Refresh() - never by typing in the search box. */
	TArray<FCachedRoot> CachedRoots;

	FString FilterText;

	/** Absolute paths of favourited documents. */
	TArray<FString> Favorites;

	/** Absolute path of the document shown when the window was last used, restored on open. */
	FString LastDocumentPath;

	/** Set while the tree selection is being changed by code, so it is not treated as a click. */
	bool bSyncingSelection = false;
};
