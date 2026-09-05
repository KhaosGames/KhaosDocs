// Copyright Khaos Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateStyle.h"

/**
 * Style set for rendered markdown. Built entirely in code from the default engine fonts and
 * theme colours, so the plugin ships no .uasset and no loose art dependency.
 *
 * The style names double as rich-text markup tags: the markdown converter emits
 * <Doc.Bold>text</> and this style set is handed to SRichTextBlock as its decorator style set.
 */
class FKhaosDocsStyle
{
public:
	static void Initialize();
	static void Shutdown();

	static const ISlateStyle& Get();
	static FName GetStyleSetName();

private:
	static TSharedRef<FSlateStyleSet> Create();
	static TSharedPtr<FSlateStyleSet> StyleInstance;
};
