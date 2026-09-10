// Copyright Khaos Games. All Rights Reserved.

#include "SKhaosDocsBrowser.h"

#include "ContentBrowserDataFilter.h"
#include "ContentBrowserDataSubsystem.h"
#include "ContentBrowserItem.h"
#include "ContentBrowserModule.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/PlatformApplicationMisc.h"
#include "HAL/PlatformProcess.h"
#include "IContentBrowserDataModule.h"
#include "IContentBrowserSingleton.h"
#include "KhaosDocsEditor.h"
#include "KhaosDocsStyle.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "SKhaosDocsDocumentEditor.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STreeView.h"

#define LOCTEXT_NAMESPACE "KhaosDocsBrowser"

namespace
{
	const TCHAR* SettingsSection = TEXT("KhaosDocs");
	const TCHAR* FavoritesKey = TEXT("Favorites");
	const TCHAR* LastDocumentKey = TEXT("LastDocument");

	/** Settings store project-relative paths, so a project moved on disk keeps them. */
	FString ToStoredPath(const FString& InFullPath)
	{
		FString Path = InFullPath;
		FPaths::MakePathRelativeTo(Path, *FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));
		return Path;
	}

	FString FromStoredPath(const FString& InStoredPath)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()) / InStoredPath);
	}

	/** Folders first, then documents; each group alphabetical. Applied recursively. */
	void SortChildren(FDocsTreeItem& InItem)
	{
		InItem.Children.Sort([](const TSharedPtr<FDocsTreeItem>& A, const TSharedPtr<FDocsTreeItem>& B)
		{
			if (A->IsDocument() != B->IsDocument())
			{
				return !A->IsDocument();
			}
			return A->Label.Compare(B->Label, ESearchCase::IgnoreCase) < 0;
		});

		for (const TSharedPtr<FDocsTreeItem>& Child : InItem.Children)
		{
			SortChildren(*Child);
		}
	}

	/**
	 * Merge chains of single-child folders into one node labelled "Content/Docs", as code editors
	 * do, so a plugin whose only documents sit three folders deep does not need three clicks.
	 */
	void CompactFolders(FDocsTreeItem& InItem)
	{
		for (TSharedPtr<FDocsTreeItem>& Child : InItem.Children)
		{
			while (Child->Type == FDocsTreeItem::EType::Folder
				&& Child->Children.Num() == 1
				&& Child->Children[0]->Type == FDocsTreeItem::EType::Folder)
			{
				TSharedPtr<FDocsTreeItem> Only = Child->Children[0];
				Only->Label = Child->Label / Only->Label;
				Only->Parent = Child->Parent;
				Child = Only;
			}

			CompactFolders(*Child);
		}
	}

	template <typename TPredicate>
	TSharedPtr<FDocsTreeItem> FindItem(const TArray<TSharedPtr<FDocsTreeItem>>& InItems, TPredicate InPredicate)
	{
		for (const TSharedPtr<FDocsTreeItem>& Item : InItems)
		{
			if (InPredicate(*Item))
			{
				return Item;
			}
			if (TSharedPtr<FDocsTreeItem> Found = FindItem(Item->Children, InPredicate))
			{
				return Found;
			}
		}
		return nullptr;
	}
}

void SKhaosDocsBrowser::Construct(const FArguments& InArgs)
{
	LoadSettings();

	ChildSlot
	[
		SNew(SSplitter)
		.Orientation(Orient_Horizontal)
		.PhysicalSplitterHandleSize(2.0f)

		// Table of contents. The splitter skips a collapsed pane, which is how it hides.
		+ SSplitter::Slot()
		.Value(0.26f)
		.MinSize(180.0f)
		[
			SNew(SVerticalBox)
			.Visibility_Lambda([this]() { return bSidebarVisible ? EVisibility::Visible : EVisibility::Collapsed; })

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("Brushes.Panel"))
				.Padding(FMargin(8.0f, 0.0f))
				[
					SNew(SBox)
					.HeightOverride(FKhaosDocsStyle::HeaderHeight)
					.VAlign(VAlign_Center)
					[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SAssignNew(SearchBox, SSearchBox)
						.HintText(LOCTEXT("SearchHint", "Search documents"))
						.OnTextChanged(this, &SKhaosDocsBrowser::OnFilterTextChanged)
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(4.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "SimpleButton")
						.ContentPadding(FMargin(4.0f))
						.ToolTipText(LOCTEXT("RefreshTooltip", "Rescan the project and its plugins for documents."))
						.OnClicked(this, &SKhaosDocsBrowser::OnRefreshClicked)
						[
							SNew(SImage)
							.Image(FAppStyle::GetBrush("Icons.Refresh"))
							.ColorAndOpacity(FSlateColor::UseForeground())
						]
					]
					]
				]
			]

			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("Brushes.Recessed"))
				.Padding(FMargin(0.0f, 4.0f))
				[
					SAssignNew(TreeView, STreeView<TSharedPtr<FDocsTreeItem>>)
					.TreeItemsSource(&RootItems)
					.SelectionMode(ESelectionMode::Single)
					.OnGenerateRow(this, &SKhaosDocsBrowser::OnGenerateRow)
					.OnGetChildren(this, &SKhaosDocsBrowser::OnGetChildren)
					.OnSelectionChanged(this, &SKhaosDocsBrowser::OnSelectionChanged)
					.OnMouseButtonDoubleClick(this, &SKhaosDocsBrowser::OnMouseDoubleClick)
					.OnContextMenuOpening(this, &SKhaosDocsBrowser::OnContextMenuOpening)
				]
			]
		]

		// The document.
		+ SSplitter::Slot()
		.Value(0.74f)
		[
			SAssignNew(Editor, SKhaosDocsDocumentEditor)
			.OnToggleSidebar(this, &SKhaosDocsBrowser::ToggleSidebar)
			.IsSidebarVisible_Lambda([this]() { return bSidebarVisible; })
			.OnNavigate(this, &SKhaosDocsBrowser::OnNavigate)
			.OnSaved(this, &SKhaosDocsBrowser::OnDocumentSaved)
		]
	];

	Refresh();

	// Open on something rather than the empty state: the document from last time, else the first.
	if (!LastDocumentPath.IsEmpty() && FindDocumentItem(LastDocumentPath).IsValid())
	{
		ShowDocument(LastDocumentPath);
	}
	else if (const TSharedPtr<FDocsTreeItem> First = FindFirstDocumentItem())
	{
		ShowDocument(First->FilePath);
	}
}

// -- Settings ----------------------------------------------------------------------------------

void SKhaosDocsBrowser::LoadSettings()
{
	Favorites.Reset();
	LastDocumentPath.Reset();

	if (!GConfig)
	{
		return;
	}

	TArray<FString> Stored;
	GConfig->GetArray(SettingsSection, FavoritesKey, Stored, GEditorPerProjectIni);
	for (const FString& Path : Stored)
	{
		Favorites.AddUnique(FromStoredPath(Path));
	}

	FString Last;
	if (GConfig->GetString(SettingsSection, LastDocumentKey, Last, GEditorPerProjectIni) && !Last.IsEmpty())
	{
		LastDocumentPath = FromStoredPath(Last);
	}
}

void SKhaosDocsBrowser::SaveSettings() const
{
	if (!GConfig)
	{
		return;
	}

	TArray<FString> Stored;
	for (const FString& Path : Favorites)
	{
		Stored.Add(ToStoredPath(Path));
	}
	GConfig->SetArray(SettingsSection, FavoritesKey, Stored, GEditorPerProjectIni);
	GConfig->SetString(SettingsSection, LastDocumentKey, *ToStoredPath(LastDocumentPath), GEditorPerProjectIni);
	GConfig->Flush(false, GEditorPerProjectIni);
}

bool SKhaosDocsBrowser::IsFavorite(const FString& InFilePath) const
{
	return Favorites.Contains(InFilePath);
}

void SKhaosDocsBrowser::ToggleFavorite(const FString& InFilePath)
{
	if (!Favorites.Remove(InFilePath))
	{
		Favorites.Add(InFilePath);
	}
	SaveSettings();

	// The favourites group and the star on the row both come from the tree, so rebuild it.
	RebuildTree();
}

TSharedPtr<FDocsTreeItem> SKhaosDocsBrowser::FindFirstDocumentItem() const
{
	return FindItem(RootItems, [](const FDocsTreeItem& Item) { return Item.IsDocument(); });
}

// -- Discovery ---------------------------------------------------------------------------------

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
		Cached.BaseDir = Root.BaseDir;
		Cached.ContentDiskPath = Root.DiskPath;
		Cached.Docs.Reserve(Files.Num());

		for (const FString& File : Files)
		{
			FCachedDoc& Doc = Cached.Docs.AddDefaulted_GetRef();
			Doc.FilePath = File;
			Doc.Title = KhaosDocs::GetDocumentTitle(File);

			// Relative to the base folder rather than to Content, so that "Content/UI" and "Docs"
			// both read sensibly and a root-level README sits directly under the owner.
			FString RelativeDir = FPaths::GetPath(File);
			if (FPaths::MakePathRelativeTo(RelativeDir, *(Root.BaseDir / TEXT(""))) && RelativeDir != TEXT("."))
			{
				Doc.RelativeDir = MoveTemp(RelativeDir);
			}
		}
	}

	// The project itself first, then its plugins alphabetically.
	CachedRoots.Sort([](const FCachedRoot& A, const FCachedRoot& B)
	{
		const bool bAIsProject = A.VirtualPath.Equals(TEXT("/Game"), ESearchCase::IgnoreCase);
		const bool bBIsProject = B.VirtualPath.Equals(TEXT("/Game"), ESearchCase::IgnoreCase);
		if (bAIsProject != bBIsProject)
		{
			return bAIsProject;
		}
		return A.Owner.Compare(B.Owner, ESearchCase::IgnoreCase) < 0;
	});
}

void SKhaosDocsBrowser::RebuildTree()
{
	// Remember what is shown so the rebuilt tree can select it again without reopening it.
	const FString ShownPath = Editor.IsValid() ? Editor->GetFilePath() : FString();

	RootItems.Reset();

	// Favourites first, as a flat group. Each entry duplicates the document's entry under its
	// owner, which stays where it is so the folder view is complete.
	TSharedRef<FDocsTreeItem> FavoritesGroup = MakeShared<FDocsTreeItem>();
	FavoritesGroup->Type = FDocsTreeItem::EType::Favorites;
	FavoritesGroup->Label = TEXT("Favorites");

	for (const FCachedRoot& Cached : CachedRoots)
	{
		TSharedRef<FDocsTreeItem> Owner = MakeShared<FDocsTreeItem>();
		Owner->Type = FDocsTreeItem::EType::Owner;
		Owner->Label = Cached.Owner;
		Owner->DirPath = Cached.BaseDir;
		Owner->VirtualPath = Cached.VirtualPath;
		Owner->ContentDiskPath = Cached.ContentDiskPath;

		for (const FCachedDoc& Doc : Cached.Docs)
		{
			// Filtering matches the title and the file name, so both "Setup" and "setup.md" hit.
			if (!FilterText.IsEmpty()
				&& !Doc.Title.Contains(FilterText, ESearchCase::IgnoreCase)
				&& !FPaths::GetCleanFilename(Doc.FilePath).Contains(FilterText, ESearchCase::IgnoreCase))
			{
				continue;
			}

			// Walk down the folder chain, creating nodes as needed.
			TSharedPtr<FDocsTreeItem> Parent = Owner;
			if (!Doc.RelativeDir.IsEmpty())
			{
				TArray<FString> Segments;
				Doc.RelativeDir.ParseIntoArray(Segments, TEXT("/"), /*bCullEmpty*/ true);

				for (const FString& Segment : Segments)
				{
					TSharedPtr<FDocsTreeItem> Folder;
					for (const TSharedPtr<FDocsTreeItem>& Child : Parent->Children)
					{
						if (Child->Type == FDocsTreeItem::EType::Folder && Child->Label == Segment)
						{
							Folder = Child;
							break;
						}
					}

					if (!Folder.IsValid())
					{
						Folder = MakeShared<FDocsTreeItem>();
						Folder->Type = FDocsTreeItem::EType::Folder;
						Folder->Label = Segment;
						Folder->DirPath = Parent->DirPath / Segment;
						Folder->VirtualPath = Cached.VirtualPath;
						Folder->ContentDiskPath = Cached.ContentDiskPath;
						Folder->Parent = Parent;
						Parent->Children.Add(Folder);
					}

					Parent = Folder;
				}
			}

			TSharedRef<FDocsTreeItem> Item = MakeShared<FDocsTreeItem>();
			Item->Type = FDocsTreeItem::EType::Document;
			Item->Label = Doc.Title;
			Item->FilePath = Doc.FilePath;
			Item->VirtualPath = Cached.VirtualPath;
			Item->ContentDiskPath = Cached.ContentDiskPath;
			Item->OwnerLabel = Cached.Owner;
			Item->Parent = Parent;
			Parent->Children.Add(Item);

			if (IsFavorite(Doc.FilePath))
			{
				TSharedRef<FDocsTreeItem> Favorite = MakeShared<FDocsTreeItem>(*Item);
				Favorite->Children.Reset();
				Favorite->Parent = FavoritesGroup;
				FavoritesGroup->Children.Add(Favorite);
			}
		}

		if (Owner->Children.Num() > 0)
		{
			SortChildren(*Owner);
			CompactFolders(*Owner);
			RootItems.Add(Owner);
		}
	}

	if (FavoritesGroup->Children.Num() > 0)
	{
		SortChildren(*FavoritesGroup);
		RootItems.Insert(FavoritesGroup, 0);
	}

	if (TreeView.IsValid())
	{
		TreeView->RequestTreeRefresh();
		ExpandAll();

		if (!ShownPath.IsEmpty())
		{
			if (const TSharedPtr<FDocsTreeItem> Item = FindDocumentItem(ShownPath))
			{
				TGuardValue<bool> Guard(bSyncingSelection, true);
				TreeView->SetSelection(Item, ESelectInfo::Direct);
			}
		}
	}
}

TSharedPtr<FDocsTreeItem> SKhaosDocsBrowser::FindDocumentItem(const FString& InFilePath) const
{
	return FindItem(RootItems, [&InFilePath](const FDocsTreeItem& Item)
	{
		return Item.IsDocument() && Item.FilePath == InFilePath;
	});
}

void SKhaosDocsBrowser::ExpandAll()
{
	TFunction<void(const TSharedPtr<FDocsTreeItem>&)> Expand = [this, &Expand](const TSharedPtr<FDocsTreeItem>& Item)
	{
		if (!Item->IsDocument())
		{
			TreeView->SetItemExpansion(Item, true);
			for (const TSharedPtr<FDocsTreeItem>& Child : Item->Children)
			{
				Expand(Child);
			}
		}
	};

	for (const TSharedPtr<FDocsTreeItem>& Item : RootItems)
	{
		Expand(Item);
	}
}

void SKhaosDocsBrowser::ExpandTo(const TSharedPtr<FDocsTreeItem>& InItem)
{
	TSharedPtr<FDocsTreeItem> Parent = InItem->Parent.Pin();
	while (Parent.IsValid())
	{
		TreeView->SetItemExpansion(Parent, true);
		Parent = Parent->Parent.Pin();
	}
}

// -- Showing documents -------------------------------------------------------------------------

void SKhaosDocsBrowser::ShowDocument(const FString& InFilePath, bool bEdit)
{
	const FString FullPath = FPaths::ConvertRelativePathToFull(InFilePath);

	if (const TSharedPtr<FDocsTreeItem> Item = FindDocumentItem(FullPath))
	{
		// Selecting opens the document through OnSelectionChanged, which also handles a
		// refused switch (the user cancelled the save prompt).
		ExpandTo(Item);
		TreeView->SetSelection(Item, ESelectInfo::OnMouseClick);
		TreeView->RequestScrollIntoView(Item);

		// The selection may not have changed - the row was already selected but a link had
		// navigated the editor elsewhere - in which case nothing above opened the file.
		if (Editor->GetFilePath() != FullPath && !Editor->OpenFile(FullPath))
		{
			return;
		}
	}
	else if (!Editor->OpenFile(FullPath))
	{
		return;
	}

	if (bEdit && Editor->GetFilePath() == FullPath)
	{
		Editor->SetEditing(true);
	}
}

void SKhaosDocsBrowser::ToggleSidebar()
{
	bSidebarVisible = !bSidebarVisible;
}

bool SKhaosDocsBrowser::CanClose()
{
	return !Editor.IsValid() || Editor->PromptToSaveIfDirty();
}

void SKhaosDocsBrowser::OnNavigate(const FString& InFilePath)
{
	ShowDocument(InFilePath, /*bEdit*/ false);
}

void SKhaosDocsBrowser::OnDocumentSaved()
{
	// The H1 may have changed, and the table of contents shows H1s.
	Refresh();
}

// -- Tree callbacks ----------------------------------------------------------------------------

TSharedRef<ITableRow> SKhaosDocsBrowser::OnGenerateRow(TSharedPtr<FDocsTreeItem> InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	TAttribute<const FSlateBrush*> Icon;
	FName LabelStyle;
	FText ToolTip;
	FText Secondary;

	switch (InItem->Type)
	{
	case FDocsTreeItem::EType::Favorites:
		Icon = FAppStyle::GetBrush("Icons.Star");
		LabelStyle = TEXT("Doc.Tree.Owner");
		ToolTip = LOCTEXT("FavoritesTooltip", "Documents you have marked as favourites.");
		break;

	case FDocsTreeItem::EType::Owner:
		Icon = FAppStyle::GetBrush("Icons.Package");
		LabelStyle = TEXT("Doc.Tree.Owner");
		ToolTip = FText::FromString(InItem->DirPath);
		break;

	case FDocsTreeItem::EType::Folder:
		Icon = TAttribute<const FSlateBrush*>::CreateLambda([this, InItem]()
		{
			return FAppStyle::GetBrush(TreeView->IsItemExpanded(InItem) ? "Icons.FolderOpen" : "Icons.FolderClosed");
		});
		LabelStyle = TEXT("Doc.Tree.Folder");
		ToolTip = FText::FromString(InItem->DirPath);
		break;

	case FDocsTreeItem::EType::Document:
	default:
	{
		Icon = FKhaosDocsStyle::Get().GetBrush("KhaosDocs.Icon");
		LabelStyle = TEXT("Doc.Tree.Item");
		ToolTip = FText::FromString(InItem->FilePath);

		const TSharedPtr<FDocsTreeItem> Parent = InItem->Parent.Pin();
		if (Parent.IsValid() && Parent->Type == FDocsTreeItem::EType::Favorites)
		{
			// Out of its folder, a favourite needs to say where it came from.
			Secondary = FText::FromString(InItem->OwnerLabel);
		}
		else
		{
			// Titles come from the H1, so show the file name too when the two differ enough that
			// someone looking for "README.md" would not recognise "Khaos UI".
			const FString FileName = FPaths::GetCleanFilename(InItem->FilePath);
			if (!FPaths::GetBaseFilename(FileName).Equals(InItem->Label, ESearchCase::IgnoreCase))
			{
				Secondary = FText::FromString(FileName);
			}
		}
		break;
	}
	}

	const bool bShowStar = InItem->IsDocument() && IsFavorite(InItem->FilePath);

	return SNew(STableRow<TSharedPtr<FDocsTreeItem>>, OwnerTable)
		.Padding(FMargin(0.0f, 1.0f))
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(2.0f, 2.0f, 6.0f, 2.0f)
			[
				SNew(SImage)
				.Image(Icon)
				.ColorAndOpacity(InItem->Type == FDocsTreeItem::EType::Folder ? FSlateColor(FStyleColors::AccentFolder) : FSlateColor::UseForeground())
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(InItem->Label))
				.ToolTipText(ToolTip)
				.TextStyle(&FKhaosDocsStyle::GetText(LabelStyle))
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(8.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(Secondary)
				.ToolTipText(ToolTip)
				.TextStyle(&FKhaosDocsStyle::GetText("Doc.Tree.Secondary"))
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
				.Visibility(Secondary.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4.0f, 0.0f, 8.0f, 0.0f)
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush("Icons.Star"))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.DesiredSizeOverride(FVector2D(12.0f, 12.0f))
				.Visibility(bShowStar ? EVisibility::Visible : EVisibility::Collapsed)
			]
		];
}

void SKhaosDocsBrowser::OnGetChildren(TSharedPtr<FDocsTreeItem> InItem, TArray<TSharedPtr<FDocsTreeItem>>& OutChildren)
{
	OutChildren = InItem->Children;
}

void SKhaosDocsBrowser::OnSelectionChanged(TSharedPtr<FDocsTreeItem> InItem, ESelectInfo::Type SelectInfo)
{
	if (bSyncingSelection || !InItem.IsValid() || !InItem->IsDocument())
	{
		return;
	}

	if (Editor->OpenFile(InItem->FilePath))
	{
		LastDocumentPath = InItem->FilePath;
		SaveSettings();
		return;
	}

	// The switch was refused - unsaved edits the user chose to keep - so put the selection back
	// on the document that is still showing.
	TGuardValue<bool> Guard(bSyncingSelection, true);
	if (const TSharedPtr<FDocsTreeItem> Shown = FindDocumentItem(Editor->GetFilePath()))
	{
		TreeView->SetSelection(Shown, ESelectInfo::Direct);
	}
	else
	{
		TreeView->ClearSelection();
	}
}

void SKhaosDocsBrowser::OnMouseDoubleClick(TSharedPtr<FDocsTreeItem> InItem)
{
	if (!InItem.IsValid())
	{
		return;
	}

	if (InItem->IsDocument())
	{
		// Single click reads, double click edits.
		if (Editor->GetFilePath() == InItem->FilePath)
		{
			Editor->SetEditing(true);
		}
	}
	else
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

FReply SKhaosDocsBrowser::OnRefreshClicked()
{
	IKhaosDocsEditorModule::Get().RefreshMounts();
	Refresh();
	return FReply::Handled();
}

TSharedPtr<SWidget> SKhaosDocsBrowser::OnContextMenuOpening()
{
	const TArray<TSharedPtr<FDocsTreeItem>> Selected = TreeView->GetSelectedItems();
	if (Selected.Num() != 1 || !Selected[0].IsValid())
	{
		return nullptr;
	}

	const TSharedPtr<FDocsTreeItem> Item = Selected[0];

	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection*/ true, nullptr);

	if (Item->IsDocument())
	{
		MenuBuilder.BeginSection("Document", LOCTEXT("DocumentSection", "Document"));
		{
			const bool bIsFavorite = IsFavorite(Item->FilePath);
			MenuBuilder.AddMenuEntry(
				bIsFavorite ? LOCTEXT("RemoveFavorite", "Remove from Favorites") : LOCTEXT("AddFavorite", "Add to Favorites"),
				LOCTEXT("FavoriteTooltip", "Favourites are listed at the top of the table of contents."),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), bIsFavorite ? "Icons.Star" : "Icons.Star.Outline"),
				FUIAction(FExecuteAction::CreateLambda([this, Item]()
				{
					ToggleFavorite(Item->FilePath);
				})));

			MenuBuilder.AddMenuEntry(
				LOCTEXT("EditDocument", "Edit"),
				LOCTEXT("EditDocumentTooltip", "Edit this document here, with a live preview."),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Edit"),
				FUIAction(FExecuteAction::CreateLambda([this, Item]()
				{
					ShowDocument(Item->FilePath, /*bEdit*/ true);
				})));

			MenuBuilder.AddMenuEntry(
				LOCTEXT("OpenExternally", "Open Externally"),
				LOCTEXT("OpenExternallyTooltip", "Open this file in your default external editor."),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.OpenInExternalEditor"),
				FUIAction(FExecuteAction::CreateLambda([Item]()
				{
					FPlatformProcess::LaunchFileInDefaultExternalApplication(*Item->FilePath, nullptr, ELaunchVerb::Edit);
				})));
		}
		MenuBuilder.EndSection();
	}

	MenuBuilder.BeginSection("Location", LOCTEXT("LocationSection", "Location"));
	{
		if (CanShowInContentBrowser(*Item))
		{
			MenuBuilder.AddMenuEntry(
				LOCTEXT("ShowInContentBrowser", "Show in Content Browser"),
				LOCTEXT("ShowInContentBrowserTooltip", "Select this in the Content Browser."),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.ContentBrowser"),
				FUIAction(FExecuteAction::CreateLambda([this, Item]()
				{
					ShowInContentBrowser(*Item);
				})));
		}

		MenuBuilder.AddMenuEntry(
			LOCTEXT("ShowInExplorer", "Show in Explorer"),
			LOCTEXT("ShowInExplorerTooltip", "Reveal this on disk."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.OpenSourceLocation"),
			FUIAction(FExecuteAction::CreateLambda([Item]()
			{
				FPlatformProcess::ExploreFolder(Item->IsDocument() ? *Item->FilePath : *Item->DirPath);
			})));

		MenuBuilder.AddMenuEntry(
			LOCTEXT("CopyPath", "Copy Path"),
			LOCTEXT("CopyPathTooltip", "Copy the full path to the clipboard."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "GenericCommands.Copy"),
			FUIAction(FExecuteAction::CreateLambda([Item]()
			{
				FPlatformApplicationMisc::ClipboardCopy(Item->IsDocument() ? *Item->FilePath : *Item->DirPath);
			})));
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

// -- Content Browser ---------------------------------------------------------------------------

bool SKhaosDocsBrowser::CanShowInContentBrowser(const FDocsTreeItem& InItem) const
{
	// Only Content is mounted into the Content Browser; a README beside the .uplugin is not there.
	const FString& Path = InItem.IsDocument() ? InItem.FilePath : InItem.DirPath;
	return !InItem.ContentDiskPath.IsEmpty() && FPaths::IsUnderDirectory(Path, InItem.ContentDiskPath);
}

void SKhaosDocsBrowser::ShowInContentBrowser(const FDocsTreeItem& InItem) const
{
	if (!CanShowInContentBrowser(InItem))
	{
		return;
	}

	UContentBrowserDataSubsystem* ContentBrowserData = IContentBrowserDataModule::Get().GetSubsystem();
	if (!ContentBrowserData)
	{
		return;
	}

	// Internal paths are the mounted root plus the path below the Content folder, e.g.
	// "/KhaosUI/Docs/Readme.md". The subsystem turns that into whatever the user's virtual layout
	// ("/All/Plugins/...") calls it.
	FString Relative = InItem.IsDocument() ? InItem.FilePath : InItem.DirPath;
	if (!FPaths::MakePathRelativeTo(Relative, *(InItem.ContentDiskPath / TEXT(""))))
	{
		return;
	}

	FString Internal = InItem.VirtualPath;
	if (!Relative.IsEmpty() && Relative != TEXT("."))
	{
		Internal /= Relative;
	}

	FName VirtualPath;
	ContentBrowserData->ConvertInternalPathToVirtual(FName(*Internal), VirtualPath);

	const FContentBrowserItem Item = ContentBrowserData->GetItemAtPath(VirtualPath, EContentBrowserItemTypeFilter::IncludeAll);
	if (!Item.IsValid())
	{
		return;
	}

	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	ContentBrowserModule.Get().SyncBrowserToItems({ Item });
}

#undef LOCTEXT_NAMESPACE
