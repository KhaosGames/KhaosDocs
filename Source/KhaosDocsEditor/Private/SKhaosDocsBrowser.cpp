// Copyright Khaos Games. All Rights Reserved.

#include "SKhaosDocsBrowser.h"

#include "HAL/PlatformProcess.h"
#include "KhaosDocsEditor.h"
#include "KhaosDocsMarkdown.h"
#include "KhaosDocsStyle.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STreeView.h"

#define LOCTEXT_NAMESPACE "KhaosDocsBrowser"

void SKhaosDocsBrowser::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SSplitter)
		.Orientation(Orient_Horizontal)

		// Table of contents.
		+ SSplitter::Slot()
		.Value(0.28f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4.0f)
			[
				SAssignNew(SearchBox, SSearchBox)
				.HintText(LOCTEXT("SearchHint", "Search documents..."))
				.OnTextChanged(this, &SKhaosDocsBrowser::OnFilterTextChanged)
			]

			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				[
					SAssignNew(TreeView, STreeView<TSharedPtr<FDocsTreeItem>>)
					.TreeItemsSource(&RootItems)
					.SelectionMode(ESelectionMode::Single)
					.OnGenerateRow(this, &SKhaosDocsBrowser::OnGenerateRow)
					.OnGetChildren(this, &SKhaosDocsBrowser::OnGetChildren)
					.OnSelectionChanged(this, &SKhaosDocsBrowser::OnSelectionChanged)
					.OnMouseButtonDoubleClick(this, &SKhaosDocsBrowser::OnMouseDoubleClick)
				]
			]
		]

		// Rendered document.
		+ SSplitter::Slot()
		.Value(0.72f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(8.0f, 6.0f)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text_Lambda([this]() { return SelectedTitle; })
					.TextStyle(&FKhaosDocsStyle::Get().GetWidgetStyle<FTextBlockStyle>("Doc.H2"))
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("OpenInEditor", "Open in Editor"))
					.ToolTipText(LOCTEXT("OpenInEditorTooltip", "Open this document in the Khaos document editor."))
					.OnClicked(this, &SKhaosDocsBrowser::OnOpenInEditorClicked)
					.IsEnabled_Lambda([this]() { return HasSelectedDocument(); })
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("OpenExternally", "Open Externally"))
					.ToolTipText(LOCTEXT("OpenExternallyTooltip", "Open the markdown file in your default external editor."))
					.OnClicked(this, &SKhaosDocsBrowser::OnOpenExternallyClicked)
					.IsEnabled_Lambda([this]() { return HasSelectedDocument(); })
				]
			]

			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				[
					SAssignNew(ContentScrollBox, SScrollBox)

					+ SScrollBox::Slot()
					.Padding(16.0f, 12.0f)
					[
						SAssignNew(ContentBlock, SRichTextBlock)
						.Text(LOCTEXT("NoSelection", "Select a document from the table of contents."))
						.TextStyle(&FKhaosDocsStyle::Get().GetWidgetStyle<FTextBlockStyle>("Doc.Body"))
						.DecoratorStyleSet(&FKhaosDocsStyle::Get())
						.Decorators({ SRichTextBlock::HyperlinkDecorator(
							TEXT("doclink"),
							FSlateHyperlinkRun::FOnClick::CreateSP(this, &SKhaosDocsBrowser::OnLinkClicked)) })
						.AutoWrapText(true)
					]
				]
			]
		]
	];

	Refresh();
}

void SKhaosDocsBrowser::Refresh()
{
	RescanDocuments();
	RebuildTree();
}

void SKhaosDocsBrowser::RescanDocuments()
{
	CachedRoots.Reset();

	TArray<KhaosDocs::FDocRoot> Roots;
	KhaosDocs::FindDocRoots(Roots);

	for (const KhaosDocs::FDocRoot& Root : Roots)
	{
		TArray<FString> Files;
		KhaosDocs::FindDocsInRoot(Root, Files);
		if (Files.Num() == 0)
		{
			continue;
		}

		Files.Sort();

		FCachedRoot& Cached = CachedRoots.AddDefaulted_GetRef();
		Cached.Owner = Root.Owner;
		Cached.VirtualPath = Root.VirtualPath;
		Cached.Docs.Reserve(Files.Num());

		for (const FString& File : Files)
		{
			FCachedDoc& Doc = Cached.Docs.AddDefaulted_GetRef();
			Doc.FilePath = File;
			Doc.Title = KhaosDocs::GetDocumentTitle(File);

			// Documents can live anywhere under the owner, so keep the containing folder to tell
			// two same-named files apart. Relative to the base folder rather than to Content, so
			// that "Content/UI" and "Docs" both read sensibly and a root-level README shows none.
			FString RelativeDir = FPaths::GetPath(File);
			if (FPaths::MakePathRelativeTo(RelativeDir, *(Root.BaseDir / TEXT(""))))
			{
				Doc.RelativeDir = MoveTemp(RelativeDir);
			}
		}
	}
}

void SKhaosDocsBrowser::RebuildTree()
{
	RootItems.Reset();

	for (const FCachedRoot& Cached : CachedRoots)
	{
		TSharedRef<FDocsTreeItem> Group = MakeShared<FDocsTreeItem>();
		Group->Label = Cached.Owner;
		Group->VirtualPath = Cached.VirtualPath;

		for (const FCachedDoc& Doc : Cached.Docs)
		{
			// Filtering matches the title and the file name, so both "Setup" and "setup.md" hit.
			if (!FilterText.IsEmpty()
				&& !Doc.Title.Contains(FilterText, ESearchCase::IgnoreCase)
				&& !FPaths::GetCleanFilename(Doc.FilePath).Contains(FilterText, ESearchCase::IgnoreCase))
			{
				continue;
			}

			TSharedRef<FDocsTreeItem> Item = MakeShared<FDocsTreeItem>();
			Item->Label = Doc.Title;
			Item->FilePath = Doc.FilePath;
			Item->VirtualPath = Cached.VirtualPath;
			Item->RelativeDir = Doc.RelativeDir;
			Group->Children.Add(Item);
		}

		if (Group->Children.Num() > 0)
		{
			RootItems.Add(Group);
		}
	}

	if (TreeView.IsValid())
	{
		TreeView->RequestTreeRefresh();
		ExpandAll();
	}
}

void SKhaosDocsBrowser::ExpandAll()
{
	for (const TSharedPtr<FDocsTreeItem>& Item : RootItems)
	{
		TreeView->SetItemExpansion(Item, true);
	}
}

TSharedRef<ITableRow> SKhaosDocsBrowser::OnGenerateRow(TSharedPtr<FDocsTreeItem> InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	const bool bIsDocument = InItem->IsDocument();
	const TCHAR* StyleName = bIsDocument ? TEXT("Doc.Body") : TEXT("Doc.Bold");
	const TCHAR* IconName = bIsDocument ? TEXT("Icons.Documentation") : TEXT("Icons.FolderClosed");

	return SNew(STableRow<TSharedPtr<FDocsTreeItem>>, OwnerTable)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(2.0f, 2.0f, 6.0f, 2.0f)
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush(IconName))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(InItem->Label))
				.ToolTipText(FText::FromString(bIsDocument ? InItem->FilePath : InItem->VirtualPath))
				.TextStyle(&FKhaosDocsStyle::Get().GetWidgetStyle<FTextBlockStyle>(FName(StyleName)))
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(6.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(InItem->RelativeDir))
				.ToolTipText(FText::FromString(bIsDocument ? InItem->FilePath : InItem->VirtualPath))
				.TextStyle(&FKhaosDocsStyle::Get().GetWidgetStyle<FTextBlockStyle>("Doc.Quote"))
				.Visibility(InItem->RelativeDir.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
			]
		];
}

void SKhaosDocsBrowser::OnGetChildren(TSharedPtr<FDocsTreeItem> InItem, TArray<TSharedPtr<FDocsTreeItem>>& OutChildren)
{
	OutChildren = InItem->Children;
}

void SKhaosDocsBrowser::OnSelectionChanged(TSharedPtr<FDocsTreeItem> InItem, ESelectInfo::Type SelectInfo)
{
	if (InItem.IsValid() && InItem->IsDocument())
	{
		ShowDocument(InItem->FilePath);
	}
}

void SKhaosDocsBrowser::OnMouseDoubleClick(TSharedPtr<FDocsTreeItem> InItem)
{
	if (InItem.IsValid() && InItem->IsDocument())
	{
		IKhaosDocsEditorModule::Get().OpenDocument(InItem->FilePath);
	}
	else if (InItem.IsValid() && TreeView.IsValid())
	{
		TreeView->SetItemExpansion(InItem, !TreeView->IsItemExpanded(InItem));
	}
}

void SKhaosDocsBrowser::OnFilterTextChanged(const FText& InText)
{
	FilterText = InText.ToString().TrimStartAndEnd();

	// Filter against the cache only - rescanning here would re-read every document from disk on
	// every keystroke.
	RebuildTree();
}

void SKhaosDocsBrowser::ShowDocument(const FString& InFilePath)
{
	SelectedFilePath = InFilePath;

	FString Raw;
	if (!FFileHelper::LoadFileToString(Raw, *InFilePath))
	{
		SelectedTitle = LOCTEXT("UnreadableTitle", "Unreadable document");
		if (ContentBlock.IsValid())
		{
			ContentBlock->SetText(FText::Format(
				LOCTEXT("Unreadable", "Could not read {0}."),
				FText::FromString(InFilePath)));
		}
		return;
	}

	const KhaosDocsMarkdown::FDocument Parsed = KhaosDocsMarkdown::Parse(Raw);

	SelectedTitle = FText::FromString(Parsed.Title.IsEmpty()
		? FPaths::GetBaseFilename(InFilePath)
		: Parsed.Title);

	if (ContentBlock.IsValid())
	{
		ContentBlock->SetText(Parsed.RichText);
	}

	if (ContentScrollBox.IsValid())
	{
		ContentScrollBox->ScrollToStart();
	}
}

void SKhaosDocsBrowser::OnLinkClicked(const FSlateHyperlinkRun::FMetadata& InMetadata)
{
	const FString* Href = InMetadata.Find(TEXT("href"));
	if (!Href || Href->IsEmpty())
	{
		return;
	}

	if (Href->StartsWith(TEXT("http://"), ESearchCase::IgnoreCase)
		|| Href->StartsWith(TEXT("https://"), ESearchCase::IgnoreCase))
	{
		FPlatformProcess::LaunchURL(**Href, nullptr, nullptr);
		return;
	}

	if (SelectedFilePath.IsEmpty())
	{
		return;
	}

	FString Target = *Href;
	int32 FragmentIndex = INDEX_NONE;
	if (Target.FindChar(TEXT('#'), FragmentIndex))
	{
		Target.LeftInline(FragmentIndex);
	}

	if (Target.IsEmpty())
	{
		return;
	}

	// Relative links navigate inside the browser rather than opening a separate editor window.
	const FString Resolved = FPaths::ConvertRelativePathToFull(FPaths::GetPath(SelectedFilePath) / Target);
	if (FPaths::FileExists(Resolved))
	{
		ShowDocument(Resolved);
	}
}

FReply SKhaosDocsBrowser::OnOpenInEditorClicked()
{
	if (!SelectedFilePath.IsEmpty())
	{
		IKhaosDocsEditorModule::Get().OpenDocument(SelectedFilePath);
	}
	return FReply::Handled();
}

FReply SKhaosDocsBrowser::OnOpenExternallyClicked()
{
	if (!SelectedFilePath.IsEmpty())
	{
		FPlatformProcess::LaunchFileInDefaultExternalApplication(*SelectedFilePath, nullptr, ELaunchVerb::Edit);
	}
	return FReply::Handled();
}

bool SKhaosDocsBrowser::HasSelectedDocument() const
{
	return !SelectedFilePath.IsEmpty();
}

#undef LOCTEXT_NAMESPACE
