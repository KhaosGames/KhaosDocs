// Copyright Khaos Systems. All Rights Reserved.

#include "KhaosDocsMarkdown.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace KhaosDocsMarkdown;

BEGIN_DEFINE_SPEC(FKhaosDocsMarkdownSpec, "KhaosDocs.Markdown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	FDocument Document;

	/** Parse, and check the block count first so the cases can index blocks without going out of range. */
	bool ParseExpecting(const TCHAR* InMarkdown, const int32 InExpectedBlocks)
	{
		Document = Parse(InMarkdown);
		return TestEqual(TEXT("Block count"), Document.Blocks.Num(), InExpectedBlocks);
	}

	void ExpectBlock(const int32 InIndex, const EBlockType InType, const FString& InRichText)
	{
		const FBlock& Block = Document.Blocks[InIndex];
		TestEqual(FString::Printf(TEXT("Block %d type"), InIndex), static_cast<int32>(Block.Type), static_cast<int32>(InType));
		TestEqual(FString::Printf(TEXT("Block %d text"), InIndex), Block.RichText, InRichText);
	}

	/** Outline text of a document with exactly one heading. Indexing an empty array would take the editor down, not fail the test. */
	FString OnlyHeading(const TCHAR* InMarkdown)
	{
		Document = Parse(InMarkdown);
		return TestEqual(TEXT("Heading count"), Document.Headings.Num(), 1) ? Document.Headings[0].Text : FString();
	}

	/** Rich text of a document that is a single paragraph. */
	FString Inline(const TCHAR* InMarkdown)
	{
		const FDocument Single = Parse(InMarkdown);
		return Single.Blocks.Num() == 1 ? Single.Blocks[0].RichText : FString();
	}

END_DEFINE_SPEC(FKhaosDocsMarkdownSpec)

void FKhaosDocsMarkdownSpec::Define()
{
	Describe("Headings", [this]
	{
		It("reads the level from the number of hashes", [this]
		{
			if (ParseExpecting(TEXT("# One\n## Two\n###### Six"), 3))
			{
				TestEqual(TEXT("Level"), Document.Blocks[0].Level, 1);
				TestEqual(TEXT("Level"), Document.Blocks[1].Level, 2);
				TestEqual(TEXT("Level"), Document.Blocks[2].Level, 6);
				ExpectBlock(2, EBlockType::Heading, TEXT("Six"));
			}
		});

		It("needs a space after the hashes", [this]
		{
			if (ParseExpecting(TEXT("#hashtag"), 1))
			{
				ExpectBlock(0, EBlockType::Paragraph, TEXT("#hashtag"));
			}
		});

		It("takes the title from the first level one heading", [this]
		{
			Document = Parse(TEXT("## Intro\n# Title\n# Second"));
			TestEqual(TEXT("Title"), Document.Title, FString(TEXT("Title")));
		});

		It("drops closing hashes", [this]
		{
			TestEqual(TEXT("Outline text"), OnlyHeading(TEXT("## Title ##")), FString(TEXT("Title")));
		});

		It("keeps a hash that is part of the last word", [this]
		{
			TestEqual(TEXT("Outline text"), OnlyHeading(TEXT("# Scripting in C#")), FString(TEXT("Scripting in C#")));
		});

		It("strips inline markers from the outline", [this]
		{
			TestEqual(TEXT("Outline text"), OnlyHeading(TEXT("## The **bold** `code`")), FString(TEXT("The bold code")));
		});

		It("points each outline entry at its block", [this]
		{
			Document = Parse(TEXT("Intro\n\n# A\n\nText\n\n## B"));
			if (TestEqual(TEXT("Heading count"), Document.Headings.Num(), 2))
			{
				TestEqual(TEXT("First heading block"), Document.Headings[0].BlockIndex, 1);
				TestEqual(TEXT("Second heading block"), Document.Headings[1].BlockIndex, 3);
			}
		});
	});

	Describe("Inline markup", [this]
	{
		It("converts strong, emphasis and code", [this]
		{
			TestEqual(TEXT("Strong"), Inline(TEXT("**a**")), FString(TEXT("<Doc.Bold>a</>")));
			TestEqual(TEXT("Emphasis"), Inline(TEXT("*a*")), FString(TEXT("<Doc.Italic>a</>")));
			TestEqual(TEXT("Code"), Inline(TEXT("`a`")), FString(TEXT("<Doc.Code>a</>")));
		});

		It("leaves underscores inside identifiers alone", [this]
		{
			TestEqual(TEXT("snake_case"), Inline(TEXT("call some_function_name now")), FString(TEXT("call some_function_name now")));
		});

		It("treats a backslash-escaped marker as text", [this]
		{
			TestEqual(TEXT("Escaped"), Inline(TEXT("\\*not emphasis\\*")), FString(TEXT("*not emphasis*")));
		});

		It("does not format inside code spans", [this]
		{
			TestEqual(TEXT("Code span"), Inline(TEXT("`**a** <T>`")), FString(TEXT("<Doc.Code>**a** &lt;T&gt;</>")));
		});

		It("converts links", [this]
		{
			TestEqual(TEXT("Link"), Inline(TEXT("see [the docs](Other.md)")), FString(TEXT("see <a id=\"doclink\" href=\"Other.md\">the docs</>")));
		});

		It("escapes text that would read as rich-text markup", [this]
		{
			TestEqual(TEXT("Escaped"), Inline(TEXT("a < b & \"c\" > d")), FString(TEXT("a &lt; b &amp; &quot;c&quot; &gt; d")));
		});
	});

	Describe("Blocks", [this]
	{
		It("joins consecutive lines into one paragraph", [this]
		{
			if (ParseExpecting(TEXT("one\ntwo\n\nthree"), 2))
			{
				ExpectBlock(0, EBlockType::Paragraph, TEXT("one two"));
				ExpectBlock(1, EBlockType::Paragraph, TEXT("three"));
			}
		});

		It("keeps fenced code verbatim, blank lines included", [this]
		{
			if (ParseExpecting(TEXT("```cpp\nint X = 1; // **not bold**\n\nreturn X;\n```"), 1))
			{
				TestEqual(TEXT("Type"), static_cast<int32>(Document.Blocks[0].Type), static_cast<int32>(EBlockType::CodeBlock));
				TestEqual(TEXT("Code"), Document.Blocks[0].Code, FString(TEXT("int X = 1; // **not bold**\n\nreturn X;")));
			}
		});

		It("reads four-space indented code after a blank line", [this]
		{
			if (ParseExpecting(TEXT("Text\n\n    first\n    second"), 2))
			{
				TestEqual(TEXT("Code"), Document.Blocks[1].Code, FString(TEXT("first\nsecond")));
			}
		});

		It("nests bullets by two spaces of indent", [this]
		{
			if (ParseExpecting(TEXT("- a\n  - b\n- c"), 3))
			{
				ExpectBlock(1, EBlockType::ListItem, TEXT("b"));
				TestEqual(TEXT("Depth"), Document.Blocks[0].Level, 0);
				TestEqual(TEXT("Depth"), Document.Blocks[1].Level, 1);
				TestEqual(TEXT("Depth"), Document.Blocks[2].Level, 0);
				TestTrue(TEXT("Bullets have no marker"), Document.Blocks[0].Marker.IsEmpty());
			}
		});

		It("keeps the author's numbering in ordered lists", [this]
		{
			if (ParseExpecting(TEXT("3. three\n4) four"), 2))
			{
				TestEqual(TEXT("Marker"), Document.Blocks[0].Marker, FString(TEXT("3.")));
				TestEqual(TEXT("Marker"), Document.Blocks[1].Marker, FString(TEXT("4.")));
				ExpectBlock(1, EBlockType::ListItem, TEXT("four"));
			}
		});

		It("continues a list item on a wrapped line", [this]
		{
			if (ParseExpecting(TEXT("- first part\n  second part\n- next"), 2))
			{
				ExpectBlock(0, EBlockType::ListItem, TEXT("first part second part"));
			}
		});

		It("merges consecutive quote lines", [this]
		{
			if (ParseExpecting(TEXT("> a\n> b"), 1))
			{
				ExpectBlock(0, EBlockType::Quote, TEXT("a b"));
			}
		});

		It("reads a run of dashes or stars as a rule", [this]
		{
			if (ParseExpecting(TEXT("---\n\n***"), 2))
			{
				TestEqual(TEXT("Type"), static_cast<int32>(Document.Blocks[0].Type), static_cast<int32>(EBlockType::Rule));
				TestEqual(TEXT("Type"), static_cast<int32>(Document.Blocks[1].Type), static_cast<int32>(EBlockType::Rule));
			}
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
