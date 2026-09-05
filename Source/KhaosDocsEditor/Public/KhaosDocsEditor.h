// Copyright Khaos Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

namespace KhaosDocs
{
	/** File extension (no dot) handled by the docs data source. */
	KHAOSDOCSEDITOR_API extern const TCHAR* DocsFileExtension;

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

		/** Absolute path on disk, e.g. ".../Plugins/KhaosUI/Content". */
		FString DiskPath;
	};

	/** Enumerate every project content root (the game plus project plugins). Engine roots are skipped. */
	KHAOSDOCSEDITOR_API void FindDocRoots(TArray<FDocRoot>& OutRoots);

	/** Recursively gather absolute paths of every markdown file under a content root. */
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

	/** Open a markdown file in the in-editor document editor. Focuses an existing editor if one is open. */
	virtual bool OpenDocument(const FString& InFilePath) = 0;

	/** Summon the project-wide table of contents tab. */
	virtual void OpenDocsBrowser() = 0;

	/** Mount any project content root that has appeared since startup. */
	virtual void RefreshMounts() = 0;
};
