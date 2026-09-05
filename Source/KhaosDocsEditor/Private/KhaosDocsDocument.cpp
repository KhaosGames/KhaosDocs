// Copyright Khaos Games. All Rights Reserved.

#include "KhaosDocsDocument.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

bool UKhaosDocsDocument::LoadFromDisk()
{
	if (FilePath.IsEmpty())
	{
		return false;
	}

	FString Loaded;
	if (!FFileHelper::LoadFileToString(Loaded, *FilePath))
	{
		return false;
	}

	Text = MoveTemp(Loaded);
	bDirty = false;
	DiskTimeStamp = IFileManager::Get().GetTimeStamp(*FilePath);
	return true;
}

bool UKhaosDocsDocument::SaveToDisk()
{
	if (FilePath.IsEmpty())
	{
		return false;
	}

	// UTF-8 without BOM keeps the file friendly to git, GitHub and every external editor.
	if (!FFileHelper::SaveStringToFile(Text, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		return false;
	}

	bDirty = false;
	DiskTimeStamp = IFileManager::Get().GetTimeStamp(*FilePath);
	return true;
}

bool UKhaosDocsDocument::HasExternalChanges() const
{
	if (FilePath.IsEmpty() || DiskTimeStamp == FDateTime::MinValue())
	{
		return false;
	}

	const FDateTime Current = IFileManager::Get().GetTimeStamp(*FilePath);
	return Current != FDateTime::MinValue() && Current > DiskTimeStamp;
}

void UKhaosDocsDocument::SetText(const FString& InText)
{
	if (Text != InText)
	{
		Text = InText;
		bDirty = true;
	}
}
