// Copyright Khaos Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace KhaosDocsMarkdown
{
	/** A heading discovered while parsing, used to build the per-document outline. */
	struct FHeading
	{
		int32 Level = 1;
		FString Text;

		/** Index of the line the heading was found on, for scroll-to-section. */
		int32 LineIndex = 0;
	};

	struct FDocument
	{
		/** First H1 in the file, or empty if the file has none. */
		FString Title;

		/** The whole document converted to Slate rich-text markup. */
		FText RichText;

		TArray<FHeading> Headings;
	};

	/**
	 * Convert a markdown string to Slate rich-text markup.
	 *
	 * Supported: ATX headings, fenced and indented code blocks, inline code, bold, italic,
	 * bullet and ordered lists, block quotes, horizontal rules, and [text](url) links.
	 * Unsupported constructs degrade to plain text rather than erroring.
	 */
	FDocument Parse(const FString& InMarkdown);

	/** Escape a plain string so it survives the rich-text markup parser verbatim. */
	FString EscapeRichText(const FString& InText);
}
