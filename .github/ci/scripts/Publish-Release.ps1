<#
.SYNOPSIS
Publish tested source zips as a GitHub release and write the Fab submission checklist.

.DESCRIPTION
Fab has no publishing API. A code plugin version is submitted in the Fab publisher portal by
pasting a public download link to its source zip, per engine version. This puts the zips on a
GitHub release, which gives them that link, and writes the links and version notes to the job
summary so the portal step is copy and paste.

Release assets are only downloadable without signing in when the repository is public. For a
private repository the release is still made, for the team, and the summary says the zips need
hosting elsewhere before Fab can fetch them.

Re-running a release replaces the zips on the existing release rather than failing.

.EXAMPLE
./Publish-Release.ps1 -PluginDir ./plugin -PackageDir ./dist -Tag v1.2.0 -Repository KhaosGames/KhaosDocs
#>
[CmdletBinding()]
param(
	# The plugin checkout, with history, for the release notes.
	[Parameter(Mandatory)][string]$PluginDir,
	[Parameter(Mandatory)][string]$PackageDir,
	[Parameter(Mandatory)][string]$Tag,
	[Parameter(Mandatory)][string]$Repository
)

$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'KhaosPluginCI.psm1') -Force

$Descriptor = Get-PluginDescriptor $PluginDir
$Release = ConvertFrom-ReleaseVersion $Tag
$Zips = @(Get-ChildItem -LiteralPath $PackageDir -Filter '*.zip' | Sort-Object Name)
if ($Zips.Count -eq 0) { throw "No zips in $PackageDir." }

$VersionNotes = Get-ReleaseNotes -RepoDir $PluginDir -Tag $Tag

$Checksums = @('| Package | SHA-256 |', '| --- | --- |')
foreach ($Zip in $Zips)
{
	$Checksums += "| ``$($Zip.Name)`` | ``$(Get-FileSha256 $Zip.FullName)`` |"
}

$Body = @(
	$VersionNotes
	''
	'### Packages'
	''
	'Source packages as submitted to Fab, one per engine version. Each was compiled with BuildPlugin and tested against that engine before this release was made.'
	''
	$Checksums
) -join "`n"
$NotesFile = Join-Paths ([IO.Path]::GetTempPath()) "release-notes-$Tag.md"
Write-Utf8File $NotesFile $Body

# Create, or refresh on a re-run ----------------------------------------------------------------

$Assets = @($Zips | ForEach-Object { $_.FullName })
$Assets += @(Get-ChildItem -LiteralPath $PackageDir -Filter '*.sha256' | ForEach-Object { $_.FullName })

if ((Invoke-NativeQuiet gh @('release', 'view', $Tag, '--repo', $Repository)).ExitCode -eq 0)
{
	Write-Host "Release $Tag exists; replacing its packages."
	& gh release upload $Tag @Assets --repo $Repository --clobber
	if ($LASTEXITCODE -ne 0) { throw "Uploading to release $Tag failed." }
}
else
{
	$Arguments = @('release', 'create', $Tag) + $Assets + @('--repo', $Repository, '--verify-tag', '--title', "$($Descriptor.Name) $($Release.VersionName)", '--notes-file', $NotesFile)
	if ($Release.VersionName.Contains('-')) { $Arguments += '--prerelease' }
	& gh @Arguments
	if ($LASTEXITCODE -ne 0) { throw "Creating release $Tag failed." }
}

# Fab checklist ---------------------------------------------------------------------------------

$IsPrivate = (Invoke-NativeQuiet gh @('api', "repos/$Repository", '--jq', '.private')).Output -contains 'true'

$Summary = @(
	"## Submit $($Descriptor.Name) $($Release.VersionName) to Fab"
	''
)
if ($IsPrivate)
{
	$Summary += "> [!WARNING]"
	$Summary += "> $Repository is private, so these links need a GitHub sign-in and Fab cannot download them. Host the zips somewhere public (or behind an unguessable URL) and use those links instead."
	$Summary += ''
}
$Summary += 'In the [Fab publisher portal](https://www.fab.com/portal/listings), open the listing, go to its Unreal Engine format, and for each engine version below paste the link as the project file link and the notes as the version notes. Then submit for review.'
$Summary += ''
$Summary += '| Engine | Project file link |'
$Summary += '| --- | --- |'
foreach ($Zip in $Zips)
{
	$Engine = [regex]::Match($Zip.Name, 'UE(\d+\.\d+)\.zip$').Groups[1].Value
	$Url = "https://github.com/$Repository/releases/download/$Tag/$($Zip.Name)"
	$Summary += "| $Engine | $Url |"
}
$Summary += ''
$Summary += '**Version notes**'
$Summary += ''
$Summary += '```text'
$Summary += $VersionNotes
$Summary += '```'
$Summary += ''
$Summary += "Keep the release and its zips until Fab has approved the version; Fab downloads them during review."
if (-not (Get-JsonValue $Descriptor.Json 'FabURL'))
{
	$Summary += ''
	$Summary += 'Once the listing is live, set `"FabURL"` in the .uplugin to its address and include that in the next update.'
}

Add-CiSummary ($Summary -join "`n")
Write-Host "Published https://github.com/$Repository/releases/tag/$Tag"
Set-CiOutput 'url' "https://github.com/$Repository/releases/tag/$Tag"
