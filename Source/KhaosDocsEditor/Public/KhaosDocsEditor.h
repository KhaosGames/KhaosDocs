// Copyright Khaos Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

namespace KhaosDocs
{
	/**
	 * Extensions treated as documents. Lowercase, no leading dot.
	 * The first entry is the primary one, used when creating a new document.
	 */
	KHAOSDOCSEDITOR_API TArray<FString> GetDocumentExtensions();

	/**
	 * A project content root that is scanned for markdown.
	 *
	 * Whole content roots are mounted rather than a single docs subfolder, so a .md file shows up
	 * wherever it sits - next to the assets it describes, if that is where it belongs. Engine
	 * content is excluded; only the project and its own plugins are scanned.
	 */
	struct KHAOSDOCSEDITOR_API FDocRoot
	{
		/** Display name of the owner - the project name, or the plugin name. */
		FString Owner;

		/** Content Browser path of the root, e.g. "/KhaosUI". */
		FString VirtualPath;

		/** Absolute path of the Content folder, e.g. ".../Plugins/KhaosUI/Content". May not exist. */
		FString DiskPath;

		/**
		 * Absolute path of the owner's base folder - the folder holding the .uplugin, or the
		 * project folder. Documents outside Content are found relative to this.
		 */
		FString BaseDir;
	};

	/** Enumerate every project content root (the game plus project plugins). Engine roots are skipped. */
	KHAOSDOCSEDITOR_API void FindDocRoots(TArray<FDocRoot>& OutRoots);

	/**
	 * Gather every markdown file belonging to a root, for the documentation window.
	 *
	 * Covers three places: all of Content recursively, a Docs folder beside the .uplugin, and any
	 * markdown sitting directly in the base folder (README.md and friends). The base folder is not
	 * searched recursively - doing so would walk Source, Binaries, Intermediate and any vendored
	 * third-party readme.
	 *
	 * Only Content is mounted into the Content Browser. The other two are reachable from the
	 * documentation window and open in the document editor like any other file.
	 */
	KHAOSDOCSEDITOR_API void FindDocsInRoot(const FDocRoot& InRoot, TArray<FString>& OutFiles);

	/** Human readable title for a document - its first H1 if present, otherwise the file name. */
	KHAOSDOCSEDITOR_API FString GetDocumentTitle(const FString& InFilePath);
}

class KHAOSDOCSEDITOR_API IKhaosDocsEditorModule : public IModuleInterface
{
public:
	static IKhaosDocsEditorModule& Get()
	{
		return FModuleManager::LoadModuleChecked<IKhaosDocsEditorModule>(TEXT("KhaosDocsEditor"));
	}

	static bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded(TEXT("KhaosDocsEditor"));
	}

	/** Show a document in the documentation window, opening the window if needed. */
	virtual bool OpenDocument(const FString& InFilePath) = 0;

	/** Summon the project-wide table of contents tab. */
	virtual void OpenDocsBrowser() = 0;

	/** Mount any project content root that has appeared since startup. */
	virtual void RefreshMounts() = 0;
};
