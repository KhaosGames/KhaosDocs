// Copyright Khaos Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Text/SlateHyperlinkRun.h"
#include "KhaosDocsMarkdown.h"
#include "Widgets/SCompoundWidget.h"

class SScrollBox;
class SVerticalBox;

/**
 * Renders markdown as a column of block widgets inside a scroll box.
 *
 * Each block gets its own widget - headings, paragraphs, code blocks with a backing panel, quotes
 * with a bar, list items with real indentation - which is what makes the result read like a
 * document rather than a wall of styled text, and gives the outline addressable targets.
 */
class SKhaosDocsDocumentView : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SKhaosDocsDocumentView) {}
		/** Invoked for every link in the document. The href is in the metadata under "href". */
		SLATE_EVENT(FSlateHyperlinkRun::FOnClick, OnLinkClicked)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Parse and render. Replaces whatever was shown before and scrolls back to the top. */
	void SetMarkdown(const FString& InMarkdown, bool bScrollToTop = true);

	/** Show a single centred message instead of a document. */
	void SetMessage(const FText& InMessage);

	const TArray<KhaosDocsMarkdown::FHeading>& GetHeadings() const { return Document.Headings; }

	/** Bring the heading at the given index in GetHeadings() into view. */
	void ScrollToHeading(int32 InHeadingIndex);

	void ScrollToTop();

private:
	TSharedRef<SWidget> MakeBlockWidget(const KhaosDocsMarkdown::FBlock& InBlock, const KhaosDocsMarkdown::FBlock* InPrevious, const KhaosDocsMarkdown::FBlock* InNext);
	TSharedRef<SWidget> MakeRichText(const FString& InRichText, const FName InStyleName);

	/** Margins that centre the column and cap it at DocumentMaxWidth, from the current width. */
	FMargin GetPageMargin() const;

	FSlateHyperlinkRun::FOnClick OnLinkClicked;

	TSharedPtr<SScrollBox> ScrollBox;
	TSharedPtr<SVerticalBox> Blocks;

	KhaosDocsMarkdown::FDocument Document;

	/** Rendered heading widgets, index-aligned with Document.Headings. */
	TArray<TWeakPtr<SWidget>> HeadingWidgets;
};
