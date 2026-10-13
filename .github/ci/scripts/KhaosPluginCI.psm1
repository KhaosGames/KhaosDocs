# Shared helpers for the plugin CI scripts.
#
# Everything here has to run under Windows PowerShell 5.1, which is what the build machine and the
# `shell: powershell` steps use. That rules out ?:, ??, &&, Join-Path with more than two parts and
# [IO.Path]::GetRelativePath, and it is why files are written through .NET rather than
# Set-Content/Out-File, whose UTF-8 in 5.1 always carries a BOM.

$ErrorActionPreference = 'Stop'

$script:Utf8NoBom = New-Object System.Text.UTF8Encoding $false

#region GitHub Actions plumbing

function Test-GitHubActions
{
	return [bool]$env:GITHUB_ACTIONS
}

function ConvertTo-WorkflowCommandData([string]$Value)
{
	return $Value.Replace('%', '%25').Replace("`r", '%0D').Replace("`n", '%0A')
}

function ConvertTo-WorkflowCommandProperty([string]$Value)
{
	return (ConvertTo-WorkflowCommandData $Value).Replace(':', '%3A').Replace(',', '%2C')
}

<#
.SYNOPSIS
Report a problem. On GitHub it becomes an annotation on the run and, given a file, on the diff.
#>
function Write-CiAnnotation
{
	param(
		[Parameter(Mandatory)][ValidateSet('error', 'warning', 'notice')][string]$Level,
		[Parameter(Mandatory)][string]$Message,
		[string]$File,
		[int]$Line = 0,
		[string]$Title
	)

	if (Test-GitHubActions)
	{
		$Properties = @()
		if ($File) { $Properties += "file=$(ConvertTo-WorkflowCommandProperty $File)" }
		if ($Line -gt 0) { $Properties += "line=$Line" }
		if ($Title) { $Properties += "title=$(ConvertTo-WorkflowCommandProperty $Title)" }

		$Command = "::$Level"
		if ($Properties.Count -gt 0) { $Command += ' ' + ($Properties -join ',') }
		Write-Host "${Command}::$(ConvertTo-WorkflowCommandData $Message)"
		return
	}

	$Colors = @{ error = 'Red'; warning = 'Yellow'; notice = 'Cyan' }
	$Where = ''
	if ($File -and $Line -gt 0) { $Where = " ${File}:${Line}" }
	elseif ($File) { $Where = " $File" }
	Write-Host "$($Level.ToUpperInvariant())$Where - $Message" -ForegroundColor $Colors[$Level]
}

function Add-Utf8Text([string]$Path, [string]$Text)
{
	[IO.File]::AppendAllText($Path, $Text, $script:Utf8NoBom)
}

function Write-Utf8File([string]$Path, [string]$Text)
{
	[IO.File]::WriteAllText($Path, $Text, $script:Utf8NoBom)
}

function Set-CiOutput([string]$Name, [string]$Value)
{
	if ($env:GITHUB_OUTPUT)
	{
		Add-Utf8Text $env:GITHUB_OUTPUT "$Name=$Value`n"
	}
	else
	{
		Write-Host "  output $Name=$Value"
	}
}

function Add-CiSummary([string]$Markdown)
{
	if ($env:GITHUB_STEP_SUMMARY)
	{
		Add-Utf8Text $env:GITHUB_STEP_SUMMARY "$Markdown`n"
	}
}

function Start-CiGroup([string]$Name)
{
	if (Test-GitHubActions) { Write-Host "::group::$Name" } else { Write-Host "== $Name" -ForegroundColor Cyan }
}

function Stop-CiGroup
{
	if (Test-GitHubActions) { Write-Host '::endgroup::' }
}

#endregion

#region Paths and processes

function Join-Paths
{
	$Result = $args[0]
	for ($Index = 1; $Index -lt $args.Count; ++$Index)
	{
		$Result = [IO.Path]::Combine($Result, $args[$Index])
	}
	return $Result
}

function Get-FullPath([string]$Path)
{
	return [IO.Path]::GetFullPath($Path)
}

<# Delete a directory and recreate it empty. #>
function Reset-Directory([string]$Path)
{
	if (Test-Path -LiteralPath $Path)
	{
		Remove-Item -LiteralPath $Path -Recurse -Force
	}
	New-Item -ItemType Directory -Path $Path -Force | Out-Null
}

<#
.SYNOPSIS
Run a native command, streaming its output, and hand each line to a callback.

.DESCRIPTION
Windows PowerShell 5.1 wraps every stderr line of a redirected native command in an ErrorRecord,
and with $ErrorActionPreference = 'Stop' the first one aborts the script. Preference is relaxed
for the duration of the call and the lines are turned back into plain strings.
#>
function Invoke-NativeCommand
{
	param(
		[Parameter(Mandatory)][string]$FilePath,
		[string[]]$ArgumentList = @(),
		[scriptblock]$OnLine
	)

	Write-Host "> `"$FilePath`" $($ArgumentList -join ' ')" -ForegroundColor DarkGray

	try { [Console]::OutputEncoding = [Text.Encoding]::UTF8 } catch { }

	$PreviousPreference = $ErrorActionPreference
	$ErrorActionPreference = 'Continue'
	try
	{
		& $FilePath @ArgumentList 2>&1 | ForEach-Object {
			$Line = "$_"
			Write-Host $Line
			if ($OnLine) { $null = & $OnLine $Line }
		}
		return $LASTEXITCODE
	}
	finally
	{
		$ErrorActionPreference = $PreviousPreference
	}
}

<#
.SYNOPSIS
Run a native command for its output alone, discarding stderr.
.OUTPUTS
ExitCode and Output, the stdout lines.
#>
function Invoke-NativeQuiet
{
	param(
		[Parameter(Mandatory)][string]$FilePath,
		[string[]]$ArgumentList = @()
	)

	# See Invoke-NativeCommand: redirecting stderr under 'Stop' would throw on its first line.
	$PreviousPreference = $ErrorActionPreference
	$ErrorActionPreference = 'Continue'
	try
	{
		$Output = @(& $FilePath @ArgumentList 2>$null | ForEach-Object { "$_" })
		return [pscustomobject]@{ ExitCode = $LASTEXITCODE; Output = $Output }
	}
	finally
	{
		$ErrorActionPreference = $PreviousPreference
	}
}

#endregion

#region Unreal Engine

<#
.SYNOPSIS
Find an installed engine by version, e.g. '5.8'.

.DESCRIPTION
If UE_ROOT_<major>_<minor> is set, that is the engine, and it is an error for it to be anything
else; an explicit setting never quietly falls back to another install. Otherwise looks, in order,
at the Epic Games Launcher's install list, the registry key the launcher writes, and the default
install folder. The launcher list comes before the registry because the registry key is not
always written; a 5.8 launcher install was seen without one.

A missing engine is an error, not a warning: Fab compiles the plugin for every engine version on
the listing, so one that cannot be built here is one that would ship untested.
#>
function Find-UnrealEngine
{
	param([Parameter(Mandatory)][string]$Version)

	if ($Version -notmatch '^\d+\.\d+$')
	{
		throw "Engine version must look like '5.8', got '$Version'."
	}

	$OverrideName = 'UE_ROOT_' + $Version.Replace('.', '_')
	$Override = [Environment]::GetEnvironmentVariable($OverrideName)
	if ($Override)
	{
		$Problem = Test-UnrealEngineRoot $Override $Version
		if ($Problem)
		{
			$Message = "$OverrideName is set to '$Override', but $Problem Fix or unset it."
			Write-CiAnnotation error $Message -Title "Unreal Engine $Version not found"
			throw $Message
		}
		return (Get-FullPath $Override).TrimEnd('\')
	}

	$Candidates = New-Object System.Collections.Generic.List[string]

	$LauncherList = Join-Paths $env:ProgramData 'Epic' 'UnrealEngineLauncher' 'LauncherInstalled.dat'
	if (Test-Path -LiteralPath $LauncherList)
	{
		try
		{
			$Installs = ([IO.File]::ReadAllText($LauncherList) | ConvertFrom-Json).InstallationList
			foreach ($Install in $Installs)
			{
				if ($Install.AppName -eq "UE_$Version") { $Candidates.Add($Install.InstallLocation) }
			}
		}
		catch
		{
			Write-CiAnnotation warning "Could not read ${LauncherList}: $($_.Exception.Message)"
		}
	}

	$Registry = Get-ItemProperty -LiteralPath "HKLM:\SOFTWARE\EpicGames\Unreal Engine\$Version" -Name InstalledDirectory -ErrorAction SilentlyContinue
	if ($Registry) { $Candidates.Add($Registry.InstalledDirectory) }

	if ($env:ProgramFiles) { $Candidates.Add((Join-Paths $env:ProgramFiles 'Epic Games' "UE_$Version")) }

	foreach ($Root in $Candidates)
	{
		if (-not $Root) { continue }
		# A stale launcher entry or registry key can point at a removed or different engine.
		if (Test-UnrealEngineRoot $Root $Version) { continue }
		return (Get-FullPath $Root).TrimEnd('\')
	}

	$Machine = [Environment]::MachineName
	$Message = "Unreal Engine $Version is not installed on $Machine. Install it from the Epic Games Launcher, or set $OverrideName to its root folder. Looked in: $(($Candidates | Select-Object -Unique) -join '; ')"
	Write-CiAnnotation error $Message -Title "Unreal Engine $Version not found"
	throw $Message
}

<#
.SYNOPSIS
Check that a folder is an engine of the given version.
.OUTPUTS
Nothing if it is, otherwise what is wrong, as the end of a sentence.
#>
function Test-UnrealEngineRoot
{
	param(
		[Parameter(Mandatory)][string]$Root,
		[Parameter(Mandatory)][string]$Version
	)

	if (-not (Test-Path -LiteralPath (Join-Paths $Root 'Engine' 'Build' 'BatchFiles' 'RunUAT.bat')))
	{
		return 'there is no engine there (no Engine\Build\BatchFiles\RunUAT.bat).'
	}

	$Found = (Get-UnrealEngineBuild $Root).Version
	if ($Found -ne $Version)
	{
		return "that is Unreal Engine $Found."
	}
	return $null
}

<#
.SYNOPSIS
Read an engine's version from Engine/Build/Build.version.
.OUTPUTS
Version (major.minor), and Description such as '5.8.2, changelist 56702186, ++UE5+Release-5.8'.
#>
function Get-UnrealEngineBuild
{
	param([Parameter(Mandatory)][string]$Root)

	$Path = Join-Paths $Root 'Engine' 'Build' 'Build.version'
	if (-not (Test-Path -LiteralPath $Path))
	{
		return [pscustomobject]@{ Version = 'unknown'; Description = 'unknown version' }
	}

	$Build = [IO.File]::ReadAllText($Path) | ConvertFrom-Json
	return [pscustomobject]@{
		Version = "$($Build.MajorVersion).$($Build.MinorVersion)"
		Description = "$($Build.MajorVersion).$($Build.MinorVersion).$($Build.PatchVersion), changelist $($Build.Changelist), $($Build.BranchName)"
	}
}

#endregion

#region Caches

# Every cache the engine would otherwise keep in a per-user or machine-wide place is moved into one
# folder the caller owns. The build machine also builds other projects, whose caches must not be
# touched, and the defaults are shared:
#
#   UnrealBuildAccelerator  C:\ProgramData\Epic\UnrealBuildAccelerator for every engine on the
#                           machine, size capped (40 GB), so other builds' entries get evicted.
#                           Remote executors and UBA cache servers may be set up for other projects.
#   Zen                     One local server on port 8558 with a version-agnostic data folder in
#                           %LOCALAPPDATA%. An editor that wants a different data folder or version
#                           shuts the running server down, under whatever else was using it.
#   Derived data cache      %LOCALAPPDATA%\UnrealEngine\Common\DerivedDataCache for Launcher
#                           engines, plus any shared or cloud cache set up by environment variable.

<#
.SYNOPSIS
Create the cache folders for one engine version and describe them.
.OUTPUTS
Root, Ddc, Zen and Uba paths, and ZenPort, a port derived from the root so that two cache folders
on one machine do not collide and none uses the default 8558.
#>
function Get-PluginCache
{
	param([Parameter(Mandatory)][string]$CacheDir)

	$Root = (Get-FullPath $CacheDir).TrimEnd('\')
	$Cache = [pscustomobject]@{
		Root = $Root
		Ddc = Join-Paths $Root 'DerivedDataCache'
		Zen = Join-Paths $Root 'Zen'
		Uba = Join-Paths $Root 'UnrealBuildAccelerator'
		ZenPort = 0
	}
	foreach ($Path in @($Cache.Ddc, $Cache.Zen, $Cache.Uba))
	{
		New-Item -ItemType Directory -Path $Path -Force | Out-Null
	}

	$Sha = [Security.Cryptography.SHA256]::Create()
	try { $Hash = $Sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($Root.ToLowerInvariant())) } finally { $Sha.Dispose() }
	$Cache.ZenPort = 8600 + ((([int]$Hash[0]) * 256 + [int]$Hash[1]) % 400)
	return $Cache
}

<#
.SYNOPSIS
Point UnrealBuildTool at the given cache for the rest of this process and its children.

.DESCRIPTION
UnrealBuildTool turns UnrealBuildTool_<Section>__<Field> environment variables into its
highest-priority BuildConfiguration.xml, which is the only way to configure the UnrealBuildTool
that BuildPlugin runs.

Since 5.8 the UBA local executor is UnrealBuildTool's local executor and cannot be turned off, so
its root, where it keeps its store, is moved into the cache folder. Everything that would reach
beyond this machine or into another project's setup is off: remote UBA and Horde, UBA cache
servers, XGE, FASTBuild and SNDBS.
#>
function Set-UnrealBuildToolIsolation
{
	param([Parameter(Mandatory)]$Cache)

	$Settings = [ordered]@{
		'UnrealBuildAccelerator__RootDir' = $Cache.Uba
		'UnrealBuildAccelerator__ForceNoCache' = 'true'
		'UnrealBuildAccelerator__bDisableHorde' = 'true'
		'BuildConfiguration__bAllowUBAExecutor' = 'false'
		'BuildConfiguration__bAllowXGE' = 'false'
		'BuildConfiguration__bAllowFASTBuild' = 'false'
		'BuildConfiguration__bAllowSNDBS' = 'false'
	}
	foreach ($Name in $Settings.Keys)
	{
		[Environment]::SetEnvironmentVariable("UnrealBuildTool_$Name", $Settings[$Name], 'Process')
	}
}

<#
.SYNOPSIS
Editor command line arguments that keep its caches in the given folder.

.DESCRIPTION
The derived data cache is limited to one local file store in the folder. Leaving the Zen, shared
and cloud stores out of the graph means no Zen server is started at all, which matters beyond the
data folder: starting one can shut down another, and updates Zen's per-user install. Zen's data
folder and port are still pointed at the folder in case something starts it regardless.
#>
function Get-EditorCacheArguments
{
	param([Parameter(Mandatory)]$Cache)

	return @(
		'-DDC=(Local)'
		"-LocalDataCachePath=`"$($Cache.Ddc)`""
		"-ZenDataPath=`"$($Cache.Zen)`""
		"-ini:Engine:[Zen.AutoLaunch]:DesiredPort=$($Cache.ZenPort)"
	)
}

#endregion

#region Plugin descriptor

function Get-JsonValue($Object, [string]$Name, $Default = $null)
{
	if ($null -eq $Object) { return $Default }
	$Property = $Object.PSObject.Properties[$Name]
	if ($Property) { return $Property.Value }
	return $Default
}

<#
.SYNOPSIS
Read the single .uplugin at the root of a plugin directory.
#>
function Get-PluginDescriptor
{
	param([Parameter(Mandatory)][string]$PluginDir)

	$PluginDir = (Get-FullPath $PluginDir).TrimEnd('\', '/')
	$Files = @(Get-ChildItem -LiteralPath $PluginDir -Filter '*.uplugin' -File)
	if ($Files.Count -ne 1)
	{
		throw "Expected exactly one .uplugin in '$PluginDir', found $($Files.Count)."
	}

	$Raw = [IO.File]::ReadAllText($Files[0].FullName)
	try
	{
		$Json = $Raw | ConvertFrom-Json
	}
	catch
	{
		throw "$($Files[0].Name) is not valid JSON: $($_.Exception.Message)"
	}

	return [pscustomobject]@{
		Name = $Files[0].BaseName
		FileName = $Files[0].Name
		Path = $Files[0].FullName
		Dir = $PluginDir
		Raw = $Raw
		Json = $Json
	}
}

<#
.SYNOPSIS
Set one top-level scalar field in .uplugin text, keeping the author's formatting.

.DESCRIPTION
Round-tripping through ConvertTo-Json would reflow the whole file and, in 5.1, escape every
apostrophe. The edit is checked by parsing the result, so a pattern that hit a nested key of the
same name is caught rather than shipped.
#>
function Set-DescriptorField
{
	param(
		[Parameter(Mandatory)][string]$Text,
		[Parameter(Mandatory)][string]$Name,
		[Parameter(Mandatory)]$Value
	)

	if ($Value -is [string]) { $Literal = '"' + $Value.Replace('\', '\\').Replace('"', '\"') + '"' }
	else { $Literal = [string]$Value }

	$Pattern = '(?m)^([ \t]*)"' + [regex]::Escape($Name) + '"([ \t]*):([ \t]*)(?:"(?:[^"\\]|\\.)*"|-?\d+(?:\.\d+)?|true|false|null)'
	$Match = [regex]::Match($Text, $Pattern)
	if ($Match.Success)
	{
		$Replacement = $Match.Groups[1].Value + '"' + $Name + '"' + $Match.Groups[2].Value + ':' + $Match.Groups[3].Value + $Literal
		$Result = $Text.Substring(0, $Match.Index) + $Replacement + $Text.Substring($Match.Index + $Match.Length)
	}
	else
	{
		$Anchor = [regex]::Match($Text, '(?m)^([ \t]*)"FileVersion"[ \t]*:[ \t]*\d+[ \t]*,[ \t]*\r?\n')
		if (-not $Anchor.Success)
		{
			throw "Cannot add $Name to the descriptor: no `"FileVersion`": N, line to insert it after."
		}
		$NewLine = "`n"
		if ($Text.Contains("`r`n")) { $NewLine = "`r`n" }
		$Insert = $Anchor.Groups[1].Value + '"' + $Name + '": ' + $Literal + ',' + $NewLine
		$Result = $Text.Insert($Anchor.Index + $Anchor.Length, $Insert)
	}

	$Check = Get-JsonValue ($Result | ConvertFrom-Json) $Name
	if ([string]$Check -ne [string]$Value)
	{
		throw "Setting $Name in the descriptor did not take (read back '$Check'). Is the key nested somewhere unexpected?"
	}
	return $Result
}

<#
.SYNOPSIS
Parse a release version such as 1.2.3 or 1.2.3-rc.1.
.OUTPUTS
VersionName (the full string) and Version, the integer Unreal compares to order releases.
#>
function ConvertFrom-ReleaseVersion
{
	param([Parameter(Mandatory)][string]$Version)

	$Match = [regex]::Match($Version, '^v?(\d+)\.(\d+)\.(\d+)(-[0-9A-Za-z.-]+)?$')
	if (-not $Match.Success)
	{
		throw "Release version '$Version' is not MAJOR.MINOR.PATCH (optionally -prerelease)."
	}

	$Major = [int]$Match.Groups[1].Value
	$Minor = [int]$Match.Groups[2].Value
	$Patch = [int]$Match.Groups[3].Value
	if ($Minor -gt 999 -or $Patch -gt 999)
	{
		throw "Release version '$Version': minor and patch must be below 1000 to fit the integer Version."
	}

	return [pscustomobject]@{
		VersionName = $Version.TrimStart('v')
		Version = $Major * 1000000 + $Minor * 1000 + $Patch
	}
}

#endregion

#region Package contents

# What goes in a Fab source zip: what BuildPlugin packages by default (see FilterPluginFiles in
# BuildPluginCommand.Automation.cs), plus Config, which Fab requires in a code plugin submission.
# Config/FilterPlugin.ini then adds to it, exactly as it does for BuildPlugin.
$script:DefaultRules = @(
	'/Source/...'
	'/Content/...'
	'/Resources/...'
	'/Shaders/...'
	'/Config/...'
	'-/Tests/...'
)

# Never shipped, wherever they appear and whatever FilterPlugin.ini says. Fab declines code plugins
# that contain build output, so this is not left to the filter.
$script:ExcludedFolders = @('Binaries', 'Intermediate', 'Saved', 'DerivedDataCache', '.vs', '.vscode', '.idea', '.git', '.github', '__pycache__')
$script:ExcludedFiles = @('.gitignore', '.gitattributes', '.gitmodules', '.gitkeep', '.DS_Store', 'Thumbs.db', 'desktop.ini')

<#
.SYNOPSIS
Turn an Unreal FileFilter pattern into a regex over '/'-separated relative paths.

.DESCRIPTION
Mirrors EpicGames.Core FileFilter.AddRule: a leading '/' anchors to the plugin root, a pattern with
no '/' at all matches at any depth, a trailing '/' means everything below, '...' spans any number
of folders, and '*' and '?' stay within one path segment.
#>
function ConvertTo-FilterRegex
{
	param([Parameter(Mandatory)][string]$Pattern)

	$Normalized = $Pattern.Trim().Replace('\', '/')
	if ($Normalized.StartsWith('/'))
	{
		$Normalized = $Normalized.Substring(1)
	}
	elseif (-not $Normalized.Contains('/') -and -not $Normalized.StartsWith('...'))
	{
		$Normalized = '.../' + $Normalized
	}
	if ($Normalized.EndsWith('/'))
	{
		$Normalized += '...'
	}

	# 'Foo...' is 'Foo*/...' and '...Foo' is '.../*Foo', as in FileFilter.
	$Normalized = [regex]::Replace($Normalized, '(?<=[^/])\.\.\.', '*/...')
	$Normalized = [regex]::Replace($Normalized, '\.\.\.(?=[^/])', '.../*')

	$Segments = $Normalized.Split('/')
	$Regex = New-Object System.Text.StringBuilder '^'
	for ($Index = 0; $Index -lt $Segments.Count; ++$Index)
	{
		$Segment = $Segments[$Index]
		$IsLast = ($Index -eq $Segments.Count - 1)

		if ($Segment -eq '...')
		{
			if ($IsLast) { [void]$Regex.Append('.+') }
			else { [void]$Regex.Append('(?:[^/]+/)*') }
			continue
		}

		foreach ($Char in $Segment.ToCharArray())
		{
			if ($Char -eq '*') { [void]$Regex.Append('[^/]*') }
			elseif ($Char -eq '?') { [void]$Regex.Append('[^/]') }
			else { [void]$Regex.Append([regex]::Escape([string]$Char)) }
		}
		if (-not $IsLast) { [void]$Regex.Append('/') }
	}
	[void]$Regex.Append('$')
	return $Regex.ToString()
}

function Get-FilterPluginRules
{
	param([Parameter(Mandatory)][string]$PluginDir)

	$Rules = @()
	$Ini = Join-Paths $PluginDir 'Config' 'FilterPlugin.ini'
	if (-not (Test-Path -LiteralPath $Ini)) { return $Rules }

	$InSection = $false
	foreach ($Line in [IO.File]::ReadAllLines($Ini))
	{
		$Trimmed = $Line.Trim()
		if (-not $Trimmed -or $Trimmed.StartsWith(';')) { continue }
		if ($Trimmed.StartsWith('['))
		{
			$InSection = ($Trimmed -eq '[FilterPlugin]')
			continue
		}
		if ($InSection) { $Rules += $Trimmed }
	}
	return $Rules
}

<#
.SYNOPSIS
List the files of a plugin that belong in its Fab source zip, as sorted '/'-separated paths
relative to the plugin root.

.DESCRIPTION
Rules are applied in order and the last one to match a file decides, as in Unreal's FileFilter.
Excluded folders are pruned while walking, so a local Intermediate full of object files costs
nothing.
#>
function Get-PluginPackageFiles
{
	param([Parameter(Mandatory)]$Descriptor)

	$Rules = @()
	foreach ($Rule in ($script:DefaultRules + (Get-FilterPluginRules $Descriptor.Dir)))
	{
		$Exclude = $Rule.StartsWith('-')
		$Pattern = $Rule
		if ($Exclude) { $Pattern = $Rule.Substring(1).Trim() }
		$Rules += [pscustomobject]@{
			Regex = New-Object regex((ConvertTo-FilterRegex $Pattern), 'IgnoreCase, CultureInvariant')
			Exclude = $Exclude
		}
	}

	$Files = New-Object System.Collections.Generic.List[string]
	$Files.Add($Descriptor.FileName)

	$Pending = New-Object System.Collections.Generic.Stack[string]
	$Pending.Push($Descriptor.Dir)
	while ($Pending.Count -gt 0)
	{
		$Directory = $Pending.Pop()
		foreach ($Child in [IO.Directory]::GetDirectories($Directory))
		{
			if ($script:ExcludedFolders -notcontains [IO.Path]::GetFileName($Child)) { $Pending.Push($Child) }
		}
		foreach ($File in [IO.Directory]::GetFiles($Directory))
		{
			$Name = [IO.Path]::GetFileName($File)
			if ($script:ExcludedFiles -contains $Name) { continue }

			$Relative = $File.Substring($Descriptor.Dir.Length + 1).Replace('\', '/')
			if ($Relative -eq $Descriptor.FileName) { continue }

			$Included = $false
			foreach ($Rule in $Rules)
			{
				if ($Rule.Regex.IsMatch($Relative)) { $Included = -not $Rule.Exclude }
			}
			if ($Included) { $Files.Add($Relative) }
		}
	}

	$Sorted = $Files.ToArray()
	[Array]::Sort($Sorted, [StringComparer]::Ordinal)
	return ,$Sorted
}

<#
.SYNOPSIS
Write a zip with a stable layout: entries sorted, '/' separators, one root folder, fixed timestamps.

.DESCRIPTION
Compress-Archive in 5.1 writes '\' separators, which macOS and Linux unzip treat as part of the
file name. Fixed timestamps mean the same sources give the same bytes, so a checksum identifies a
build.
#>
function New-PluginZip
{
	param(
		[Parameter(Mandatory)][string]$SourceDir,
		[Parameter(Mandatory)][string[]]$Files,
		[Parameter(Mandatory)][string]$RootFolder,
		[Parameter(Mandatory)][string]$ZipPath
	)

	Add-Type -AssemblyName System.IO.Compression
	Add-Type -AssemblyName System.IO.Compression.FileSystem

	if (Test-Path -LiteralPath $ZipPath) { Remove-Item -LiteralPath $ZipPath -Force }

	$Timestamp = New-Object DateTimeOffset (New-Object DateTime 2020, 1, 1, 0, 0, 0), ([TimeSpan]::Zero)
	$Stream = [IO.File]::Open($ZipPath, [IO.FileMode]::CreateNew)
	try
	{
		$Zip = New-Object System.IO.Compression.ZipArchive $Stream, ([System.IO.Compression.ZipArchiveMode]::Create)
		try
		{
			foreach ($Relative in $Files)
			{
				$Entry = $Zip.CreateEntry("$RootFolder/$Relative", [System.IO.Compression.CompressionLevel]::Optimal)
				$Entry.LastWriteTime = $Timestamp
				$Source = [IO.File]::OpenRead((Join-Paths $SourceDir $Relative))
				try
				{
					$Target = $Entry.Open()
					try { $Source.CopyTo($Target) } finally { $Target.Dispose() }
				}
				finally
				{
					$Source.Dispose()
				}
			}
		}
		finally
		{
			$Zip.Dispose()
		}
	}
	finally
	{
		$Stream.Dispose()
	}
}

function Expand-PluginZip
{
	param(
		[Parameter(Mandatory)][string]$ZipPath,
		[Parameter(Mandatory)][string]$Destination
	)

	Add-Type -AssemblyName System.IO.Compression.FileSystem
	Reset-Directory $Destination
	# .NET Framework 4.6.2+ refuses entries that would land outside the destination.
	[System.IO.Compression.ZipFile]::ExtractToDirectory($ZipPath, $Destination)
}

function Get-FileSha256([string]$Path)
{
	return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

#endregion

#region Build output

<#
.SYNOPSIS
Make a callback that turns compiler output lines into annotations on the plugin's own files.

.DESCRIPTION
BuildPlugin compiles a copy of the plugin under HostProject/Plugins/<Name>/, so everything after
that in a reported path is the path in the plugin's repository. Diagnostics in engine files are
skipped, except that an error raised inside an engine header, typically a template instantiated
with one of the plugin's types, is reported at the first note that points back into the plugin.
#>
function New-CompileDiagnosticReporter
{
	param(
		[Parameter(Mandatory)][string]$PluginName,
		[string]$Label = 'Compile'
	)

	$PluginPath = [regex]('(?i)[\\/]Plugins[\\/]' + [regex]::Escape($PluginName) + '[\\/](?<path>.+)$')
	$Pattern = [regex]'^\s*(?<file>[A-Za-z]:[\\/][^(]+?)\((?<line>\d+)(?:,\d+)?\)\s*:\s*(?<level>fatal error|error|warning|note)(?:\s+(?<code>[A-Z]+\d+))?\s*:\s*(?<message>.+)$'
	$State = @{ Pending = $null; Reported = @{} }

	return {
		param([string]$Line)
		$Match = $Pattern.Match($Line)
		if (-not $Match.Success) { return }

		$Level = $Match.Groups['level'].Value
		$PathMatch = $PluginPath.Match($Match.Groups['file'].Value)
		if ($Level -eq 'note')
		{
			if (-not $State.Pending -or -not $PathMatch.Success) { return }
			$Diagnostic = $State.Pending
			$Message = "$($Diagnostic.Groups['message'].Value.Trim()) (raised in $([IO.Path]::GetFileName($Diagnostic.Groups['file'].Value)), from here)"
		}
		elseif (-not $PathMatch.Success)
		{
			if ($Level -ne 'warning') { $State.Pending = $Match }
			return
		}
		else
		{
			$Diagnostic = $Match
			$Message = $Match.Groups['message'].Value.Trim()
		}
		$State.Pending = $null

		$File = $PathMatch.Groups['path'].Value.Replace('\', '/')
		$LineNumber = [int]$Match.Groups['line'].Value
		$Key = "$File|$LineNumber|$Message"
		if ($State.Reported.ContainsKey($Key)) { return }
		$State.Reported[$Key] = $true

		$DiagnosticLevel = $Diagnostic.Groups['level'].Value
		$Severity = 'error'
		if ($DiagnosticLevel -eq 'warning') { $Severity = 'warning' }
		$Title = "$Label $DiagnosticLevel"
		if ($Diagnostic.Groups['code'].Success) { $Title += " $($Diagnostic.Groups['code'].Value)" }
		Write-CiAnnotation $Severity $Message -File $File -Line $LineNumber -Title $Title
	}.GetNewClosure()
}

#endregion

#region Validation

<#
.SYNOPSIS
Check a plugin against Fab's code plugin requirements and the problems that get submissions
declined.

.DESCRIPTION
Errors fail the build. Warnings are things a human should look at before submitting. Each rule
names the requirement so a failure explains itself.

.OUTPUTS
A result with Errors and Warnings counts.
#>
function Test-PluginPackage
{
	param(
		[Parameter(Mandatory)]$Descriptor,
		[Parameter(Mandatory)][string[]]$Files,
		[string]$CopyrightHolder
	)

	$State = @{ Errors = 0; Warnings = 0 }
	$Fail = {
		param([string]$Message, [string]$File, [int]$Line = 0)
		$State.Errors++
		Write-CiAnnotation error $Message -File $File -Line $Line -Title 'Fab requirement'
	}
	$Warn = {
		param([string]$Message, [string]$File, [int]$Line = 0)
		$State.Warnings++
		Write-CiAnnotation warning $Message -File $File -Line $Line -Title 'Fab check'
	}

	$Json = $Descriptor.Json
	$DescriptorFile = $Descriptor.FileName

	# Descriptor ------------------------------------------------------------------------------

	if ((Get-JsonValue $Json 'FileVersion') -ne 3)
	{
		& $Fail '"FileVersion" must be 3.' $DescriptorFile
	}
	if (-not (Get-JsonValue $Json 'FriendlyName'))
	{
		& $Fail '"FriendlyName" is empty.' $DescriptorFile
	}
	if (-not (Get-JsonValue $Json 'VersionName'))
	{
		& $Fail '"VersionName" is empty.' $DescriptorFile
	}
	if ((Get-JsonValue $Json 'Installed') -eq $true)
	{
		& $Warn '"Installed" is true. Source descriptors normally leave it false; BuildPlugin sets it on the packaged copy.' $DescriptorFile
	}
	if (Get-JsonValue $Json 'MarketplaceURL')
	{
		& $Warn '"MarketplaceURL" is the old Marketplace field. Fab uses "FabURL".' $DescriptorFile
	}
	if (-not (Get-JsonValue $Json 'FabURL'))
	{
		& $Warn '"FabURL" is not set. Expected until the listing is first approved, since only the live listing has its final URL; add it in the next update.' $DescriptorFile
	}

	$Modules = @(Get-JsonValue $Json 'Modules' @())
	if ($Modules.Count -eq 0)
	{
		& $Fail 'A code plugin needs at least one module in "Modules".' $DescriptorFile
	}
	foreach ($Module in $Modules)
	{
		$ModuleName = Get-JsonValue $Module 'Name'
		$Allow = @(Get-JsonValue $Module 'PlatformAllowList' @())
		$Deny = @(Get-JsonValue $Module 'PlatformDenyList' @())
		if ($Allow.Count -eq 0 -and $Deny.Count -eq 0)
		{
			& $Fail "Module '$ModuleName' needs a PlatformAllowList or PlatformDenyList. Fab requires one on every module." $DescriptorFile
		}

		$BuildFile = $Files | Where-Object { $_.StartsWith('Source/') -and $_.EndsWith("/$ModuleName.Build.cs") } | Select-Object -First 1
		if (-not $BuildFile)
		{
			& $Fail "Module '$ModuleName' has no $ModuleName.Build.cs under Source/." $DescriptorFile
		}
	}

	# Layout ----------------------------------------------------------------------------------

	foreach ($Folder in @('Source', 'Content', 'Config'))
	{
		if (-not ($Files | Where-Object { $_.StartsWith("$Folder/") } | Select-Object -First 1))
		{
			$Hint = ''
			if ($Folder -eq 'Config') { $Hint = ' Add Config/FilterPlugin.ini; it also controls which extra files ship.' }
			& $Fail "Fab requires a $Folder/ folder in a code plugin, and this package has none.$Hint"
		}
	}

	$HasAssets = $Files | Where-Object { $_ -match '^Content/.*\.(uasset|umap)$' } | Select-Object -First 1
	if ($HasAssets -and (Get-JsonValue $Json 'CanContainContent') -ne $true)
	{
		& $Fail 'Content/ has assets but "CanContainContent" is not true, so the engine will not mount them.' $DescriptorFile
	}

	if (-not ($Files -contains 'Resources/Icon128.png'))
	{
		& $Warn 'Resources/Icon128.png is missing, so the plugin shows a blank icon in the Plugins window.'
	}

	# Files -----------------------------------------------------------------------------------

	foreach ($File in $Files)
	{
		$Name = [IO.Path]::GetFileName($File)
		$Extension = [IO.Path]::GetExtension($File).ToLowerInvariant()
		$IsThirdParty = $File -match '(^|/)ThirdParty/'

		if (@('.exe', '.msi', '.dmg', '.pkg', '.zip', '.rar', '.7z', '.tar', '.gz') -contains $Extension)
		{
			& $Fail "Executables, installers and nested archives are not accepted in a Fab code plugin." $File
		}
		elseif (@('.bat', '.cmd', '.ps1', '.sh', '.command') -contains $Extension)
		{
			& $Warn "Scripts get extra scrutiny in review. Make sure this one is needed by buyers." $File
		}

		if (-not $IsThirdParty -and $Name -match '^(LICEN[CS]E|EULA|COPYING)(\..*)?$')
		{
			& $Fail "License and EULA files are not allowed outside ThirdParty folders; the license is the one chosen on the Fab listing." $File
		}

		if ($File.Length -gt 170)
		{
			& $Warn "Path is $($File.Length) characters. Installed under Engine/Plugins/Marketplace this risks the Windows 260 character limit." $File
		}
	}

	# Copyright notices -----------------------------------------------------------------------

	if (-not $CopyrightHolder) { $CopyrightHolder = Get-JsonValue $Json 'CreatedBy' }

	foreach ($File in $Files)
	{
		if ($File -notmatch '^Source/.*\.(h|hpp|inl|c|cpp|cs)$') { continue }
		if ($File -match '(^|/)ThirdParty/') { continue }

		$FirstLine = ''
		$Reader = New-Object IO.StreamReader (Join-Paths $Descriptor.Dir $File), $true
		try { $FirstLine = $Reader.ReadLine() } finally { $Reader.Dispose() }
		if ($null -eq $FirstLine) { $FirstLine = '' }

		if ($FirstLine -notmatch '^\s*//.*\bCopyright\b')
		{
			& $Fail "The first line must be a copyright notice, e.g. '// Copyright $CopyrightHolder. All Rights Reserved.'" $File 1
		}
		elseif ($CopyrightHolder -and $FirstLine.IndexOf($CopyrightHolder, [StringComparison]::OrdinalIgnoreCase) -lt 0)
		{
			& $Fail "The copyright notice should name '$CopyrightHolder' (the descriptor's CreatedBy). Epic's own notice left in from a template is a common reason to be declined." $File 1
		}
	}

	return [pscustomobject]$State
}

#endregion

#region Releases

<#
.SYNOPSIS
Release notes from the conventional commit subjects since the previous vX.Y.Z tag.

.DESCRIPTION
feat and fix commits get their own sections. ci, chore, test, build and style commits are left
out, since they change nothing for someone using the plugin. Anything else, including subjects
not in conventional form, is listed under Other changes.
#>
function Get-ReleaseNotes
{
	param(
		[Parameter(Mandatory)][string]$RepoDir,
		[Parameter(Mandatory)][string]$Tag
	)

	function Invoke-Git([string[]]$Arguments)
	{
		$Result = Invoke-NativeQuiet git (@('-C', $RepoDir) + $Arguments)
		if ($Result.ExitCode -ne 0) { return $null }
		return $Result.Output
	}

	$Previous = Invoke-Git @('describe', '--tags', '--abbrev=0', '--match', 'v[0-9]*', "$Tag^")
	$Range = $Tag
	if ($Previous) { $Range = "$Previous..$Tag" }

	$Sections = [ordered]@{ 'Features' = @(); 'Fixes' = @(); 'Other changes' = @() }
	foreach ($Line in @(Invoke-Git @('log', '--no-merges', '--format=%h %s', $Range)))
	{
		if (-not $Line) { continue }
		$Hash, $Subject = $Line.Split(' ', 2)
		$Match = [regex]::Match($Subject, '^(?<type>\w+)(\([^)]*\))?!?:\s*(?<text>.+)$')
		if (-not $Match.Success)
		{
			$Sections['Other changes'] += "- $Subject ($Hash)"
			continue
		}

		$Text = $Match.Groups['text'].Value
		switch ($Match.Groups['type'].Value.ToLowerInvariant())
		{
			'feat' { $Sections['Features'] += "- $Text ($Hash)" }
			'fix' { $Sections['Fixes'] += "- $Text ($Hash)" }
			# Housekeeping that changes nothing for someone using the plugin.
			{ @('ci', 'chore', 'test', 'build', 'style') -contains $_ } { }
			default { $Sections['Other changes'] += "- $Text ($Hash)" }
		}
	}

	$Notes = @()
	foreach ($Section in $Sections.Keys)
	{
		if ($Sections[$Section].Count -eq 0) { continue }
		$Notes += "### $Section"
		$Notes += $Sections[$Section]
		$Notes += ''
	}
	if ($Notes.Count -eq 0) { $Notes = @('Maintenance release.', '') }
	return ($Notes -join "`n").Trim()
}

#endregion

Export-ModuleMember -Function *
