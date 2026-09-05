// Copyright Khaos Games. All Rights Reserved.

#include "KhaosDocsStyle.h"

#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateNoResource.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateTypes.h"
#include "Styling/StyleColors.h"

TSharedPtr<FSlateStyleSet> FKhaosDocsStyle::StyleInstance = nullptr;

void FKhaosDocsStyle::Initialize()
{
	if (!StyleInstance.IsValid())
	{
		StyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
	}
}

void FKhaosDocsStyle::Shutdown()
{
	if (StyleInstance.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
		ensure(StyleInstance.IsUnique());
		StyleInstance.Reset();
	}
}

const ISlateStyle& FKhaosDocsStyle::Get()
{
	return *StyleInstance;
}

FName FKhaosDocsStyle::GetStyleSetName()
{
	static const FName StyleSetName(TEXT("KhaosDocsStyle"));
	return StyleSetName;
}

TSharedRef<FSlateStyleSet> FKhaosDocsStyle::Create()
{
	TSharedRef<FSlateStyleSet> Style = MakeShared<FSlateStyleSet>(GetStyleSetName());

	const FTextBlockStyle Body = FTextBlockStyle()
		.SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 10))
		.SetColorAndOpacity(FStyleColors::Foreground)
		.SetSelectedBackgroundColor(FStyleColors::Select);

	Style->Set("Doc.Body", Body);

	Style->Set("Doc.Bold", FTextBlockStyle(Body)
		.SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 10))
		.SetColorAndOpacity(FStyleColors::White));

	Style->Set("Doc.Italic", FTextBlockStyle(Body)
		.SetFont(FCoreStyle::GetDefaultFontStyle("Italic", 10)));

	Style->Set("Doc.BoldItalic", FTextBlockStyle(Body)
		.SetFont(FCoreStyle::GetDefaultFontStyle("BoldItalic", 10))
		.SetColorAndOpacity(FStyleColors::White));

	Style->Set("Doc.Code", FTextBlockStyle(Body)
		.SetFont(FCoreStyle::GetDefaultFontStyle("Mono", 9))
		.SetColorAndOpacity(FStyleColors::AccentOrange));

	Style->Set("Doc.CodeBlock", FTextBlockStyle(Body)
		.SetFont(FCoreStyle::GetDefaultFontStyle("Mono", 9))
		.SetColorAndOpacity(FStyleColors::AccentGreen));

	Style->Set("Doc.Quote", FTextBlockStyle(Body)
		.SetFont(FCoreStyle::GetDefaultFontStyle("Italic", 10))
		.SetColorAndOpacity(FStyleColors::ForegroundHover));

	Style->Set("Doc.H1", FTextBlockStyle(Body)
		.SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 18))
		.SetColorAndOpacity(FStyleColors::White));

	Style->Set("Doc.H2", FTextBlockStyle(Body)
		.SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 14))
		.SetColorAndOpacity(FStyleColors::White));

	Style->Set("Doc.H3", FTextBlockStyle(Body)
		.SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 11))
		.SetColorAndOpacity(FStyleColors::White));

	// Hyperlink style consumed by SRichTextBlock::HyperlinkDecorator. The underline button is
	// deliberately chrome-free so links read as text rather than as buttons.
	const FButtonStyle LinkButton = FButtonStyle()
		.SetNormal(FSlateNoResource())
		.SetHovered(FSlateNoResource())
		.SetPressed(FSlateNoResource())
		.SetNormalPadding(FMargin(0))
		.SetPressedPadding(FMargin(0));

	Style->Set("Hyperlink", FHyperlinkStyle()
		.SetUnderlineStyle(LinkButton)
		.SetTextStyle(FTextBlockStyle(Body).SetColorAndOpacity(FStyleColors::AccentBlue))
		.SetPadding(FMargin(0)));

	// Backing panel for fenced code blocks.
	Style->Set("Doc.CodeBlock.Background", new FSlateColorBrush(FStyleColors::Recessed));

	return Style;
}
