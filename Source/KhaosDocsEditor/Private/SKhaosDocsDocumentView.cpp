// Copyright Khaos Games. All Rights Reserved.

#include "SKhaosDocsDocumentView.h"

#include "KhaosDocsStyle.h"
#include "Styling/AppStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "KhaosDocsDocumentView"

using namespace KhaosDocsMarkdown;

namespace
{
	/** Horizontal breathing room around the document column. */
	constexpr float PageMarginX = 28.0f;
	constexpr float PageMarginY = 20.0f;

	/** Indent per list nesting level and width reserved for the marker. */
	constexpr float ListIndent = 18.0f;
	constexpr float ListMarkerWidth = 18.0f;

	FName HeadingStyleName(const int32 InLevel)
	{
		switch (FMath::Clamp(InLevel, 1, 4))
		{
		case 1: return TEXT("Doc.H1");
		case 2: return TEXT("Doc.H2");
		case 3: return TEXT("Doc.H3");
		default: return TEXT("Doc.H4");
		}
	}

	/** Space above a heading depends on its weight; the first block in a document gets none. */
	float HeadingTopPadding(const int32 InLevel, const bool bIsFirst)
	{
		if (bIsFirst)
		{
			return 0.0f;
		}
		switch (FMath::Clamp(InLevel, 1, 4))
		{
		case 1: return 22.0f;
		case 2: return 20.0f;
		case 3: return 14.0f;
		default: return 10.0f;
		}
	}
}

void SKhaosDocsDocumentView::Construct(const FArguments& InArgs)
{
	OnLinkClicked = InArgs._OnLinkClicked;

	ChildSlot
	[
		SAssignNew(ScrollBox, SScrollBox)
		.ScrollBarPadding(FMargin(2.0f, 0.0f, 0.0f, 0.0f))

		+ SScrollBox::Slot()
		[
			// A fill-width box with computed side margins rather than a centred box with a max
			// width: wrapped text reports its wrapped width as its desired width, so a centred
			// box would shrink a little every frame until nothing wrapped any more.
			SNew(SBox)
			.Padding(this, &SKhaosDocsDocumentView::GetPageMargin)
			[
				SAssignNew(Blocks, SVerticalBox)
			]
		]
	];
}

FMargin SKhaosDocsDocumentView::GetPageMargin() const
{
	const float Width = static_cast<float>(GetCachedGeometry().GetLocalSize().X);
	const float Side = FMath::Max(PageMarginX, (Width - FKhaosDocsStyle::DocumentMaxWidth) * 0.5f);
	return FMargin(Side, PageMarginY, Side, PageMarginY * 2.0f);
}

void SKhaosDocsDocumentView::SetMarkdown(const FString& InMarkdown, bool bScrollToTop)
{
	Document = Parse(InMarkdown);
	HeadingWidgets.Reset();
	Blocks->ClearChildren();

	for (int32 Index = 0; Index < Document.Blocks.Num(); ++Index)
	{
		const FBlock& Block = Document.Blocks[Index];
		const FBlock* Previous = Index > 0 ? &Document.Blocks[Index - 1] : nullptr;
		const FBlock* Next = Index + 1 < Document.Blocks.Num() ? &Document.Blocks[Index + 1] : nullptr;

		const TSharedRef<SWidget> Widget = MakeBlockWidget(Block, Previous, Next);

		if (Block.Type == EBlockType::Heading)
		{
			HeadingWidgets.Add(Widget);
		}

		Blocks->AddSlot()
		.AutoHeight()
		[
			Widget
		];
	}

	if (bScrollToTop)
	{
		ScrollToTop();
	}
}

void SKhaosDocsDocumentView::SetMessage(const FText& InMessage)
{
	Document = FDocument();
	HeadingWidgets.Reset();
	Blocks->ClearChildren();

	Blocks->AddSlot()
	.AutoHeight()
	[
		SNew(STextBlock)
		.Text(InMessage)
		.TextStyle(&FKhaosDocsStyle::GetText("Doc.Empty.Body"))
		.AutoWrapText(true)
	];

	ScrollToTop();
}

void SKhaosDocsDocumentView::ScrollToHeading(int32 InHeadingIndex)
{
	if (HeadingWidgets.IsValidIndex(InHeadingIndex))
	{
		if (const TSharedPtr<SWidget> Widget = HeadingWidgets[InHeadingIndex].Pin())
		{
			ScrollBox->ScrollDescendantIntoView(Widget, /*bAnimateScroll*/ true, EDescendantScrollDestination::TopOrLeft);
		}
	}
}

void SKhaosDocsDocumentView::ScrollToTop()
{
	ScrollBox->ScrollToStart();
}

TSharedRef<SWidget> SKhaosDocsDocumentView::MakeRichText(const FString& InRichText, const FName InStyleName)
{
	return SNew(SRichTextBlock)
		.Text(FText::FromString(InRichText))
		.TextStyle(&FKhaosDocsStyle::GetText(InStyleName))
		.DecoratorStyleSet(&FKhaosDocsStyle::Get())
		.Decorators({ SRichTextBlock::HyperlinkDecorator(TEXT("doclink"), OnLinkClicked) })
		.AutoWrapText(true)
		.LineHeightPercentage(1.15f);
}

TSharedRef<SWidget> SKhaosDocsDocumentView::MakeBlockWidget(const FBlock& InBlock, const FBlock* InPrevious, const FBlock* InNext)
{
	switch (InBlock.Type)
	{
	case EBlockType::Heading:
	{
		const bool bIsFirst = InPrevious == nullptr;
		const bool bUnderlined = InBlock.Level <= 2;

		TSharedRef<SVerticalBox> Box = SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, HeadingTopPadding(InBlock.Level, bIsFirst), 0.0f, bUnderlined ? 4.0f : 6.0f)
			[
				MakeRichText(InBlock.RichText, HeadingStyleName(InBlock.Level))
			];

		if (bUnderlined)
		{
			Box->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 10.0f)
			[
				SNew(SSeparator)
				.SeparatorImage(FKhaosDocsStyle::Get().GetBrush("Doc.Separator"))
				.Thickness(1.0f)
			];
		}

		return Box;
	}

	case EBlockType::Paragraph:
		return SNew(SBox)
			.Padding(0.0f, 0.0f, 0.0f, 12.0f)
			[
				MakeRichText(InBlock.RichText, TEXT("Doc.Body"))
			];

	case EBlockType::CodeBlock:
		return SNew(SBox)
			.Padding(0.0f, 2.0f, 0.0f, 14.0f)
			[
				SNew(SBorder)
				.BorderImage(FKhaosDocsStyle::Get().GetBrush("Doc.CodeBlock.Background"))
				.Padding(FMargin(12.0f, 10.0f))
				[
					// Code never wraps; long lines scroll sideways inside the panel instead.
					SNew(SScrollBox)
					.Orientation(Orient_Horizontal)
					.ScrollBarThickness(FVector2D(4.0f, 4.0f))
					.ScrollBarPadding(FMargin(0.0f, 6.0f, 0.0f, 0.0f))

					+ SScrollBox::Slot()
					[
						SNew(STextBlock)
						.Text(FText::FromString(InBlock.Code))
						.TextStyle(&FKhaosDocsStyle::GetText("Doc.CodeBlock"))
						.LineHeightPercentage(1.25f)
					]
				]
			];

	case EBlockType::Quote:
		return SNew(SBox)
			.Padding(0.0f, 2.0f, 0.0f, 12.0f)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SBox)
					.WidthOverride(3.0f)
					[
						SNew(SBorder)
						.BorderImage(FKhaosDocsStyle::Get().GetBrush("Doc.Quote.Bar"))
						.Padding(0.0f)
					]
				]

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.Padding(12.0f, 2.0f, 0.0f, 2.0f)
				[
					MakeRichText(InBlock.RichText, TEXT("Doc.Quote"))
				]
			];

	case EBlockType::ListItem:
	{
		// A run of items reads as one list, so only the last item carries the paragraph gap.
		const bool bEndsList = InNext == nullptr || InNext->Type != EBlockType::ListItem;
		const FString Marker = InBlock.Marker.IsEmpty() ? FString(TEXT("\x2022")) : InBlock.Marker;

		return SNew(SBox)
			.Padding(ListIndent * InBlock.Level, 0.0f, 0.0f, bEndsList ? 12.0f : 3.0f)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Top)
				[
					SNew(SBox)
					.MinDesiredWidth(ListMarkerWidth)
					.Padding(2.0f, 0.0f, 4.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(FText::FromString(Marker))
						.TextStyle(&FKhaosDocsStyle::GetText("Doc.ListMarker"))
						.Justification(InBlock.Marker.IsEmpty() ? ETextJustify::Center : ETextJustify::Right)
					]
				]

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					MakeRichText(InBlock.RichText, TEXT("Doc.Body"))
				]
			];
	}

	case EBlockType::Rule:
	default:
		return SNew(SBox)
			.Padding(0.0f, 6.0f, 0.0f, 16.0f)
			[
				SNew(SSeparator)
				.SeparatorImage(FKhaosDocsStyle::Get().GetBrush("Doc.Separator"))
				.Thickness(1.0f)
			];
	}
}

#undef LOCTEXT_NAMESPACE
