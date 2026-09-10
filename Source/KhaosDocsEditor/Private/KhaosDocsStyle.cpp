// Copyright Khaos Games. All Rights Reserved.

#include "KhaosDocsStyle.h"

#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateImageBrush.h"
#include "Brushes/SlateNoResource.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Interfaces/IPluginManager.h"
#include "KhaosDocsDocument.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateStyleMacros.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateTypes.h"
#include "Styling/StyleColors.h"

// Required by the IMAGE_BRUSH_SVG macro, which resolves paths against the style set's content root.
#define RootToContentDir Style->RootToContentDir

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

const FTextBlockStyle& FKhaosDocsStyle::GetText(const FName InName)
{
	return Get().GetWidgetStyle<FTextBlockStyle>(InName);
}

TSharedRef<FSlateStyleSet> FKhaosDocsStyle::Create()
{
	TSharedRef<FSlateStyleSet> Style = MakeShared<FSlateStyleSet>(GetStyleSetName());

	if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("KhaosDocs")))
	{
		Style->SetContentRoot(Plugin->GetBaseDir() / TEXT("Resources"));

		const FVector2D Icon16x16(16.0f, 16.0f);
		const FVector2D Icon64x64(64.0f, 64.0f);

		// The Content Browser only shows a class thumbnail when the item's type resolves to a real
		// UClass (SAssetThumbnail::GetClassThumbnailVisibility), so the file type is registered
		// as UKhaosDocsDocument and the brushes are keyed by that class name. FSlateIconFinder
		// searches every registered style set, so they can live here rather than in the app style.
		const FString ClassName = UKhaosDocsDocument::StaticClass()->GetName();
		Style->Set(*FString::Printf(TEXT("ClassThumbnail.%s"), *ClassName), new IMAGE_BRUSH_SVG(TEXT("DocumentThumbnail"), Icon64x64));
		Style->Set(*FString::Printf(TEXT("ClassIcon.%s"), *ClassName), new IMAGE_BRUSH_SVG(TEXT("Document"), Icon16x16));

		// Mark used by the toolbar button, the documentation tab and the table of contents rows.
		Style->Set("KhaosDocs.Icon", new IMAGE_BRUSH_SVG(TEXT("Document"), Icon16x16));
		Style->Set("KhaosDocs.Icon.Large", new IMAGE_BRUSH_SVG(TEXT("DocumentThumbnail"), Icon64x64));

		// Pane toggles: a frame with the side being toggled filled in.
		Style->Set("KhaosDocs.PanelLeft", new IMAGE_BRUSH_SVG(TEXT("PanelLeft"), Icon16x16));
		Style->Set("KhaosDocs.PanelRight", new IMAGE_BRUSH_SVG(TEXT("PanelRight"), Icon16x16));
	}

	// -- Rendered document -----------------------------------------------------------------------

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
		.SetColorAndOpacity(FStyleColors::ForegroundHover));

	Style->Set("Doc.Quote", FTextBlockStyle(Body)
		.SetFont(FCoreStyle::GetDefaultFontStyle("Italic", 10))
		.SetColorAndOpacity(FStyleColors::ForegroundHover));

	Style->Set("Doc.ListMarker", FTextBlockStyle(Body)
		.SetColorAndOpacity(FStyleColors::ForegroundHover));

	Style->Set("Doc.H1", FTextBlockStyle(Body)
		.SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 20))
		.SetColorAndOpacity(FStyleColors::White));

	Style->Set("Doc.H2", FTextBlockStyle(Body)
		.SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 15))
		.SetColorAndOpacity(FStyleColors::White));

	Style->Set("Doc.H3", FTextBlockStyle(Body)
		.SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 12))
		.SetColorAndOpacity(FStyleColors::White));

	Style->Set("Doc.H4", FTextBlockStyle(Body)
		.SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 10))
		.SetColorAndOpacity(FStyleColors::ForegroundHeader));

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

	// Documents render on the recessed panel colour, so blocks that need to stand out are lifted
	// to the ordinary panel colour rather than pushed further back.
	Style->Set("Doc.CodeBlock.Background", new FSlateRoundedBoxBrush(FStyleColors::Panel, 4.0f));
	Style->Set("Doc.Quote.Bar", new FSlateRoundedBoxBrush(FStyleColors::Hover, 1.5f));
	Style->Set("Doc.Separator", new FSlateColorBrush(FStyleColors::Hover));

	// -- Window chrome ---------------------------------------------------------------------------

	Style->Set("Doc.Title", FTextBlockStyle(Body)
		.SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 13))
		.SetColorAndOpacity(FStyleColors::White));

	Style->Set("Doc.Subtitle", FTextBlockStyle(Body)
		.SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 8))
		.SetColorAndOpacity(FSlateColor::UseSubduedForeground()));

	Style->Set("Doc.Empty.Title", FTextBlockStyle(Body)
		.SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 12))
		.SetColorAndOpacity(FStyleColors::ForegroundHover));

	Style->Set("Doc.Empty.Body", FTextBlockStyle(Body)
		.SetColorAndOpacity(FSlateColor::UseSubduedForeground()));

	Style->Set("Doc.Tree.Owner", FTextBlockStyle(Body)
		.SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 10))
		.SetColorAndOpacity(FStyleColors::ForegroundHeader));

	Style->Set("Doc.Tree.Folder", FTextBlockStyle(Body)
		.SetColorAndOpacity(FStyleColors::ForegroundHeader));

	Style->Set("Doc.Tree.Item", Body);

	Style->Set("Doc.Tree.Secondary", FTextBlockStyle(Body)
		.SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 8))
		.SetColorAndOpacity(FSlateColor::UseSubduedForeground()));

	Style->Set("Doc.Source", FTextBlockStyle(Body)
		.SetFont(FCoreStyle::GetDefaultFontStyle("Mono", 9)));

	return Style;
}

// Keep the macro out of other translation units in a unity build.
#undef RootToContentDir
