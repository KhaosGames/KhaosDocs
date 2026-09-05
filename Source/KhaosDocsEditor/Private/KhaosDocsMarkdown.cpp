// Copyright Khaos Games. All Rights Reserved.

#include "KhaosDocsMarkdown.h"

namespace KhaosDocsMarkdown
{

FString EscapeRichText(const FString& InText)
{
	FString Out = InText;
	// Ampersand must be replaced first or it would double-escape the entities added below.
	Out.ReplaceInline(TEXT("&"), TEXT("&amp;"), ESearchCase::CaseSensitive);
	Out.ReplaceInline(TEXT("<"), TEXT("&lt;"), ESearchCase::CaseSensitive);
	Out.ReplaceInline(TEXT(">"), TEXT("&gt;"), ESearchCase::CaseSensitive);
	Out.ReplaceInline(TEXT("\""), TEXT("&quot;"), ESearchCase::CaseSensitive);
	return Out;
}

namespace
{

/** Strip inline markers so a heading reads cleanly in the outline tree. */
FString StripInlineMarkup(const FString& InText)
{
	FString Out = InText;
	Out.ReplaceInline(TEXT("**"), TEXT(""), ESearchCase::CaseSensitive);
	Out.ReplaceInline(TEXT("__"), TEXT(""), ESearchCase::CaseSensitive);
	Out.ReplaceInline(TEXT("`"), TEXT(""), ESearchCase::CaseSensitive);
	return Out.TrimStartAndEnd();
}

/**
 * Decide whether a delimiter run should actually open and close emphasis.
 *
 * CommonMark forbids underscore emphasis inside a word, which matters a lot here: without this,
 * identifiers like snake_case_name and TEXT_MACRO_NAME silently render as italics with the
 * underscores eaten. Asterisks have no such restriction.
 */
bool IsValidDelimiterRun(const FString& InLine, const TCHAR InDelimiter, const int32 InOpenIndex, const int32 InCloseIndex, const int32 InRunLength)
{
	if (InDelimiter != TEXT('_'))
	{
		return true;
	}

	const int32 BeforeOpen = InOpenIndex - 1;
	if (BeforeOpen >= 0 && FChar::IsAlnum(InLine[BeforeOpen]))
	{
		return false;
	}

	const int32 AfterClose = InCloseIndex + InRunLength;
	if (AfterClose < InLine.Len() && FChar::IsAlnum(InLine[AfterClose]))
	{
		return false;
	}

	return true;
}

/**
 * Convert the inline span markers of a single line into rich-text markup.
 * Precedence follows CommonMark loosely: code spans win outright (no nested formatting),
 * then links, then strong, then emphasis.
 */
FString ParseInline(const FString& InLine)
{
	FString Out;
	FString Pending;
	const int32 Len = InLine.Len();
	int32 Index = 0;

	auto FlushPending = [&Out, &Pending]()
	{
		if (!Pending.IsEmpty())
		{
			Out += EscapeRichText(Pending);
			Pending.Reset();
		}
	};

	while (Index < Len)
	{
		const TCHAR Char = InLine[Index];

		// Backslash escape - the next character is always literal.
		if (Char == TEXT('\\') && Index + 1 < Len)
		{
			Pending.AppendChar(InLine[Index + 1]);
			Index += 2;
			continue;
		}

		// Inline code span.
		if (Char == TEXT('`'))
		{
			const int32 End = InLine.Find(TEXT("`"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Index + 1);
			if (End != INDEX_NONE)
			{
				FlushPending();
				Out += FString::Printf(TEXT("<Doc.Code>%s</>"), *EscapeRichText(InLine.Mid(Index + 1, End - Index - 1)));
				Index = End + 1;
				continue;
			}
		}

		// Link: [text](url)
		if (Char == TEXT('['))
		{
			const int32 CloseBracket = InLine.Find(TEXT("]"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Index + 1);
			if (CloseBracket != INDEX_NONE && CloseBracket + 1 < Len && InLine[CloseBracket + 1] == TEXT('('))
			{
				const int32 CloseParen = InLine.Find(TEXT(")"), ESearchCase::CaseSensitive, ESearchDir::FromStart, CloseBracket + 2);
				if (CloseParen != INDEX_NONE)
				{
					const FString LinkText = InLine.Mid(Index + 1, CloseBracket - Index - 1);
					const FString Url = InLine.Mid(CloseBracket + 2, CloseParen - CloseBracket - 2);

					FlushPending();
					Out += FString::Printf(
						TEXT("<a id=\"doclink\" href=\"%s\">%s</>"),
						*EscapeRichText(Url),
						*EscapeRichText(LinkText.IsEmpty() ? Url : LinkText));
					Index = CloseParen + 1;
					continue;
				}
			}
		}

		// Strong: **text** or __text__
		if ((Char == TEXT('*') || Char == TEXT('_')) && Index + 1 < Len && InLine[Index + 1] == Char)
		{
			const FString Delimiter = FString::ChrN(2, Char);
			const int32 End = InLine.Find(Delimiter, ESearchCase::CaseSensitive, ESearchDir::FromStart, Index + 2);
			if (End != INDEX_NONE && End > Index + 2 && IsValidDelimiterRun(InLine, Char, Index, End, 2))
			{
				FlushPending();
				Out += FString::Printf(TEXT("<Doc.Bold>%s</>"), *EscapeRichText(InLine.Mid(Index + 2, End - Index - 2)));
				Index = End + 2;
				continue;
			}
		}

		// Emphasis: *text* or _text_
		if (Char == TEXT('*') || Char == TEXT('_'))
		{
			const FString Delimiter = FString::ChrN(1, Char);
			const int32 End = InLine.Find(Delimiter, ESearchCase::CaseSensitive, ESearchDir::FromStart, Index + 1);
			if (End != INDEX_NONE && End > Index + 1 && IsValidDelimiterRun(InLine, Char, Index, End, 1))
			{
				FlushPending();
				Out += FString::Printf(TEXT("<Doc.Italic>%s</>"), *EscapeRichText(InLine.Mid(Index + 1, End - Index - 1)));
				Index = End + 1;
				continue;
			}
		}

		Pending.AppendChar(Char);
		++Index;
	}

	FlushPending();
	return Out;
}

/** Number of leading spaces, counting a tab as four. */
int32 MeasureIndent(const FString& InLine)
{
	int32 Indent = 0;
	for (int32 Index = 0; Index < InLine.Len(); ++Index)
	{
		if (InLine[Index] == TEXT(' '))
		{
			++Indent;
		}
		else if (InLine[Index] == TEXT('\t'))
		{
			Indent += 4;
		}
		else
		{
			break;
		}
	}
	return Indent;
}

bool IsHorizontalRule(const FString& InTrimmed)
{
	if (InTrimmed.Len() < 3)
	{
		return false;
	}

	const TCHAR First = InTrimmed[0];
	if (First != TEXT('-') && First != TEXT('*') && First != TEXT('_'))
	{
		return false;
	}

	for (int32 Index = 0; Index < InTrimmed.Len(); ++Index)
	{
		if (InTrimmed[Index] != First)
		{
			return false;
		}
	}
	return true;
}

} // anonymous namespace

FDocument Parse(const FString& InMarkdown)
{
	FDocument Document;

	TArray<FString> Lines;
	InMarkdown.ParseIntoArrayLines(Lines, /*bCullEmpty*/ false);

	FString Out;
	bool bInFence = false;
	bool bPreviousLineWasBlank = true;

	auto AppendBlock = [&Out, &bPreviousLineWasBlank](const FString& InBlock)
	{
		if (!Out.IsEmpty())
		{
			Out += LINE_TERMINATOR;
		}
		Out += InBlock;
		bPreviousLineWasBlank = false;
	};

	for (int32 LineIndex = 0; LineIndex < Lines.Num(); ++LineIndex)
	{
		const FString& Line = Lines[LineIndex];
		const FString Trimmed = Line.TrimStartAndEnd();

		// Fenced code block delimiters. The info string (```cpp) is dropped.
		if (Trimmed.StartsWith(TEXT("```"), ESearchCase::CaseSensitive)
			|| Trimmed.StartsWith(TEXT("~~~"), ESearchCase::CaseSensitive))
		{
			bInFence = !bInFence;
			continue;
		}

		if (bInFence)
		{
			AppendBlock(FString::Printf(TEXT("<Doc.CodeBlock>    %s</>"), *EscapeRichText(Line)));
			continue;
		}

		if (Trimmed.IsEmpty())
		{
			// Collapse runs of blank lines into a single paragraph break.
			if (!bPreviousLineWasBlank && !Out.IsEmpty())
			{
				Out += LINE_TERMINATOR;
				bPreviousLineWasBlank = true;
			}
			continue;
		}

		// Indented code block (four spaces), only when it does not continue a paragraph.
		if (MeasureIndent(Line) >= 4 && bPreviousLineWasBlank)
		{
			AppendBlock(FString::Printf(TEXT("<Doc.CodeBlock>%s</>"), *EscapeRichText(Line)));
			continue;
		}

		if (IsHorizontalRule(Trimmed))
		{
			AppendBlock(TEXT("<Doc.Quote>\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500</>"));
			continue;
		}

		// ATX heading.
		if (Trimmed.StartsWith(TEXT("#"), ESearchCase::CaseSensitive))
		{
			int32 Level = 0;
			while (Level < Trimmed.Len() && Trimmed[Level] == TEXT('#'))
			{
				++Level;
			}

			// "#Foo" is not a heading in CommonMark - a space is required.
			if (Level <= 6 && Level < Trimmed.Len() && Trimmed[Level] == TEXT(' '))
			{
				const FString HeadingText = Trimmed.RightChop(Level).TrimStartAndEnd();
				const int32 ClampedLevel = FMath::Clamp(Level, 1, 3);

				FHeading& Heading = Document.Headings.AddDefaulted_GetRef();
				Heading.Level = Level;
				Heading.Text = StripInlineMarkup(HeadingText);
				Heading.LineIndex = LineIndex;

				if (Level == 1 && Document.Title.IsEmpty())
				{
					Document.Title = Heading.Text;
				}

				AppendBlock(FString::Printf(TEXT("<Doc.H%d>%s</>"), ClampedLevel, *ParseInline(HeadingText)));
				continue;
			}
		}

		// Block quote.
		if (Trimmed.StartsWith(TEXT(">"), ESearchCase::CaseSensitive))
		{
			const FString QuoteText = Trimmed.RightChop(1).TrimStart();
			AppendBlock(FString::Printf(TEXT("<Doc.Quote>\x2502 %s</>"), *ParseInline(QuoteText)));
			continue;
		}

		// Bullet list. Nesting is rendered by preserving the source indent.
		if (Trimmed.StartsWith(TEXT("- "), ESearchCase::CaseSensitive)
			|| Trimmed.StartsWith(TEXT("* "), ESearchCase::CaseSensitive)
			|| Trimmed.StartsWith(TEXT("+ "), ESearchCase::CaseSensitive))
		{
			const int32 Depth = MeasureIndent(Line) / 2;
			const FString Indent = FString::ChrN(Depth * 4 + 4, TEXT(' '));
			AppendBlock(FString::Printf(TEXT("%s\x2022 %s"), *Indent, *ParseInline(Trimmed.RightChop(2))));
			continue;
		}

		// Ordered list - keep the author's numbering rather than renumbering.
		{
			int32 DigitCount = 0;
			while (DigitCount < Trimmed.Len() && FChar::IsDigit(Trimmed[DigitCount]))
			{
				++DigitCount;
			}

			if (DigitCount > 0 && DigitCount + 1 < Trimmed.Len()
				&& Trimmed[DigitCount] == TEXT('.') && Trimmed[DigitCount + 1] == TEXT(' '))
			{
				const int32 Depth = MeasureIndent(Line) / 2;
				const FString Indent = FString::ChrN(Depth * 4 + 4, TEXT(' '));
				AppendBlock(FString::Printf(
					TEXT("%s%s. %s"),
					*Indent,
					*Trimmed.Left(DigitCount),
					*ParseInline(Trimmed.RightChop(DigitCount + 2))));
				continue;
			}
		}

		// Plain paragraph. Consecutive non-blank lines are joined into one wrapped paragraph.
		if (bPreviousLineWasBlank || Out.IsEmpty())
		{
			AppendBlock(ParseInline(Trimmed));
		}
		else
		{
			Out += TEXT(" ");
			Out += ParseInline(Trimmed);
		}
	}

	Document.RichText = FText::FromString(Out);
	return Document;
}

} // namespace KhaosDocsMarkdown
