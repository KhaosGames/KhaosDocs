// Copyright Khaos Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateStyle.h"

struct FTextBlockStyle;

/**
 * Style set for rendered markdown and the documentation window chrome. Built entirely in code
 * from the default engine fonts and theme colours, so the plugin ships no .uasset and no loose
 * art dependency beyond two SVG icons.
 *
 * The text style names double as rich-text markup tags: the markdown converter emits
 * <Doc.Bold>text</> and this style set is handed to SRichTextBlock as its decorator style set.
 */
class FKhaosDocsStyle
{
public:
	static void Initialize();
	static void Shutdown();

	static const ISlateStyle& Get();
	static FName GetStyleSetName();

	/** Shorthand for looking up one of the Doc.* text styles. */
	static const FTextBlockStyle& GetText(const FName InName);

	/** Height of the strip above each pane of the documentation window, so the two line up. */
	static constexpr float HeaderHeight = 48.0f;

	/** Widest a rendered document column gets; past this the margins grow instead. */
	static constexpr float DocumentMaxWidth = 880.0f;

private:
	static TSharedRef<FSlateStyleSet> Create();
	static TSharedPtr<FSlateStyleSet> StyleInstance;
};
