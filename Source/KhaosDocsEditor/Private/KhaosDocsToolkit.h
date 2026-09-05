// Copyright Khaos Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Text/SlateHyperlinkRun.h"
#include "KhaosDocsMarkdown.h"
#include "Toolkits/AssetEditorToolkit.h"

class SMultiLineEditableTextBox;
class SRichTextBlock;
class SScrollBox;
class UKhaosDocsDocument;
template <typename ItemType> class SListView;

/**
 * Document editor for a markdown file.
 *
 * This is a real FAssetEditorToolkit - dockable tabs, layout persistence, the standard asset
 * editor chrome - driven by a transient UObject rather than a package. Everything it saves goes
 * straight back to the .md file on disk.
 */
class FKhaosDocsToolkit : public FAssetEditorToolkit
{
public:
	void InitEditor(
		const EToolkitMode::Type InMode,
		const TSharedPtr<IToolkitHost>& InToolkitHost,
		UKhaosDocsDocument* InDocument);

	//~ Begin FAssetEditorToolkit interface
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FText GetToolkitName() const override;
	virtual FString GetWorldCentricTabPrefix() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;
	virtual bool OnRequestClose(EAssetEditorCloseReason InCloseReason) override;
	//~ End FAssetEditorToolkit interface

	static const FName ToolkitName;

private:
	TSharedRef<SDockTab> SpawnTab_Preview(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Source(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Outline(const FSpawnTabArgs& Args);

	void ExtendToolbar();

	/** Re-parse the working text and push it into the preview and the outline. */
	void RefreshPreview();

	void OnSourceTextChanged(const FText& InText);

	void OnSaveClicked();
	void OnReloadClicked();
	void OnOpenExternallyClicked();
	bool IsDocumentDirty() const;

	/** Resolve a markdown link: http(s) opens a browser, a relative .md opens another document. */
	void OnLinkClicked(const FSlateHyperlinkRun::FMetadata& InMetadata);

	UKhaosDocsDocument* GetDocument() const;

	TSharedPtr<SRichTextBlock> PreviewBlock;
	TSharedPtr<SMultiLineEditableTextBox> SourceBox;
	TSharedPtr<SScrollBox> PreviewScrollBox;
	TSharedPtr<SListView<TSharedPtr<KhaosDocsMarkdown::FHeading>>> OutlineList;

	TArray<TSharedPtr<KhaosDocsMarkdown::FHeading>> Outline;

	/** Rendered section widgets, index-aligned with Outline, so the outline can scroll to one. */
	TArray<TSharedPtr<SWidget>> SectionWidgets;

	static const FName PreviewTabId;
	static const FName SourceTabId;
	static const FName OutlineTabId;
};
