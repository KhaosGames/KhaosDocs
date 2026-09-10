// Copyright Khaos Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace KhaosDocsMarkdown
{
	enum class EBlockType : uint8
	{
		Heading,
		Paragraph,
		CodeBlock,
		Quote,
		ListItem,
		Rule,
	};

	/**
	 * One block-level element of a document.
	 *
	 * The renderer builds a widget per block, which is what gives code blocks a background, quotes
	 * a bar, lists real indentation, and the outline something to scroll to. A single rich-text run
	 * for the whole file could do none of those.
	 */
	struct FBlock
	{
		EBlockType Type = EBlockType::Paragraph;

		/** Inline content as Slate rich-text markup. Empty for code blocks and rules. */
		FString RichText;

		/** Verbatim text of a code block, lines joined with newlines. */
		FString Code;

		/** Heading level 1-6, or list nesting depth starting at 0. */
		int32 Level = 0;

		/** List items only: the marker to draw, "1." for ordered lists, empty for bullets. */
		FString Marker;
	};

	/** A heading discovered while parsing, used to build the per-document outline. */
	struct FHeading
	{
		int32 Level = 1;

		/** Heading text with inline markers stripped. */
		FString Text;

		/** Index into FDocument::Blocks of the heading block. */
		int32 BlockIndex = 0;
	};

	struct FDocument
	{
		/** First H1 in the file, or empty if the file has none. */
		FString Title;

		TArray<FBlock> Blocks;
		TArray<FHeading> Headings;
	};

	/**
	 * Convert a markdown string to a list of blocks with rich-text inline content.
	 *
	 * Supported: ATX headings, fenced and indented code blocks, inline code, bold, italic,
	 * bullet and ordered lists, block quotes, horizontal rules, and [text](url) links.
	 * Unsupported constructs degrade to plain text rather than erroring.
	 */
	FDocument Parse(const FString& InMarkdown);

	/** Escape a plain string so it survives the rich-text markup parser verbatim. */
	FString EscapeRichText(const FString& InText);
}
