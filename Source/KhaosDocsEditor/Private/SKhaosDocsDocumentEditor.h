// Copyright Khaos Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Text/SlateHyperlinkRun.h"
#include "KhaosDocsMarkdown.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"

class SKhaosDocsDocumentView;
class SMultiLineEditableTextBox;
class SWidgetSwitcher;
class STableViewBase;
class ITableRow;
class UKhaosDocsDocument;
template <typename ItemType> class SListView;

/** A link to another document was followed. The path is absolute and known to exist. */
DECLARE_DELEGATE_OneParam(FOnKhaosDocsNavigate, const FString& /*FilePath*/);

/**
 * Reads and edits one document: a header with the title, path and actions, then either the
 * rendered document or a side-by-side source editor with a live preview, with an outline of the
 * document's headings beside it.
 *
 * This is the one place documents are read and edited; the documentation window hosts it in its
 * right pane.
 */
class SKhaosDocsDocumentEditor : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SKhaosDocsDocumentEditor)
		: _IsSidebarVisible(true)
	{}
		/** Bound: a toggle for the host's table of contents appears at the left of the header. */
		SLATE_EVENT(FSimpleDelegate, OnToggleSidebar)

		/** Whether the host's table of contents is showing, for the toggle's state. */
		SLATE_ATTRIBUTE(bool, IsSidebarVisible)

		/** A relative link was followed. When unbound the target simply replaces the current document. */
		SLATE_EVENT(FOnKhaosDocsNavigate, OnNavigate)

		/** Text, file or dirty state changed. Hosts refresh their outline and title from here. */
		SLATE_EVENT(FSimpleDelegate, OnDocumentChanged)

		/** The document was written to disk. The table of contents re-reads titles on this. */
		SLATE_EVENT(FSimpleDelegate, OnSaved)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/**
	 * Show a file. Prompts to save first if the current document has unsaved edits, and returns
	 * false if the user cancels or the file cannot be read. Reopening the current file re-reads
	 * it from disk unless it has unsaved edits.
	 */
	bool OpenFile(const FString& InFilePath);

	/** Adopt a document that is already loaded, for hosts that own the object themselves. */
	void SetDocument(UKhaosDocsDocument* InDocument);

	/** Drop the current document and show the empty state. Does not prompt. */
	void Clear();

	/**
	 * Offer to save unsaved edits. Returns false if the user cancelled, in which case the caller
	 * should abandon whatever it was about to do (close, switch document).
	 */
	bool PromptToSaveIfDirty();

	bool Save();
	void Reload();

	bool HasDocument() const;
	bool IsDirty() const;
	bool IsEditing() const { return bEditing; }
	void SetEditing(bool bInEditing);

	FString GetFilePath() const;
	FText GetTitle() const;
	UKhaosDocsDocument* GetDocument() const;

	const TArray<KhaosDocsMarkdown::FHeading>& GetHeadings() const;
	void ScrollToHeading(int32 InHeadingIndex);

	//~ Begin SWidget interface
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	//~ End SWidget interface

private:
	TSharedRef<SWidget> MakeHeader();
	TSharedRef<SWidget> MakeDivider();
	TSharedRef<SWidget> MakePaneToggle(
		const FName InIconName,
		const FText& InToolTip,
		FOnClicked InOnClicked,
		TAttribute<bool> InIsPaneVisible);
	TSharedRef<SWidget> MakeEmptyState();
	TSharedRef<SWidget> MakeOutline();
	TSharedRef<ITableRow> OnGenerateOutlineRow(TSharedPtr<KhaosDocsMarkdown::FHeading> InItem, const TSharedRef<STableViewBase>& OwnerTable);
	void OnOutlineSelectionChanged(TSharedPtr<KhaosDocsMarkdown::FHeading> InItem, ESelectInfo::Type SelectInfo);
	EVisibility GetOutlineVisibility() const;

	/** Rebuild the outline list from the headings of whichever view is showing. */
	void RefreshOutline();
	TSharedRef<SWidget> MakeToolbarButton(
		const FName InIconName,
		const FText& InToolTip,
		FOnClicked InOnClicked,
		TAttribute<bool> InIsEnabled);

	/** Push the document text into whichever views are showing. */
	void RefreshViews(bool bScrollToTop);

	void NotifyChanged();

	FText GetDisplayPath() const;
	FText GetHeaderTitle() const;
	int32 GetActiveSwitcherIndex() const;

	void OnSourceTextChanged(const FText& InText);
	void OnModeChanged(bool bInEditing);
	void OnLinkClicked(const FSlateHyperlinkRun::FMetadata& InMetadata);

	FReply OnSaveClicked();
	FReply OnReloadClicked();
	FReply OnOpenExternallyClicked();
	FReply OnToggleSidebarClicked();
	FReply OnToggleOutlineClicked();

	FSimpleDelegate OnToggleSidebar;
	TAttribute<bool> IsSidebarVisible;
	FOnKhaosDocsNavigate OnNavigate;
	FSimpleDelegate OnDocumentChanged;
	FSimpleDelegate OnSaved;

	TStrongObjectPtr<UKhaosDocsDocument> Document;

	TSharedPtr<SWidgetSwitcher> Switcher;
	TSharedPtr<SKhaosDocsDocumentView> ReadView;
	TSharedPtr<SKhaosDocsDocumentView> PreviewView;
	TSharedPtr<SMultiLineEditableTextBox> SourceBox;

	TSharedPtr<SListView<TSharedPtr<KhaosDocsMarkdown::FHeading>>> OutlineList;
	TArray<TSharedPtr<KhaosDocsMarkdown::FHeading>> Outline;
	bool bOutlineVisible = true;

	bool bEditing = false;

	/** Set while SourceBox->SetText runs, so the resulting change event is not treated as an edit. */
	bool bSettingSourceText = false;

	/** Header text, recomputed in NotifyChanged rather than per frame. */
	FText CachedTitle;
	FText CachedDisplayPath;
};
