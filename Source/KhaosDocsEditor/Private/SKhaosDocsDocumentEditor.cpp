// Copyright Khaos Games. All Rights Reserved.

#include "SKhaosDocsDocumentEditor.h"

#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformProcess.h"
#include "KhaosDocsDocument.h"
#include "KhaosDocsEditor.h"
#include "KhaosDocsStyle.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "SKhaosDocsDocumentView.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Styling/StyleColors.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "KhaosDocsDocumentEditor"

namespace
{
	enum ESwitcherIndex : int32
	{
		Switcher_Empty = 0,
		Switcher_Read = 1,
		Switcher_Edit = 2,
	};
}

void SKhaosDocsDocumentEditor::Construct(const FArguments& InArgs)
{
	OnToggleSidebar = InArgs._OnToggleSidebar;
	IsSidebarVisible = InArgs._IsSidebarVisible;
	OnNavigate = InArgs._OnNavigate;
	OnDocumentChanged = InArgs._OnDocumentChanged;
	OnSaved = InArgs._OnSaved;

	const FSlateHyperlinkRun::FOnClick LinkHandler =
		FSlateHyperlinkRun::FOnClick::CreateSP(this, &SKhaosDocsDocumentEditor::OnLinkClicked);

	ChildSlot
	[
		// The outline is a pane of its own, split from the document the way the table of contents
		// is on the other side, so the three columns read as one layout.
		SNew(SSplitter)
		.Orientation(Orient_Horizontal)
		.PhysicalSplitterHandleSize(2.0f)

		+ SSplitter::Slot()
		.Value(0.82f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				MakeHeader()
			]

			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("Brushes.Recessed"))
			.Padding(0.0f)
			[
				SAssignNew(Switcher, SWidgetSwitcher)
				.WidgetIndex(this, &SKhaosDocsDocumentEditor::GetActiveSwitcherIndex)

				// Switcher_Empty
				+ SWidgetSwitcher::Slot()
				[
					MakeEmptyState()
				]

				// Switcher_Read
				+ SWidgetSwitcher::Slot()
				[
					SAssignNew(ReadView, SKhaosDocsDocumentView)
					.OnLinkClicked(LinkHandler)
				]

				// Switcher_Edit
				+ SWidgetSwitcher::Slot()
				[
					SNew(SSplitter)
					.Orientation(Orient_Horizontal)
					.PhysicalSplitterHandleSize(2.0f)

					+ SSplitter::Slot()
					.Value(0.5f)
					[
						SAssignNew(SourceBox, SMultiLineEditableTextBox)
						.Style(&FAppStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("Log.TextBox"))
						// SMultiLineEditableTextBox::TextStyle is deprecated and ignored since 5.2;
						// Font is the supported per-widget override.
						.Font(FCoreStyle::GetDefaultFontStyle("Mono", 9))
						.Padding(FMargin(12.0f, 10.0f))
						.AutoWrapText(false)
						.AlwaysShowScrollbars(false)
						.OnTextChanged(this, &SKhaosDocsDocumentEditor::OnSourceTextChanged)
					]

					+ SSplitter::Slot()
					.Value(0.5f)
					[
						SAssignNew(PreviewView, SKhaosDocsDocumentView)
						.OnLinkClicked(LinkHandler)
					]
				]
			]
			]
		]

		+ SSplitter::Slot()
		.Value(0.18f)
		.MinSize(150.0f)
		[
			MakeOutline()
		]
	];
}

TSharedRef<SWidget> SKhaosDocsDocumentEditor::MakeOutline()
{
	// Same anatomy as the table of contents pane: a header strip of the shared height, then the
	// list on the recessed ground. The splitter skips it entirely while collapsed.
	return SNew(SVerticalBox)
		.Visibility(this, &SKhaosDocsDocumentEditor::GetOutlineVisibility)

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("Brushes.Panel"))
			.Padding(FMargin(12.0f, 0.0f))
			[
				SNew(SBox)
				.HeightOverride(FKhaosDocsStyle::HeaderHeight)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("OutlineTitle", "On this page"))
					.TextStyle(&FKhaosDocsStyle::GetText("Doc.Tree.Owner"))
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
				SAssignNew(OutlineList, SListView<TSharedPtr<KhaosDocsMarkdown::FHeading>>)
				.ListItemsSource(&Outline)
				.SelectionMode(ESelectionMode::Single)
				.OnGenerateRow(this, &SKhaosDocsDocumentEditor::OnGenerateOutlineRow)
				.OnSelectionChanged(this, &SKhaosDocsDocumentEditor::OnOutlineSelectionChanged)
			]
		];
}

TSharedRef<ITableRow> SKhaosDocsDocumentEditor::OnGenerateOutlineRow(TSharedPtr<KhaosDocsMarkdown::FHeading> InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	// Indent by heading depth so the outline reads as a hierarchy. H1 is the title and is shown
	// heavier; everything below it is a section.
	const float Indent = 12.0f * FMath::Max(0, InItem->Level - 1);
	const FName StyleName = (InItem->Level <= 1) ? TEXT("Doc.Tree.Owner") : TEXT("Doc.Tree.Item");

	return SNew(STableRow<TSharedPtr<KhaosDocsMarkdown::FHeading>>, OwnerTable)
		.Padding(FMargin(0.0f, 1.0f))
		[
			SNew(STextBlock)
			.Text(FText::FromString(InItem->Text))
			.ToolTipText(FText::FromString(InItem->Text))
			.Margin(FMargin(Indent + 8.0f, 3.0f, 8.0f, 3.0f))
			.TextStyle(&FKhaosDocsStyle::GetText(StyleName))
			.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
		];
}

void SKhaosDocsDocumentEditor::OnOutlineSelectionChanged(TSharedPtr<KhaosDocsMarkdown::FHeading> InItem, ESelectInfo::Type SelectInfo)
{
	if (InItem.IsValid() && SelectInfo != ESelectInfo::Direct)
	{
		ScrollToHeading(Outline.IndexOfByKey(InItem));
	}
}

EVisibility SKhaosDocsDocumentEditor::GetOutlineVisibility() const
{
	// A single heading is the title, which the header already shows.
	return (bOutlineVisible && Document.IsValid() && Outline.Num() > 1) ? EVisibility::Visible : EVisibility::Collapsed;
}

void SKhaosDocsDocumentEditor::RefreshOutline()
{
	Outline.Reset();

	if (Document.IsValid())
	{
		for (const KhaosDocsMarkdown::FHeading& Heading : GetHeadings())
		{
			Outline.Add(MakeShared<KhaosDocsMarkdown::FHeading>(Heading));
		}
	}

	if (OutlineList.IsValid())
	{
		OutlineList->ClearSelection();
		OutlineList->RequestListRefresh();
	}
}

TSharedRef<SWidget> SKhaosDocsDocumentEditor::MakeHeader()
{
	// A pane's toggle sits on the pane's side: table of contents at the far left, outline at the
	// far right. Thin dividers separate the header into its groups.
	TSharedRef<SHorizontalBox> Header = SNew(SHorizontalBox);

	if (OnToggleSidebar.IsBound())
	{
		Header->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			MakePaneToggle(
				"KhaosDocs.PanelLeft",
				LOCTEXT("ToggleSidebarTooltip", "Show or hide the table of contents."),
				FOnClicked::CreateSP(this, &SKhaosDocsDocumentEditor::OnToggleSidebarClicked),
				IsSidebarVisible)
		];

		Header->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			MakeDivider()
		];
	}

	Header->AddSlot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	.Padding(0.0f, 0.0f, 10.0f, 0.0f)
	[
		SNew(SImage)
		.Image(FKhaosDocsStyle::Get().GetBrush("KhaosDocs.Icon"))
		.Visibility_Lambda([this]() { return HasDocument() ? EVisibility::Visible : EVisibility::Collapsed; })
	];

	Header->AddSlot()
	.FillWidth(1.0f)
	.VAlign(VAlign_Center)
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(this, &SKhaosDocsDocumentEditor::GetHeaderTitle)
			.TextStyle(&FKhaosDocsStyle::GetText("Doc.Title"))
			.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 1.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(this, &SKhaosDocsDocumentEditor::GetDisplayPath)
			.ToolTipText_Lambda([this]() { return FText::FromString(GetFilePath()); })
			.TextStyle(&FKhaosDocsStyle::GetText("Doc.Subtitle"))
			.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
			.Visibility_Lambda([this]() { return HasDocument() ? EVisibility::Visible : EVisibility::Collapsed; })
		]
	];

	Header->AddSlot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	.Padding(12.0f, 0.0f, 0.0f, 0.0f)
	[
		SNew(SSegmentedControl<bool>)
		.Value_Lambda([this]() { return bEditing; })
		.OnValueChanged(this, &SKhaosDocsDocumentEditor::OnModeChanged)
		.IsEnabled_Lambda([this]() { return HasDocument(); })

		+ SSegmentedControl<bool>::Slot(false)
		.Icon(FAppStyle::GetBrush("Icons.Visible"))
		.Text(LOCTEXT("ViewMode", "View"))
		.ToolTip(LOCTEXT("ViewModeTooltip", "Read the rendered document."))

		+ SSegmentedControl<bool>::Slot(true)
		.Icon(FAppStyle::GetBrush("Icons.Edit"))
		.Text(LOCTEXT("EditMode", "Edit"))
		.ToolTip(LOCTEXT("EditModeTooltip", "Edit the markdown with a live preview alongside."))
	];

	Header->AddSlot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	[
		MakeDivider()
	];

	Header->AddSlot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	[
		MakeToolbarButton(
			"Icons.Save",
			LOCTEXT("SaveTooltip", "Save this document to disk (Ctrl+S)."),
			FOnClicked::CreateSP(this, &SKhaosDocsDocumentEditor::OnSaveClicked),
			TAttribute<bool>::CreateLambda([this]() { return IsDirty(); }))
	];

	Header->AddSlot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	[
		MakeToolbarButton(
			"Icons.Refresh",
			LOCTEXT("ReloadTooltip", "Re-read this document from disk, discarding unsaved edits."),
			FOnClicked::CreateSP(this, &SKhaosDocsDocumentEditor::OnReloadClicked),
			TAttribute<bool>::CreateLambda([this]() { return HasDocument(); }))
	];

	Header->AddSlot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	[
		MakeToolbarButton(
			"Icons.OpenInExternalEditor",
			LOCTEXT("OpenExternallyTooltip", "Open this file in your default external editor."),
			FOnClicked::CreateSP(this, &SKhaosDocsDocumentEditor::OnOpenExternallyClicked),
			TAttribute<bool>::CreateLambda([this]() { return HasDocument(); }))
	];

	Header->AddSlot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	[
		MakeDivider()
	];

	Header->AddSlot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	[
		MakePaneToggle(
			"KhaosDocs.PanelRight",
			LOCTEXT("ToggleOutlineTooltip", "Show or hide the outline of this document's headings."),
			FOnClicked::CreateSP(this, &SKhaosDocsDocumentEditor::OnToggleOutlineClicked),
			TAttribute<bool>::CreateLambda([this]() { return bOutlineVisible; }))
	];

	// A fixed height rather than padding, so this lines up with the search strip on the left
	// whether or not a document (and so a path line) is showing.
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Brushes.Panel"))
		.Padding(FMargin(8.0f, 0.0f))
		[
			SNew(SBox)
			.HeightOverride(FKhaosDocsStyle::HeaderHeight)
			.VAlign(VAlign_Center)
			[
				Header
			]
		];
}

TSharedRef<SWidget> SKhaosDocsDocumentEditor::MakeDivider()
{
	return SNew(SBox)
		.HeightOverride(18.0f)
		.Padding(FMargin(8.0f, 0.0f))
		.VAlign(VAlign_Fill)
		[
			SNew(SSeparator)
			.Orientation(Orient_Vertical)
			.SeparatorImage(FKhaosDocsStyle::Get().GetBrush("Doc.Separator"))
			.Thickness(1.0f)
		];
}

TSharedRef<SWidget> SKhaosDocsDocumentEditor::MakePaneToggle(
	const FName InIconName,
	const FText& InToolTip,
	FOnClicked InOnClicked,
	TAttribute<bool> InIsPaneVisible)
{
	// The glyph dims while its pane is hidden, so the state reads at a glance.
	return SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "SimpleButton")
		.ContentPadding(FMargin(5.0f, 4.0f))
		.ToolTipText(InToolTip)
		.OnClicked(InOnClicked)
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Center)
		[
			SNew(SImage)
			.Image(FKhaosDocsStyle::Get().GetBrush(InIconName))
			.ColorAndOpacity_Lambda([InIsPaneVisible]()
			{
				return InIsPaneVisible.Get() ? FSlateColor::UseForeground() : FSlateColor::UseSubduedForeground();
			})
		];
}

TSharedRef<SWidget> SKhaosDocsDocumentEditor::MakeEmptyState()
{
	return SNew(SBox)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.Padding(32.0f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(0.0f, 0.0f, 0.0f, 12.0f)
			[
				SNew(SImage)
				.Image(FKhaosDocsStyle::Get().GetBrush("KhaosDocs.Icon.Large"))
				.ColorAndOpacity(FLinearColor(1.0f, 1.0f, 1.0f, 0.35f))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(0.0f, 0.0f, 0.0f, 6.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("EmptyTitle", "No document selected"))
				.TextStyle(&FKhaosDocsStyle::GetText("Doc.Empty.Title"))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			[
				SNew(SBox)
				.MaxDesiredWidth(360.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("EmptyBody", "Pick a document from the table of contents. Documents are .md or .txt files under a Content folder, in a Docs folder beside a .uplugin, or at a plugin's root."))
					.TextStyle(&FKhaosDocsStyle::GetText("Doc.Empty.Body"))
					.Justification(ETextJustify::Center)
					.AutoWrapText(true)
				]
			]
		];
}

TSharedRef<SWidget> SKhaosDocsDocumentEditor::MakeToolbarButton(
	const FName InIconName,
	const FText& InToolTip,
	FOnClicked InOnClicked,
	TAttribute<bool> InIsEnabled)
{
	return SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "SimpleButton")
		.ContentPadding(FMargin(5.0f, 4.0f))
		.ToolTipText(InToolTip)
		.OnClicked(InOnClicked)
		.IsEnabled(InIsEnabled)
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Center)
		[
			SNew(SImage)
			.Image(FAppStyle::GetBrush(InIconName))
			.ColorAndOpacity(FSlateColor::UseForeground())
		];
}

// -- Document lifecycle ------------------------------------------------------------------------

bool SKhaosDocsDocumentEditor::OpenFile(const FString& InFilePath)
{
	const FString FullPath = FPaths::ConvertRelativePathToFull(InFilePath);

	if (Document.IsValid() && Document->FilePath == FullPath)
	{
		// Already showing this file. Pick up outside edits, but never throw away our own.
		if (!Document->IsDirty() && Document->HasExternalChanges())
		{
			Document->LoadFromDisk();
			RefreshViews(/*bScrollToTop*/ false);
			NotifyChanged();
		}
		return true;
	}

	if (!PromptToSaveIfDirty())
	{
		return false;
	}

	UKhaosDocsDocument* NewDocument = NewObject<UKhaosDocsDocument>(GetTransientPackage(), NAME_None, RF_Transient);
	NewDocument->FilePath = FullPath;
	if (!NewDocument->LoadFromDisk())
	{
		return false;
	}

	Document.Reset(NewDocument);
	RefreshViews(/*bScrollToTop*/ true);
	NotifyChanged();
	return true;
}

void SKhaosDocsDocumentEditor::SetDocument(UKhaosDocsDocument* InDocument)
{
	Document.Reset(InDocument);
	RefreshViews(/*bScrollToTop*/ true);
	NotifyChanged();
}

void SKhaosDocsDocumentEditor::Clear()
{
	Document.Reset();
	RefreshViews(/*bScrollToTop*/ true);
	NotifyChanged();
}

bool SKhaosDocsDocumentEditor::PromptToSaveIfDirty()
{
	if (!IsDirty())
	{
		return true;
	}

	const EAppReturnType::Type Choice = FMessageDialog::Open(
		EAppMsgType::YesNoCancel,
		FText::Format(
			LOCTEXT("SaveOnSwitch", "Save changes to {0}?"),
			FText::FromString(FPaths::GetCleanFilename(Document->FilePath))));

	if (Choice == EAppReturnType::Cancel)
	{
		return false;
	}

	if (Choice == EAppReturnType::Yes)
	{
		return Save();
	}

	return true;
}

bool SKhaosDocsDocumentEditor::Save()
{
	if (!Document.IsValid())
	{
		return false;
	}

	// Documents are expected to be edited outside Unreal too, so saving over a file that changed
	// underneath us is a real risk rather than a theoretical one.
	if (Document->HasExternalChanges())
	{
		const EAppReturnType::Type Choice = FMessageDialog::Open(
			EAppMsgType::YesNo,
			FText::Format(
				LOCTEXT("ConfirmOverwriteExternal", "{0} has changed on disk since it was opened. Overwrite those changes?"),
				FText::FromString(FPaths::GetCleanFilename(Document->FilePath))));

		if (Choice != EAppReturnType::Yes)
		{
			return false;
		}
	}

	if (!Document->SaveToDisk())
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			FText::Format(
				LOCTEXT("SaveFailed", "Could not write {0}. The file may be read-only or checked in."),
				FText::FromString(Document->FilePath)));
		return false;
	}

	NotifyChanged();
	OnSaved.ExecuteIfBound();
	return true;
}

void SKhaosDocsDocumentEditor::Reload()
{
	if (!Document.IsValid())
	{
		return;
	}

	if (Document->IsDirty())
	{
		const EAppReturnType::Type Choice = FMessageDialog::Open(
			EAppMsgType::YesNo,
			LOCTEXT("ConfirmReload", "Discard your unsaved changes and reload this file from disk?"));

		if (Choice != EAppReturnType::Yes)
		{
			return;
		}
	}

	if (Document->LoadFromDisk())
	{
		RefreshViews(/*bScrollToTop*/ false);
		NotifyChanged();
	}
}

bool SKhaosDocsDocumentEditor::HasDocument() const
{
	return Document.IsValid();
}

bool SKhaosDocsDocumentEditor::IsDirty() const
{
	return Document.IsValid() && Document->IsDirty();
}

void SKhaosDocsDocumentEditor::SetEditing(bool bInEditing)
{
	if (bEditing == bInEditing)
	{
		return;
	}

	bEditing = bInEditing;

	// Only the visible view is kept current while editing, so bring the other one up to date on
	// the way across.
	RefreshViews(/*bScrollToTop*/ false);
	NotifyChanged();

	if (bEditing && SourceBox.IsValid())
	{
		FSlateApplication::Get().SetKeyboardFocus(SourceBox, EFocusCause::SetDirectly);
	}
}

FString SKhaosDocsDocumentEditor::GetFilePath() const
{
	return Document.IsValid() ? Document->FilePath : FString();
}

FText SKhaosDocsDocumentEditor::GetTitle() const
{
	return CachedTitle;
}

UKhaosDocsDocument* SKhaosDocsDocumentEditor::GetDocument() const
{
	return Document.Get();
}

const TArray<KhaosDocsMarkdown::FHeading>& SKhaosDocsDocumentEditor::GetHeadings() const
{
	return (bEditing ? PreviewView : ReadView)->GetHeadings();
}

void SKhaosDocsDocumentEditor::ScrollToHeading(int32 InHeadingIndex)
{
	(bEditing ? PreviewView : ReadView)->ScrollToHeading(InHeadingIndex);
}

FReply SKhaosDocsDocumentEditor::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::S && (InKeyEvent.IsControlDown() || InKeyEvent.IsCommandDown()))
	{
		if (IsDirty())
		{
			Save();
		}
		return FReply::Handled();
	}

	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

// -- Internals ---------------------------------------------------------------------------------

void SKhaosDocsDocumentEditor::RefreshViews(bool bScrollToTop)
{
	const FString Text = Document.IsValid() ? Document->GetText() : FString();

	if (bEditing)
	{
		if (SourceBox.IsValid())
		{
			// Only reset the box when its text actually differs, or the caret jumps to the end
			// of the file on every save.
			if (!SourceBox->GetText().ToString().Equals(Text, ESearchCase::CaseSensitive))
			{
				bSettingSourceText = true;
				SourceBox->SetText(FText::FromString(Text));
				bSettingSourceText = false;
			}
		}
		PreviewView->SetMarkdown(Text, bScrollToTop);
	}
	else
	{
		ReadView->SetMarkdown(Text, bScrollToTop);
	}
}

void SKhaosDocsDocumentEditor::NotifyChanged()
{
	if (Document.IsValid())
	{
		// Title from the text in hand rather than the file, so it follows the H1 as it is typed.
		// The headings are already parsed for whichever view is showing.
		FString Title;
		for (const KhaosDocsMarkdown::FHeading& Heading : GetHeadings())
		{
			if (Heading.Level == 1)
			{
				Title = Heading.Text;
				break;
			}
		}
		CachedTitle = FText::FromString(Title.IsEmpty() ? FPaths::GetBaseFilename(Document->FilePath) : Title);

		// Relative to the project, which is how people talk about these files
		// ("Plugins/KhaosUI/README.md").
		FString Path = Document->FilePath;
		const FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
		if (!FPaths::MakePathRelativeTo(Path, *ProjectDir))
		{
			Path = Document->FilePath;
		}
		CachedDisplayPath = FText::FromString(Path);
	}
	else
	{
		CachedTitle = FText::GetEmpty();
		CachedDisplayPath = FText::GetEmpty();
	}

	RefreshOutline();
	OnDocumentChanged.ExecuteIfBound();
}

FText SKhaosDocsDocumentEditor::GetHeaderTitle() const
{
	if (!Document.IsValid())
	{
		return LOCTEXT("NoDocument", "Documentation");
	}

	const FText Title = GetTitle();
	return Document->IsDirty()
		? FText::Format(LOCTEXT("DirtyTitle", "{0}*"), Title)
		: Title;
}

FText SKhaosDocsDocumentEditor::GetDisplayPath() const
{
	return CachedDisplayPath;
}

int32 SKhaosDocsDocumentEditor::GetActiveSwitcherIndex() const
{
	if (!Document.IsValid())
	{
		return Switcher_Empty;
	}
	return bEditing ? Switcher_Edit : Switcher_Read;
}

void SKhaosDocsDocumentEditor::OnSourceTextChanged(const FText& InText)
{
	if (bSettingSourceText || !Document.IsValid())
	{
		return;
	}

	Document->SetText(InText.ToString());
	PreviewView->SetMarkdown(Document->GetText(), /*bScrollToTop*/ false);
	NotifyChanged();
}

void SKhaosDocsDocumentEditor::OnModeChanged(bool bInEditing)
{
	SetEditing(bInEditing);
}

void SKhaosDocsDocumentEditor::OnLinkClicked(const FSlateHyperlinkRun::FMetadata& InMetadata)
{
	const FString* Href = InMetadata.Find(TEXT("href"));
	if (!Href || Href->IsEmpty())
	{
		return;
	}

	// Absolute URLs go to the system browser.
	if (Href->StartsWith(TEXT("http://"), ESearchCase::IgnoreCase)
		|| Href->StartsWith(TEXT("https://"), ESearchCase::IgnoreCase))
	{
		FPlatformProcess::LaunchURL(**Href, nullptr, nullptr);
		return;
	}

	if (!Document.IsValid())
	{
		return;
	}

	// Relative links resolve against the folder holding this document, so cross-document links
	// written for GitHub ("[see](Setup.md)") work unchanged here. Anchors are dropped.
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

	const FString Resolved = FPaths::ConvertRelativePathToFull(FPaths::GetPath(Document->FilePath) / Target);
	if (!FPaths::FileExists(Resolved))
	{
		return;
	}

	if (OnNavigate.IsBound())
	{
		OnNavigate.Execute(Resolved);
	}
	else
	{
		OpenFile(Resolved);
	}
}

FReply SKhaosDocsDocumentEditor::OnSaveClicked()
{
	Save();
	return FReply::Handled();
}

FReply SKhaosDocsDocumentEditor::OnReloadClicked()
{
	Reload();
	return FReply::Handled();
}

FReply SKhaosDocsDocumentEditor::OnOpenExternallyClicked()
{
	if (Document.IsValid())
	{
		FPlatformProcess::LaunchFileInDefaultExternalApplication(*Document->FilePath, nullptr, ELaunchVerb::Edit);
	}
	return FReply::Handled();
}

FReply SKhaosDocsDocumentEditor::OnToggleSidebarClicked()
{
	OnToggleSidebar.ExecuteIfBound();
	return FReply::Handled();
}

FReply SKhaosDocsDocumentEditor::OnToggleOutlineClicked()
{
	bOutlineVisible = !bOutlineVisible;
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
