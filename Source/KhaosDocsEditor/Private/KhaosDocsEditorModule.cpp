// Copyright Khaos Games. All Rights Reserved.

#include "KhaosDocsEditor.h"

#include "ContentBrowserDataSubsystem.h"
#include "ContentBrowserFileDataCore.h"
#include "ContentBrowserFileDataSource.h"
#include "IContentBrowserDataModule.h"
#include "Misc/CoreDelegates.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "KhaosDocsDocument.h"
#include "KhaosDocsStyle.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "SKhaosDocsBrowser.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/StructOnScope.h"
#include "UObject/TopLevelAssetPath.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "KhaosDocsEditor"

TArray<FString> KhaosDocs::GetDocumentExtensions()
{
	// Every extension registers its own Content Browser file type, so the list is deliberately
	// short. Formats that are not markdown (.rst, .adoc, Epic's own .udn) would render wrong
	// rather than merely plain, and would need their own parser instead of an entry here.
	return { TEXT("md"), TEXT("txt") };
}

namespace
{
	const FName KhaosDocsBrowserTabId(TEXT("KhaosDocsBrowser"));

	/** Must match the name given to the data source object, since activation is looked up by name. */
	const FName KhaosDocsDataSourceName(TEXT("KhaosDocsData"));

	/** Content Browser path of a mounted root without its trailing slash, e.g. "/KhaosUI". */
	FString MakeRootVirtualPath(const FString& InAssetPath)
	{
		FString Trimmed = InAssetPath;
		Trimmed.RemoveFromEnd(TEXT("/"));
		return Trimmed;
	}

	/**
	 * Absolute disk path of a mounted root, e.g. ".../Plugins/KhaosUI/Content".
	 * AddFileMount asserts on a trailing slash and root paths arrive with one, so normalize.
	 */
	FString MakeRootDiskPath(const FString& InFilesystemPath)
	{
		FString DiskPath = FPaths::ConvertRelativePathToFull(InFilesystemPath);
		FPaths::NormalizeDirectoryName(DiskPath);
		return DiskPath;
	}

	/**
	 * Whether a content root belongs to this project rather than the engine.
	 *
	 * Engine content is excluded deliberately: scanning several hundred engine plugins for markdown
	 * would cost a lot of directory walking and surface documents nobody here maintains.
	 */
	bool IsProjectContentRoot(const FString& InAssetPath)
	{
		const FString Trimmed = MakeRootVirtualPath(InAssetPath);

		if (Trimmed.IsEmpty() || Trimmed.Equals(TEXT("/Temp"), ESearchCase::IgnoreCase))
		{
			return false;
		}

		if (Trimmed.Equals(TEXT("/Game"), ESearchCase::IgnoreCase))
		{
			return true;
		}

		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(Trimmed.RightChop(1));
		return Plugin.IsValid() && Plugin->GetLoadedFrom() == EPluginLoadedFrom::Project;
	}
}

void KhaosDocs::FindDocRoots(TArray<FDocRoot>& OutRoots)
{
	TArray<FString> RootPaths;
	FPackageName::QueryRootContentPaths(RootPaths);

	for (const FString& RootPath : RootPaths)
	{
		if (!IsProjectContentRoot(RootPath))
		{
			continue;
		}

		const FString VirtualPath = MakeRootVirtualPath(RootPath);
		const bool bIsProject = VirtualPath.Equals(TEXT("/Game"), ESearchCase::IgnoreCase);

		FString BaseDir;
		if (bIsProject)
		{
			BaseDir = FPaths::ProjectDir();
		}
		else if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(VirtualPath.RightChop(1)))
		{
			BaseDir = Plugin->GetBaseDir();
		}
		else
		{
			continue;
		}

		FDocRoot& Root = OutRoots.AddDefaulted_GetRef();
		// "/Game" is the project itself; show its real name rather than the mount alias.
		Root.Owner = bIsProject ? FApp::GetProjectName() : VirtualPath.RightChop(1);
		Root.VirtualPath = VirtualPath;
		Root.DiskPath = MakeRootDiskPath(FPackageName::LongPackageNameToFilename(RootPath));
		Root.BaseDir = MakeRootDiskPath(BaseDir);
	}
}

void KhaosDocs::FindDocsInRoot(const FDocRoot& InRoot, TArray<FString>& OutFiles)
{
	IFileManager& FileManager = IFileManager::Get();
	const FString SiblingDocsDir = InRoot.BaseDir / TEXT("Docs");
	const bool bHasSiblingDocs = FPaths::DirectoryExists(SiblingDocsDir);

	TArray<FString> Found;

	for (const FString& Extension : GetDocumentExtensions())
	{
		const FString Wildcard = FString::Printf(TEXT("*.%s"), *Extension);

		// Everything under Content. This is the set the Content Browser also shows.
		FileManager.FindFilesRecursive(Found, *InRoot.DiskPath, *Wildcard, /*Files*/ true, /*Directories*/ false, /*bClearFileNames*/ false);

		// A Docs folder beside the .uplugin, for documentation that is not shipped as content.
		if (bHasSiblingDocs)
		{
			FileManager.FindFilesRecursive(Found, *SiblingDocsDir, *Wildcard, /*Files*/ true, /*Directories*/ false, /*bClearFileNames*/ false);
		}

		// Documents sitting directly in the base folder: README.md, CHANGELOG.md and the like.
		// Not recursive, deliberately - see the comment on this function.
		TArray<FString> BaseNames;
		FileManager.FindFiles(BaseNames, *InRoot.BaseDir, *Extension);
		for (const FString& Name : BaseNames)
		{
			Found.Add(InRoot.BaseDir / Name);
		}
	}

	for (FString& File : Found)
	{
		// A plugin whose Content sits inside its base folder can match the same file twice.
		OutFiles.AddUnique(FPaths::ConvertRelativePathToFull(MoveTemp(File)));
	}
}

FString KhaosDocs::GetDocumentTitle(const FString& InFilePath)
{
	// Read only far enough to find the first H1 - the table of contents scans every file on
	// refresh, so parsing whole documents here would be wasteful.
	TArray<FString> Lines;
	if (FFileHelper::LoadFileToStringArrayWithPredicate(Lines, *InFilePath, [](const FString& Line)
		{
			return Line.TrimStart().StartsWith(TEXT("# "), ESearchCase::CaseSensitive);
		}))
	{
		if (Lines.Num() > 0)
		{
			return Lines[0].TrimStart().RightChop(2).TrimStartAndEnd();
		}
	}

	return FPaths::GetBaseFilename(InFilePath);
}

/**
 * Editor module for the docs system.
 *
 * Registers a Content Browser file data source for each document extension, mounts every project
 * content root into it, and owns the documentation window.
 */
class FKhaosDocsEditorModule : public IKhaosDocsEditorModule
{
public:
	//~ Begin IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface

	//~ Begin IKhaosDocsEditorModule
	virtual bool OpenDocument(const FString& InFilePath) override;
	virtual void OpenDocsBrowser() override;
	virtual void RefreshMounts() override;
	//~ End IKhaosDocsEditorModule

private:
	void RegisterDataSource();
	void ActivateDataSource();
	void RegisterMenus();
	void RegisterTabs();

	void MountRoot(const FString& InAssetPath, const FString& InFilesystemPath);
	void OnContentPathMounted(const FString& InAssetPath, const FString& InFilesystemPath);
	void OnContentPathDismounted(const FString& InAssetPath, const FString& InFilesystemPath);

	/** Bound to FFileActions::Edit - double-clicking a .md in the Content Browser lands here. */
	bool OnEditDocument(const FName InFilePath, const FString& InFilename);

	/** Bound to FFileActions::Create - writes the starter file behind "Create Document". */
	static bool OnCreateDocument(const FName InFilePath, const FString& InFilename, const FStructOnScope& InConfig);

	TSharedRef<SDockTab> SpawnDocsBrowserTab(const FSpawnTabArgs& Args);

	TStrongObjectPtr<UContentBrowserFileDataSource> DataSource;

	TWeakPtr<SKhaosDocsBrowser> DocsBrowser;
};

void FKhaosDocsEditorModule::StartupModule()
{
	FKhaosDocsStyle::Initialize();

	if (!GIsEditor || IsRunningCommandlet())
	{
		return;
	}

	RegisterDataSource();

	// Activation needs the Content Browser data subsystem, which is an editor subsystem and so
	// does not exist yet at module startup. PostEngineInit is the first point it is available.
	FCoreDelegates::GetOnPostEngineInit().AddRaw(this, &FKhaosDocsEditorModule::ActivateDataSource);

	FPackageName::OnContentPathMounted().AddRaw(this, &FKhaosDocsEditorModule::OnContentPathMounted);
	FPackageName::OnContentPathDismounted().AddRaw(this, &FKhaosDocsEditorModule::OnContentPathDismounted);

	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FKhaosDocsEditorModule::RegisterMenus));

	RegisterTabs();
}

void FKhaosDocsEditorModule::ShutdownModule()
{
	if (UObjectInitialized())
	{
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);
	}

	if (FSlateApplication::IsInitialized())
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(KhaosDocsBrowserTabId);
	}

	FCoreDelegates::GetOnPostEngineInit().RemoveAll(this);
	FPackageName::OnContentPathMounted().RemoveAll(this);
	FPackageName::OnContentPathDismounted().RemoveAll(this);

	DataSource.Reset();

	FKhaosDocsStyle::Shutdown();
}

void FKhaosDocsEditorModule::RegisterDataSource()
{
	ContentBrowserFileData::FFileConfigData Config;

	// Folders inside a docs tree behave like ordinary folders so docs can be organised in
	// subdirectories, which is also how they read on GitHub.
	ContentBrowserFileData::FDirectoryActions DirectoryActions;
	DirectoryActions.PassesFilter.BindStatic(&ContentBrowserFileData::FDefaultFileActions::ItemPassesFilter, false);
	DirectoryActions.GetAttribute.BindStatic(&ContentBrowserFileData::FDefaultFileActions::GetItemAttribute);
	Config.SetDirectoryActions(DirectoryActions);

	// Every extension registers as the same "Document" type and is rendered as markdown. The data
	// source is keyed by extension, so each still needs its own registration.
	const TArray<FString> Extensions = KhaosDocs::GetDocumentExtensions();
	for (int32 Index = 0; Index < Extensions.Num(); ++Index)
	{
		// The Add-New menu builds its entry name from the type name, so with a shared type only the
		// primary extension offers "Create Document" - otherwise every extension would contribute a
		// colliding entry. CanCreate is the engine's own hook for exactly this (see
		// UContentBrowserFileDataSource::PopulateAddNewContextMenu).
		const bool bIsPrimary = (Index == 0);

		ContentBrowserFileData::FFileActions FileActions;
		FileActions.TypeExtension = Extensions[Index];
		// A real class path, even though these files are never loaded as objects. The Content
		// Browser draws a class thumbnail only when the type resolves to a UClass, and takes the
		// type's display name from it, so a synthetic path would leave every document with a
		// blank tile and a default icon in its tooltip.
		FileActions.TypeName = UKhaosDocsDocument::StaticClass()->GetClassPathName();
		FileActions.TypeDisplayName = LOCTEXT("DocTypeName", "Document");
		FileActions.TypeShortDescription = LOCTEXT("DocTypeShortDescription", "Document");
		FileActions.TypeFullDescription = LOCTEXT("DocTypeFullDescription", "A document stored as a plain text file on disk");
		FileActions.DefaultNewFileName = TEXT("NewDocument");
		FileActions.TypeColor = FColor(120, 190, 255);
		FileActions.PassesFilter.BindStatic(&ContentBrowserFileData::FDefaultFileActions::ItemPassesFilter, true);
		FileActions.GetAttribute.BindStatic(&ContentBrowserFileData::FDefaultFileActions::GetItemAttribute);
		FileActions.CanCreate.BindLambda([bIsPrimary](const FName, const FString&, FText*) { return bIsPrimary; });
		FileActions.Create.BindStatic(&FKhaosDocsEditorModule::OnCreateDocument);
		FileActions.Edit.BindRaw(this, &FKhaosDocsEditorModule::OnEditDocument);
		Config.RegisterFileActions(FileActions);
	}

	DataSource.Reset(NewObject<UContentBrowserFileDataSource>(GetTransientPackage(), KhaosDocsDataSourceName));
	DataSource->Initialize(Config);

	RefreshMounts();
}

void FKhaosDocsEditorModule::ActivateDataSource()
{
	if (!DataSource)
	{
		return;
	}

	// Registering a data source only makes it *available*. The Content Browser ignores it until it
	// is explicitly activated by name - see UContentBrowserDataSubsystem::HandleDataSourceRegistered,
	// which activates on registration only if the name is already in its enabled list.
	if (UContentBrowserDataSubsystem* ContentBrowserData = IContentBrowserDataModule::Get().GetSubsystem())
	{
		ContentBrowserData->ActivateDataSource(KhaosDocsDataSourceName);
	}
}

void FKhaosDocsEditorModule::RefreshMounts()
{
	if (!DataSource)
	{
		return;
	}

	TArray<FString> RootPaths;
	FPackageName::QueryRootContentPaths(RootPaths);

	for (const FString& RootPath : RootPaths)
	{
		MountRoot(RootPath, FPackageName::LongPackageNameToFilename(RootPath));
	}
}

void FKhaosDocsEditorModule::MountRoot(const FString& InAssetPath, const FString& InFilesystemPath)
{
	if (!DataSource)
	{
		return;
	}

	if (!IsProjectContentRoot(InAssetPath))
	{
		return;
	}

	const FString DiskPath = MakeRootDiskPath(InFilesystemPath);
	const FString VirtualPath = MakeRootVirtualPath(InAssetPath);

	if (VirtualPath.IsEmpty() || !FPaths::DirectoryExists(DiskPath))
	{
		return;
	}

	// The whole content root is mounted, so markdown is found wherever it lives rather than only
	// in a reserved folder. Discovery filters by extension as it walks, so this costs a directory
	// scan and picks up nothing but .md files.
	if (!DataSource->HasFileMount(*VirtualPath))
	{
		DataSource->AddFileMount(*VirtualPath, DiskPath);
	}
}

void FKhaosDocsEditorModule::OnContentPathMounted(const FString& InAssetPath, const FString& InFilesystemPath)
{
	MountRoot(InAssetPath, InFilesystemPath);
}

void FKhaosDocsEditorModule::OnContentPathDismounted(const FString& InAssetPath, const FString& InFilesystemPath)
{
	if (DataSource)
	{
		DataSource->RemoveFileMount(*MakeRootVirtualPath(InAssetPath));
	}
}

bool FKhaosDocsEditorModule::OnCreateDocument(const FName InFilePath, const FString& InFilename, const FStructOnScope& InConfig)
{
	// Seed the file with an H1 matching the name the user just typed, so the new document already
	// has a title in the table of contents. Every document type is markdown, so this holds
	// whatever extension the file ends up with.
	const FString Title = FPaths::GetBaseFilename(InFilename);
	const FString Contents = FString::Printf(
		TEXT("# %s") LINE_TERMINATOR LINE_TERMINATOR TEXT("Write your documentation here.") LINE_TERMINATOR,
		*Title);

	return FFileHelper::SaveStringToFile(Contents, *InFilename, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

bool FKhaosDocsEditorModule::OnEditDocument(const FName InFilePath, const FString& InFilename)
{
	return OpenDocument(InFilename);
}

bool FKhaosDocsEditorModule::OpenDocument(const FString& InFilePath)
{
	const FString FullPath = FPaths::ConvertRelativePathToFull(InFilePath);
	if (!FPaths::FileExists(FullPath))
	{
		return false;
	}

	// Everything goes through the documentation window: one place to read, one place to edit.
	OpenDocsBrowser();

	if (const TSharedPtr<SKhaosDocsBrowser> Browser = DocsBrowser.Pin())
	{
		Browser->ShowDocument(FullPath);
		return true;
	}

	return false;
}

void FKhaosDocsEditorModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	if (UToolMenu* Toolbar = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.User"))
	{
		FToolMenuSection& Section = Toolbar->FindOrAddSection("KhaosDocs");
		Section.AddEntry(FToolMenuEntry::InitToolBarButton(
			"OpenKhaosDocs",
			FUIAction(FExecuteAction::CreateRaw(this, &FKhaosDocsEditorModule::OpenDocsBrowser)),
			LOCTEXT("DocsToolbarLabel", "Docs"),
			LOCTEXT("DocsToolbarTooltip", "Open the project documentation browser."),
			FSlateIcon(FKhaosDocsStyle::GetStyleSetName(), "KhaosDocs.Icon")));
	}
}

void FKhaosDocsEditorModule::RegisterTabs()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
			KhaosDocsBrowserTabId,
			FOnSpawnTab::CreateRaw(this, &FKhaosDocsEditorModule::SpawnDocsBrowserTab))
		.SetDisplayName(LOCTEXT("DocsTabTitle", "Documentation"))
		.SetTooltipText(LOCTEXT("DocsTabTooltip", "Browse documentation from every plugin in this project."))
		.SetIcon(FSlateIcon(FKhaosDocsStyle::GetStyleSetName(), "KhaosDocs.Icon"))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());
}

TSharedRef<SDockTab> FKhaosDocsEditorModule::SpawnDocsBrowserTab(const FSpawnTabArgs& Args)
{
	TSharedRef<SKhaosDocsBrowser> Browser = SNew(SKhaosDocsBrowser);
	DocsBrowser = Browser;

	// A weak reference so the tab's delegate never keeps its own content alive.
	const TWeakPtr<SKhaosDocsBrowser> WeakBrowser = Browser;

	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		.Label(LOCTEXT("DocsTabTitle", "Documentation"))
		.OnCanCloseTab_Lambda([WeakBrowser]()
		{
			const TSharedPtr<SKhaosDocsBrowser> Pinned = WeakBrowser.Pin();
			return !Pinned.IsValid() || Pinned->CanClose();
		})
		[
			Browser
		];
}

void FKhaosDocsEditorModule::OpenDocsBrowser()
{
	// Pick up docs folders created since the editor started before showing the window.
	RefreshMounts();

	// Invoking the tab spawns the browser the first time, which scans on construction; after that
	// the existing one is brought forward and rescanned so new files show up.
	const bool bAlreadyOpen = DocsBrowser.IsValid();
	FGlobalTabmanager::Get()->TryInvokeTab(KhaosDocsBrowserTabId);

	if (bAlreadyOpen)
	{
		if (const TSharedPtr<SKhaosDocsBrowser> Browser = DocsBrowser.Pin())
		{
			Browser->Refresh();
		}
	}
}

IMPLEMENT_MODULE(FKhaosDocsEditorModule, KhaosDocsEditor)

#undef LOCTEXT_NAMESPACE
