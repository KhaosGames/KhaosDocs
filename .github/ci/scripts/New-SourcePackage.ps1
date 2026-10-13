<#
.SYNOPSIS
Validate a plugin and write the source zips that get submitted to Fab, one per engine version.

.DESCRIPTION
Each zip holds one folder named after the plugin, containing only what Fab expects of a code
plugin: the descriptor, Source, Content, Config, Resources and Shaders, plus anything
Config/FilterPlugin.ini adds. The descriptor in each zip is stamped with its EngineVersion and,
for a release, with the version taken from the tag.

These zips are the artifact everything else works from. CI builds and tests the zip, not the
checkout, so what passed is byte for byte what gets uploaded.

.EXAMPLE
./New-SourcePackage.ps1 -PluginDir ../KhaosDocs -OutDir ./dist -EngineVersions 5.8 -Version 1.2.0
#>
[CmdletBinding()]
param(
	[Parameter(Mandatory)][string]$PluginDir,
	[Parameter(Mandatory)][string]$OutDir,

	# Engine versions as '5.7,5.8' or a JSON array, as passed from a workflow input.
	[Parameter(Mandatory)][string]$EngineVersions,

	# Release version, e.g. 1.2.0 or v1.2.0. Omit for a CI build to keep the descriptor's own.
	[string]$Version,

	# Name the copyright notices must carry. Defaults to the descriptor's CreatedBy.
	[string]$CopyrightHolder
)

$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'KhaosPluginCI.psm1') -Force

if ($EngineVersions.TrimStart().StartsWith('['))
{
	$Engines = @($EngineVersions | ConvertFrom-Json)
}
else
{
	$Engines = @($EngineVersions.Split(',') | ForEach-Object { $_.Trim() } | Where-Object { $_ })
}
if ($Engines.Count -eq 0) { throw 'No engine versions given.' }
foreach ($Engine in $Engines)
{
	if ($Engine -notmatch '^\d+\.\d+$') { throw "Engine version '$Engine' should look like 5.8." }
}

$Descriptor = Get-PluginDescriptor $PluginDir
$Files = Get-PluginPackageFiles $Descriptor

Start-CiGroup "Validate $($Descriptor.Name)"
Write-Host "$($Files.Count) files in the package:"
$Files | ForEach-Object { Write-Host "  $_" }
$Result = Test-PluginPackage -Descriptor $Descriptor -Files $Files -CopyrightHolder $CopyrightHolder
Stop-CiGroup

if ($Result.Errors -gt 0)
{
	throw "$($Descriptor.Name) has $($Result.Errors) problem(s) that would get it declined on Fab. See the errors above."
}

$Text = $Descriptor.Raw
if ($Version)
{
	$Release = ConvertFrom-ReleaseVersion $Version
	$Text = Set-DescriptorField $Text 'VersionName' $Release.VersionName
	$Text = Set-DescriptorField $Text 'Version' $Release.Version
	$VersionName = $Release.VersionName
}
else
{
	$VersionName = [string](Get-JsonValue $Descriptor.Json 'VersionName')
}

New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
$OutDir = Get-FullPath $OutDir
$Staging = Join-Paths ([IO.Path]::GetTempPath()) ("plugin-package-" + [Guid]::NewGuid().ToString('N').Substring(0, 8))

$Summary = @(
	"### $($Descriptor.Name) $VersionName source packages"
	''
	'| Engine | Package | SHA-256 |'
	'| --- | --- | --- |'
)

try
{
	foreach ($Engine in $Engines)
	{
		Reset-Directory $Staging
		foreach ($File in $Files)
		{
			$Target = Join-Paths $Staging $File
			New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($Target)) -Force | Out-Null
			Copy-Item -LiteralPath (Join-Paths $Descriptor.Dir $File) -Destination $Target
		}

		$Stamped = Set-DescriptorField $Text 'EngineVersion' "$Engine.0"
		Write-Utf8File (Join-Paths $Staging $Descriptor.FileName) $Stamped

		$ZipName = "$($Descriptor.Name)-$VersionName-UE$Engine.zip"
		$ZipPath = Join-Paths $OutDir $ZipName
		New-PluginZip -SourceDir $Staging -Files $Files -RootFolder $Descriptor.Name -ZipPath $ZipPath

		$Hash = Get-FileSha256 $ZipPath
		Write-Utf8File "$ZipPath.sha256" "$Hash  $ZipName`n"
		$SizeKb = [math]::Ceiling((Get-Item -LiteralPath $ZipPath).Length / 1KB)
		Write-Host "Wrote $ZipName ($SizeKb KB, sha256 $Hash)"
		$Summary += "| $Engine | ``$ZipName`` ($SizeKb KB) | ``$Hash`` |"
	}
}
finally
{
	if (Test-Path -LiteralPath $Staging) { Remove-Item -LiteralPath $Staging -Recurse -Force }
}

if ($Result.Warnings -gt 0)
{
	$Summary += ''
	$Summary += "$($Result.Warnings) warning(s) to review before submitting; see the annotations."
}
Add-CiSummary ($Summary -join "`n")

Set-CiOutput 'plugin' $Descriptor.Name
Set-CiOutput 'version' $VersionName
