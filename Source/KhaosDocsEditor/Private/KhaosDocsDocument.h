// Copyright Khaos Games. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "KhaosDocsDocument.generated.h"

/**
 * A markdown file on disk, wrapped in a transient UObject.
 *
 * This object is never saved into a package and never appears in the asset registry - it exists
 * only while the document is open. The file on disk stays plain markdown, which is the whole
 * point: docs remain diffable, editable outside Unreal, and free of .uasset baggage.
 *
 * The class names the Content Browser file type. The Content Browser will only draw a class
 * thumbnail for a type that resolves to a real UClass, so the file data source registers documents
 * under this class path and the style set keys its icons by this class name. The DisplayName is
 * what the Content Browser tooltip shows as the type.
 */
UCLASS(Transient, meta = (DisplayName = "Document"))
class UKhaosDocsDocument : public UObject
{
	GENERATED_BODY()

public:
	/** Absolute path of the markdown file this document mirrors. */
	UPROPERTY(VisibleAnywhere, Category = "Document")
	FString FilePath;

	/** Content Browser path, e.g. "/KhaosUI/Docs/Overview.md". Empty for files opened off disk. */
	UPROPERTY(VisibleAnywhere, Category = "Document")
	FString VirtualPath;

	/** Read the file into memory, clearing the dirty flag. Returns false if the file is unreadable. */
	bool LoadFromDisk();

	/** Write the in-memory text back to disk as UTF-8, clearing the dirty flag. */
	bool SaveToDisk();

	/** True if the file has been modified on disk since we last read or wrote it. */
	bool HasExternalChanges() const;

	const FString& GetText() const { return Text; }
	void SetText(const FString& InText);

	bool IsDirty() const { return bDirty; }

private:
	/** Working copy of the file contents. */
	FString Text;

	/** True when Text differs from what is on disk. */
	bool bDirty = false;

	/** Modification stamp captured at the last successful load or save. */
	FDateTime DiskTimeStamp = FDateTime::MinValue();
};
