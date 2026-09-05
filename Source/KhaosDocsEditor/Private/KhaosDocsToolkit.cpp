// Copyright Khaos Games. All Rights Reserved.

#include "KhaosDocsToolkit.h"

#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/PlatformProcess.h"
#include "KhaosDocsDocument.h"
#include "KhaosDocsEditor.h"
#include "KhaosDocsStyle.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "KhaosDocsToolkit"

const FName FKhaosDocsToolkit::ToolkitName(TEXT("KhaosDocsEditor"));
const FName FKhaosDocsToolkit::PreviewTabId(TEXT("KhaosDocs_Preview"));
const FName FKhaosDocsToolkit::SourceTabId(TEXT("KhaosDocs_Source"));
const FName FKhaosDocsToolkit::OutlineTabId(TEXT("KhaosDocs_Outline"));

namespace
{
	/** One heading and the markdown that follows it, up to the next heading. */
	struct FDocSection
	{
		FString Markdown;
		FString HeadingText;
		int32 HeadingLevel = 0;
	};

	/**
	 * Split a document at its headings so each section can be rendered into its own widget.
	 * Rendering per-section is what lets the outline scroll precisely to a heading - a single
	 * SRichTextBlock has no addressable anchors.
	 */
	void SplitSections(const FString& InMarkdown, TArray<FDocSection>& OutSections)
	{
		TArray<FString> Lines;
		InMarkdown.ParseIntoArrayLines(Lines, /*bCullEmpty*/ false);

		bool bInFence = false;
		FDocSection Current;

		for (const FString& Line : Lines)
		{
			const FString Trimmed = Line.TrimStartAndEnd();

			if (Trimmed.StartsWith(TEXT("```"), ESearchCase::CaseSensitive)
				|| Trimmed.StartsWith(TEXT("~~~"), ESearchCase::CaseSensitive))
			{
				bInFence = !bInFence;
			}

			int32 Level = 0;
			if (!bInFence)
			{
				while (Level < Trimmed.Len() && Trimmed[Level] == TEXT('#'))
				{
					++Level;
				}
			}

			const bool bIsHeading = !bInFence
				&& Level >= 1 && Level <= 6
				&& Level < Trimmed.Len()
				&& Trimmed[Level] == TEXT(' ');

			if (bIsHeading && !Current.Markdown.IsEmpty())
			{
				OutSections.Add(MoveTemp(Current));
				Current = FDocSection();
			}

			if (bIsHeading)
			{
				Current.HeadingLevel = Level;
				Current.HeadingText = Trimmed.RightChop(Level).TrimStartAndEnd();
			}

			Current.Markdown += Line;
			Current.Markdown += LINE_TERMINATOR;
		}

		if (!Current.Markdown.TrimStartAndEnd().IsEmpty())
		{
			OutSections.Add(MoveTemp(Current));
		}
	}
}

void FKhaosDocsToolkit::InitEditor(
	const EToolkitMode::Type InMode,
	const TSharedPtr<IToolkitHost>& InToolkitHost,
	UKhaosDocsDocument* InDocument)
{
	check(InDocument);

	const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("KhaosDocsEditor_Layout_v1")
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->SetOrientation(Orient_Vertical)
			->Split
			(
				FTabManager::NewSplitter()
				->SetOrientation(Orient_Horizontal)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.2f)
					->AddTab(OutlineTabId, ETabState::OpenedTab)
				)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.5f)
					->AddTab(PreviewTabId, ETabState::OpenedTab)
					->SetForegroundTab(PreviewTabId)
				)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.3f)
					->AddTab(SourceTabId, ETabState::OpenedTab)
				)
			)
		);

	InitAssetEditor(
		InMode,
		InToolkitHost,
		TEXT("KhaosDocsEditorApp"),
		Layout,
		/*bCreateDefaultStandaloneMenu*/ true,
		/*bCreateDefaultToolbar*/ true,
		InDocument);

	ExtendToolbar();
	RegenerateMenusAndToolbars();
	RefreshPreview();
}

UKhaosDocsDocument* FKhaosDocsToolkit::GetDocument() const
{
	return Cast<UKhaosDocsDocument>(GetEditingObject());
}

void FKhaosDocsToolkit::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	const TSharedRef<FWorkspaceItem> Category =
		InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_KhaosDocs", "Document"));

	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	const FSlateIcon TabIcon(FAppStyle::GetAppStyleSetName(), "Icons.Documentation");

	InTabManager->RegisterTabSpawner(PreviewTabId, FOnSpawnTab::CreateSP(this, &FKhaosDocsToolkit::SpawnTab_Preview))
		.SetDisplayName(LOCTEXT("PreviewTab", "Preview"))
		.SetGroup(Category)
		.SetIcon(TabIcon);

	InTabManager->RegisterTabSpawner(SourceTabId, FOnSpawnTab::CreateSP(this, &FKhaosDocsToolkit::SpawnTab_Source))
		.SetDisplayName(LOCTEXT("SourceTab", "Markdown"))
		.SetGroup(Category)
		.SetIcon(TabIcon);

	InTabManager->RegisterTabSpawner(OutlineTabId, FOnSpawnTab::CreateSP(this, &FKhaosDocsToolkit::SpawnTab_Outline))
		.SetDisplayName(LOCTEXT("OutlineTab", "Outline"))
		.SetGroup(Category)
		.SetIcon(TabIcon);
}

void FKhaosDocsToolkit::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);

	InTabManager->UnregisterTabSpawner(PreviewTabId);
	InTabManager->UnregisterTabSpawner(SourceTabId);
	InTabManager->UnregisterTabSpawner(OutlineTabId);
}

FName FKhaosDocsToolkit::GetToolkitFName() const
{
	return ToolkitName;
}

FText FKhaosDocsToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("BaseToolkitName", "Document Editor");
}

FText FKhaosDocsToolkit::GetToolkitName() const
{
	if (const UKhaosDocsDocument* Document = GetDocument())
	{
		const FText Title = FText::FromString(FPaths::GetCleanFilename(Document->FilePath));
		return Document->IsDirty()
			? FText::Format(LOCTEXT("ToolkitNameDirty", "{0}*"), Title)
			: Title;
	}

	return GetBaseToolkitName();
}

FString FKhaosDocsToolkit::GetWorldCentricTabPrefix() const
{
	return LOCTEXT("WorldCentricTabPrefix", "Document ").ToString();
}

FLinearColor FKhaosDocsToolkit::GetWorldCentricTabColorScale() const
{
	return FLinearColor(0.47f, 0.75f, 1.0f, 0.5f);
}

bool FKhaosDocsToolkit::OnRequestClose(EAssetEditorCloseReason InCloseReason)
{
	UKhaosDocsDocument* Document = GetDocument();
	if (Document && Document->IsDirty())
	{
		const EAppReturnType::Type Choice = FMessageDialog::Open(
			EAppMsgType::YesNoCancel,
			FText::Format(
				LOCTEXT("SaveOnClose", "Save changes to {0}?"),
				FText::FromString(FPaths::GetCleanFilename(Document->FilePath))));

		if (Choice == EAppReturnType::Cancel)
		{
			return false;
		}

		if (Choice == EAppReturnType::Yes && !Document->SaveToDisk())
		{
			FMessageDialog::Open(
				EAppMsgType::Ok,
				FText::Format(
					LOCTEXT("SaveFailed", "Could not write {0}. The file may be read-only or checked in."),
					FText::FromString(Document->FilePath)));
			return false;
		}
	}

	return FAssetEditorToolkit::OnRequestClose(InCloseReason);
}

void FKhaosDocsToolkit::ExtendToolbar()
{
	const TSharedRef<FExtender> Extender = MakeShared<FExtender>();

	Extender->AddToolBarExtension(
		"Asset",
		EExtensionHook::After,
		GetToolkitCommands(),
		FToolBarExtensionDelegate::CreateLambda([this](FToolBarBuilder& Builder)
		{
			Builder.BeginSection("Document");
			{
				Builder.AddToolBarButton(
					FUIAction(
						FExecuteAction::CreateSP(this, &FKhaosDocsToolkit::OnSaveClicked),
						FCanExecuteAction::CreateSP(this, &FKhaosDocsToolkit::IsDocumentDirty)),
					NAME_None,
					LOCTEXT("SaveDocument", "Save"),
					LOCTEXT("SaveDocumentTooltip", "Write this document back to its markdown file on disk."),
					FSlateIcon(FAppStyle::GetAppStyleSetName(), "AssetEditor.SaveAsset"));

				Builder.AddToolBarButton(
					FUIAction(FExecuteAction::CreateSP(this, &FKhaosDocsToolkit::OnReloadClicked)),
					NAME_None,
					LOCTEXT("ReloadDocument", "Reload"),
					LOCTEXT("ReloadDocumentTooltip", "Discard changes and re-read the file from disk."),
					FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Refresh"));

				Builder.AddToolBarButton(
					FUIAction(FExecuteAction::CreateSP(this, &FKhaosDocsToolkit::OnOpenExternallyClicked)),
					NAME_None,
					LOCTEXT("OpenExternally", "Open Externally"),
					LOCTEXT("OpenExternallyTooltip", "Open this markdown file in your default external editor."),
					FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Launch"));
			}
			Builder.EndSection();
		}));

	AddToolbarExtender(Extender);
}

TSharedRef<SDockTab> FKhaosDocsToolkit::SpawnTab_Preview(const FSpawnTabArgs& Args)
{
	TSharedRef<SDockTab> Tab = SNew(SDockTab)
		.Label(LOCTEXT("PreviewTab", "Preview"))
		[
			SAssignNew(PreviewScrollBox, SScrollBox)
		];

	// Repopulate on respawn too - closing and reopening the tab would otherwise leave it blank
	// until the next edit.
	RefreshPreview();

	return Tab;
}

TSharedRef<SDockTab> FKhaosDocsToolkit::SpawnTab_Source(const FSpawnTabArgs& Args)
{
	const UKhaosDocsDocument* Document = GetDocument();

	return SNew(SDockTab)
		.Label(LOCTEXT("SourceTab", "Markdown"))
		[
			SAssignNew(SourceBox, SMultiLineEditableTextBox)
			.Text(FText::FromString(Document ? Document->GetText() : FString()))
			.OnTextChanged(this, &FKhaosDocsToolkit::OnSourceTextChanged)
			// SMultiLineEditableTextBox::TextStyle is deprecated and ignored since 5.2; Font is
			// the supported per-widget override.
			.Font(FCoreStyle::GetDefaultFontStyle("Mono", 9))
			.AutoWrapText(false)
			.AlwaysShowScrollbars(true)
		];
}

TSharedRef<SDockTab> FKhaosDocsToolkit::SpawnTab_Outline(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("OutlineTab", "Outline"))
		[
			SAssignNew(OutlineList, SListView<TSharedPtr<KhaosDocsMarkdown::FHeading>>)
			.ListItemsSource(&Outline)
			.SelectionMode(ESelectionMode::Single)
			.OnGenerateRow_Lambda([](TSharedPtr<KhaosDocsMarkdown::FHeading> Item, const TSharedRef<STableViewBase>& OwnerTable)
			{
				// Indent by heading depth so the outline reads as a hierarchy.
				const float Indent = 8.0f * FMath::Max(0, Item->Level - 1);
				const TCHAR* StyleName = (Item->Level <= 1) ? TEXT("Doc.Bold") : TEXT("Doc.Body");

				return SNew(STableRow<TSharedPtr<KhaosDocsMarkdown::FHeading>>, OwnerTable)
					[
						SNew(STextBlock)
						.Text(FText::FromString(Item->Text))
						.Margin(FMargin(Indent + 4.0f, 2.0f, 4.0f, 2.0f))
						.TextStyle(&FKhaosDocsStyle::Get().GetWidgetStyle<FTextBlockStyle>(FName(StyleName)))
					];
			})
			.OnSelectionChanged_Lambda([this](TSharedPtr<KhaosDocsMarkdown::FHeading> Item, ESelectInfo::Type SelectInfo)
			{
				if (!Item.IsValid() || SelectInfo == ESelectInfo::Direct || !PreviewScrollBox.IsValid())
				{
					return;
				}

				const int32 Index = Outline.IndexOfByKey(Item);
				if (SectionWidgets.IsValidIndex(Index) && SectionWidgets[Index].IsValid())
				{
					PreviewScrollBox->ScrollDescendantIntoView(SectionWidgets[Index], /*bAnimateScroll*/ true);
				}
			})
		];
}

void FKhaosDocsToolkit::RefreshPreview()
{
	const UKhaosDocsDocument* Document = GetDocument();
	if (!Document || !PreviewScrollBox.IsValid())
	{
		return;
	}

	Outline.Reset();
	SectionWidgets.Reset();
	PreviewScrollBox->ClearChildren();

	TArray<FDocSection> Sections;
	SplitSections(Document->GetText(), Sections);

	TArray<TSharedRef<ITextDecorator>> Decorators;
	Decorators.Add(SRichTextBlock::HyperlinkDecorator(
		TEXT("doclink"),
		FSlateHyperlinkRun::FOnClick::CreateSP(this, &FKhaosDocsToolkit::OnLinkClicked)));

	for (const FDocSection& Section : Sections)
	{
		const KhaosDocsMarkdown::FDocument Parsed = KhaosDocsMarkdown::Parse(Section.Markdown);

		TSharedPtr<SWidget> SectionWidget;
		PreviewScrollBox->AddSlot()
			.Padding(12.0f, 4.0f, 12.0f, 4.0f)
			[
				SAssignNew(SectionWidget, SRichTextBlock)
				.Text(Parsed.RichText)
				.TextStyle(&FKhaosDocsStyle::Get().GetWidgetStyle<FTextBlockStyle>("Doc.Body"))
				.DecoratorStyleSet(&FKhaosDocsStyle::Get())
				.Decorators(Decorators)
				.AutoWrapText(true)
			];

		if (Section.HeadingLevel > 0)
		{
			TSharedRef<KhaosDocsMarkdown::FHeading> Heading = MakeShared<KhaosDocsMarkdown::FHeading>();
			Heading->Level = Section.HeadingLevel;
			Heading->Text = Section.HeadingText;

			Outline.Add(Heading);
			SectionWidgets.Add(SectionWidget);
		}
	}

	if (OutlineList.IsValid())
	{
		OutlineList->RequestListRefresh();
	}
}

void FKhaosDocsToolkit::OnSourceTextChanged(const FText& InText)
{
	if (UKhaosDocsDocument* Document = GetDocument())
	{
		Document->SetText(InText.ToString());
		RefreshPreview();
	}
}

void FKhaosDocsToolkit::OnSaveClicked()
{
	UKhaosDocsDocument* Document = GetDocument();
	if (!Document)
	{
		return;
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
			return;
		}
	}

	if (!Document->SaveToDisk())
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			FText::Format(
				LOCTEXT("SaveFailedToolbar", "Could not write {0}. The file may be read-only or checked in."),
				FText::FromString(Document->FilePath)));
	}
}

void FKhaosDocsToolkit::OnReloadClicked()
{
	UKhaosDocsDocument* Document = GetDocument();
	if (!Document)
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
		if (SourceBox.IsValid())
		{
			SourceBox->SetText(FText::FromString(Document->GetText()));
		}
		RefreshPreview();
	}
}

void FKhaosDocsToolkit::OnOpenExternallyClicked()
{
	if (const UKhaosDocsDocument* Document = GetDocument())
	{
		FPlatformProcess::LaunchFileInDefaultExternalApplication(*Document->FilePath, nullptr, ELaunchVerb::Edit);
	}
}

bool FKhaosDocsToolkit::IsDocumentDirty() const
{
	const UKhaosDocsDocument* Document = GetDocument();
	return Document && Document->IsDirty();
}

void FKhaosDocsToolkit::OnLinkClicked(const FSlateHyperlinkRun::FMetadata& InMetadata)
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

	const UKhaosDocsDocument* Document = GetDocument();
	if (!Document)
	{
		return;
	}

	// Relative links resolve against the folder holding this document, so cross-document links
	// written for GitHub ("[see](Setup.md)") work unchanged inside the editor.
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
	if (FPaths::FileExists(Resolved))
	{
		IKhaosDocsEditorModule::Get().OpenDocument(Resolved);
	}
}

#undef LOCTEXT_NAMESPACE
