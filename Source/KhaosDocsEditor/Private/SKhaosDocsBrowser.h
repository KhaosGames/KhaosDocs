// Copyright Khaos Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Text/SlateHyperlinkRun.h"
#include "Types/SlateEnums.h"
#include "Widgets/SCompoundWidget.h"

class ITableRow;
class SRichTextBlock;
class SScrollBox;
class SSearchBox;
class STableViewBase;
template <typename ItemType> class STreeView;

/** A node in the table of contents: either an owner (project / plugin) or a single document. */
struct FDocsTreeItem
{
	FString Label;

	/** Absolute path to the markdown file. Empty for grouping nodes. */
	FString FilePath;

	/** Content Browser path of the owning content root. */
	FString VirtualPath;

	/** Folder holding the document, relative to its content root. Empty when it sits at the root. */
	FString RelativeDir;

	TArray<TSharedPtr<FDocsTreeItem>> Children;

	bool IsDocument() const { return !FilePath.IsEmpty(); }
};

/**
 * The project-wide docs window: every plugin that ships a Content/Docs folder becomes a section
 * in the table of contents, and selecting a document renders it inline.
 */
class SKhaosDocsBrowser : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SKhaosDocsBrowser) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Rescan every mounted docs root from disk, then rebuild the tree. */
	void Refresh();

private:
	/** One discovered document, with its title cached so filtering never touches the disk. */
	struct FCachedDoc
	{
		FString FilePath;
		FString Title;
		FString RelativeDir;
	};

	/** A docs root and the documents found under it. */
	struct FCachedRoot
	{
		FString Owner;
		FString VirtualPath;
		TArray<FCachedDoc> Docs;
	};

	/** Walk the mounted docs roots and refill the cache. Reads every document's first heading. */
	void RescanDocuments();

	/** Rebuild the tree from the cache, applying the current filter. Does no file IO. */
	void RebuildTree();

	TSharedRef<ITableRow> OnGenerateRow(TSharedPtr<FDocsTreeItem> InItem, const TSharedRef<STableViewBase>& OwnerTable);
	void OnGetChildren(TSharedPtr<FDocsTreeItem> InItem, TArray<TSharedPtr<FDocsTreeItem>>& OutChildren);
	void OnSelectionChanged(TSharedPtr<FDocsTreeItem> InItem, ESelectInfo::Type SelectInfo);
	void OnMouseDoubleClick(TSharedPtr<FDocsTreeItem> InItem);
	void OnFilterTextChanged(const FText& InText);

	void ShowDocument(const FString& InFilePath);
	void OnLinkClicked(const FSlateHyperlinkRun::FMetadata& InMetadata);

	FReply OnOpenInEditorClicked();
	FReply OnOpenExternallyClicked();
	bool HasSelectedDocument() const;

	/** Expand every owner node, so a freshly opened window shows the whole table of contents. */
	void ExpandAll();

	TSharedPtr<STreeView<TSharedPtr<FDocsTreeItem>>> TreeView;
	TSharedPtr<SSearchBox> SearchBox;
	TSharedPtr<SScrollBox> ContentScrollBox;
	TSharedPtr<SRichTextBlock> ContentBlock;

	TArray<TSharedPtr<FDocsTreeItem>> RootItems;

	/** Disk scan results, refreshed only by Refresh() - never by typing in the search box. */
	TArray<FCachedRoot> CachedRoots;

	FString FilterText;
	FString SelectedFilePath;
	FText SelectedTitle;
};
