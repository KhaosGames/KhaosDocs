<#
.SYNOPSIS
Report what is using the disk on a build machine. Reads only; deletes nothing.

.DESCRIPTION
Walks every fixed drive once, then prints:

  - free and used space per drive;
  - the largest folders as a tree, down to whatever depth they are big at;
  - the usual places space goes on an Unreal build machine (engine and editor caches for every
    account, runner workspaces, crash dumps, update and installer caches), each with a note on
    whether it is safe to clear.

Run it from an elevated Windows PowerShell so other accounts' folders, such as the runners'
service profiles, can be read. Writes the same report to disk-report.txt in the current folder.

.EXAMPLE
.\Get-DiskReport.ps1
#>
[CmdletBinding()]
param(
	# Smallest folder shown in the tree. Raised automatically if the tree gets too long.
	[double]$MinimumGb = 1,
	[int]$MaximumLines = 90,
	[string]$OutFile = 'disk-report.txt'
)

$ErrorActionPreference = 'Stop'

# A compiled walker: a PowerShell loop over a few million files takes far too long. The \\?\ prefix
# lets it into paths longer than 260 characters, which Unreal intermediates are full of. Reparse
# points are not followed, so junctions are not counted twice.
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.IO;

public static class KhaosDiskWalker
{
	public static Dictionary<string, long> Sizes = new Dictionary<string, long>(StringComparer.OrdinalIgnoreCase);
	public static long Files;
	public static long Skipped;

	public static long Walk(string root)
	{
		return WalkDirectory(new DirectoryInfo(@"\\?\" + root.TrimEnd('\\') + @"\"));
	}

	static long WalkDirectory(DirectoryInfo directory)
	{
		long total = 0;
		try
		{
			foreach (FileInfo file in directory.EnumerateFiles())
			{
				try { total += file.Length; Files++; } catch { Skipped++; }
			}
			foreach (DirectoryInfo child in directory.EnumerateDirectories())
			{
				if ((child.Attributes & FileAttributes.ReparsePoint) != 0) { continue; }
				total += WalkDirectory(child);
			}
		}
		catch
		{
			Skipped++;
		}
		Sizes[directory.FullName.Substring(4).TrimEnd('\\')] = total;
		return total;
	}
}
'@

$Lines = New-Object System.Collections.Generic.List[string]
function Out-Line([string]$Text = '')
{
	$Lines.Add($Text)
	Write-Host $Text
}

function Format-Gb([double]$Bytes)
{
	return ('{0,7:N1} GB' -f ($Bytes / 1GB))
}

function Get-Size([string]$Path)
{
	$Key = $Path.TrimEnd('\')
	if ([KhaosDiskWalker]::Sizes.ContainsKey($Key)) { return [KhaosDiskWalker]::Sizes[$Key] }
	try
	{
		if (Test-Path -LiteralPath $Path -PathType Leaf) { return (Get-Item -LiteralPath $Path -Force).Length }
	}
	catch { }
	return $null
}

$Started = Get-Date
Out-Line "Disk report for $([Environment]::MachineName), $($Started.ToString('yyyy-MM-dd HH:mm'))"

$Drives = @(Get-CimInstance Win32_LogicalDisk -Filter 'DriveType = 3')
Out-Line ''
Out-Line 'Drives'
foreach ($Drive in $Drives)
{
	Out-Line ("  {0} {1} free of {2}" -f $Drive.DeviceID, (Format-Gb $Drive.FreeSpace), (Format-Gb $Drive.Size))
}

foreach ($Drive in $Drives)
{
	Write-Host "Walking $($Drive.DeviceID)\ ..." -ForegroundColor DarkGray
	[void][KhaosDiskWalker]::Walk("$($Drive.DeviceID)\")
}
Write-Host ("Walked {0:N0} files in {1:N0} seconds ({2:N0} folders unreadable)." -f [KhaosDiskWalker]::Files, ((Get-Date) - $Started).TotalSeconds, [KhaosDiskWalker]::Skipped) -ForegroundColor DarkGray

# Largest folders --------------------------------------------------------------------------------

$Threshold = $MinimumGb * 1GB
do
{
	$Big = @([KhaosDiskWalker]::Sizes.GetEnumerator() | Where-Object { $_.Value -ge $Threshold -and $_.Key.Length -gt 3 })
	if ($Big.Count -gt $MaximumLines) { $Threshold *= 2 }
}
while ($Big.Count -gt $MaximumLines)

Out-Line ''
Out-Line ("Folders of {0:N0} GB and more" -f ($Threshold / 1GB))
foreach ($Entry in ($Big | Sort-Object Key))
{
	$Depth = ($Entry.Key.TrimEnd('\').Split('\').Count - 2)
	Out-Line ("  {0}  {1}{2}" -f (Format-Gb $Entry.Value), ('  ' * $Depth), $Entry.Key)
}

# Known places -----------------------------------------------------------------------------------

# Every account's profile, including the service accounts runners usually run as.
$UserDirs = @(Get-ChildItem 'C:\Users' -Directory -Force -ErrorAction SilentlyContinue | Where-Object { @('All Users', 'Default User') -notcontains $_.Name } | ForEach-Object { $_.FullName })
$UserDirs += @('C:\Windows\ServiceProfiles\NetworkService', 'C:\Windows\ServiceProfiles\LocalService', 'C:\Windows\System32\config\systemprofile')

$Known = New-Object System.Collections.Generic.List[object]
function Add-Known([string]$Path, [string]$Note)
{
	$Size = Get-Size $Path
	if ($null -ne $Size -and $Size -ge 100MB) { $Known.Add([pscustomobject]@{ Path = $Path; Size = $Size; Note = $Note }) }
}

Add-Known 'C:\$Recycle.Bin' 'Recycle Bin. Safe to empty.'
Add-Known 'C:\Windows\Temp' 'Windows temp. Safe to clear what is not in use.'
Add-Known 'C:\Windows\SoftwareDistribution\Download' 'Windows Update downloads. Disk Cleanup > Clean up system files clears it.'
Add-Known 'C:\Windows\MEMORY.DMP' 'Kernel crash dump. Safe to delete.'
Add-Known 'C:\Windows\Minidump' 'Kernel minidumps. Safe to delete.'
Add-Known 'C:\ProgramData\Microsoft\Windows\WER' 'Windows Error Reporting dumps. Safe to delete.'
Add-Known 'C:\hiberfil.sys' 'Hibernation file. powercfg /h off removes it, if this machine never hibernates.'
Add-Known 'C:\pagefile.sys' 'Page file. Leave it; Unreal builds need it.'
Add-Known 'C:\ProgramData\Epic\UnrealBuildAccelerator' 'UBA store shared by every engine on this machine. Rebuilt on demand; slower next build.'
Add-Known 'C:\ProgramData\Epic\EpicGamesLauncher\VaultCache' 'Fab/Marketplace download cache. Safe to delete.'
Add-Known 'C:\ProgramData\Microsoft\VisualStudio\Packages' 'Visual Studio download cache. Safe to delete if the installer has "Keep download cache" off.'
Add-Known 'C:\ProgramData\Package Cache' 'Installer cache for repair and uninstall. Do not delete by hand.'
Add-Known 'C:\steamcmd' 'SteamCMD, used by the WhatSub deploy. Leave it.'

foreach ($UserDir in $UserDirs)
{
	Add-Known "$UserDir\AppData\Local\Temp" 'Temp for this account. Safe to clear what is not in use.'
	Add-Known "$UserDir\AppData\Local\CrashDumps" 'Application crash dumps. Safe to delete.'
	Add-Known "$UserDir\AppData\Local\UnrealEngine\Common\DerivedDataCache" 'Derived data cache for Launcher engines. Rebuilt on demand.'
	Add-Known "$UserDir\AppData\Local\UnrealEngine\Common\Zen" 'Zen cache server data. Rebuilt on demand; close editors and cooks first.'
	Add-Known "$UserDir\AppData\Local\UnrealEngine\Intermediate" 'Engine intermediates for Launcher engines. Rebuilt on demand.'
	Add-Known "$UserDir\AppData\Local\UnrealBuildTool" 'UnrealBuildTool logs. Safe to delete.'
	Add-Known "$UserDir\AppData\Local\EpicGamesLauncher\Saved" 'Epic Games Launcher logs and web cache. Safe to delete.'
	Add-Known "$UserDir\.nuget\packages" 'NuGet package cache. Re-downloaded on demand.'
	foreach ($Saved in @(Get-ChildItem "$UserDir\AppData\Local\UnrealEngine" -Directory -Filter '5.*' -ErrorAction SilentlyContinue))
	{
		Add-Known "$($Saved.FullName)\Saved" "Editor logs, crashes and autosaves for engine $($Saved.Name). Logs and crashes are safe to delete."
	}
}

# Installed engines, and what in each runner's workspace is big.
$LauncherList = 'C:\ProgramData\Epic\UnrealEngineLauncher\LauncherInstalled.dat'
if (Test-Path -LiteralPath $LauncherList)
{
	foreach ($Install in ([IO.File]::ReadAllText($LauncherList) | ConvertFrom-Json).InstallationList)
	{
		if ($Install.AppName -like 'UE_*') { Add-Known $Install.InstallLocation "Launcher engine $($Install.AppName). Uninstall from the Launcher if no project or runner needs it." }
	}
}

foreach ($Service in @(Get-CimInstance Win32_Service -Filter "Name LIKE 'actions.runner.%'"))
{
	$Root = Split-Path (Split-Path ($Service.PathName -replace '^"([^"]+)".*$', '$1') -Parent) -Parent
	Add-Known "$Root\_diag" "Logs of runner $($Service.Name). Old ones are safe to delete."
	# The work folder is set when the runner is configured: _work unless chosen otherwise.
	$Work = Join-Path $Root '_work'
	$Settings = Join-Path $Root '.runner'
	if (Test-Path -LiteralPath $Settings)
	{
		$WorkFolder = ([IO.File]::ReadAllText($Settings) | ConvertFrom-Json).workFolder
		if ($WorkFolder -and [IO.Path]::IsPathRooted($WorkFolder)) { $Work = $WorkFolder }
		elseif ($WorkFolder) { $Work = Join-Path $Root $WorkFolder }
	}
	Add-Known "$Work\_temp" 'Runner temp, emptied after each job. Anything left is from a job that was killed.'
	foreach ($Child in @(Get-ChildItem $Work -Directory -ErrorAction SilentlyContinue | Where-Object { $_.Name -notlike '_*' }))
	{
		# _work\<repo>\<repo>\<checkout>\...: the levels that hold engines, projects and their caches.
		foreach ($Folder in @(Get-ChildItem $Child.FullName -Directory -Recurse -Depth 3 -ErrorAction SilentlyContinue))
		{
			if (@('Intermediate', 'Saved', 'DerivedDataCache', 'Binaries', 'StagedBuilds', 'Cooked', '.ue-cache', '.git') -contains $Folder.Name)
			{
				Add-Known $Folder.FullName "Workspace of $($Child.Name), runner $($Service.Name). The project's own build output and caches; clearing makes its next build slower or full."
			}
		}
	}
}

Out-Line ''
Out-Line 'Usual places (100 MB and more)'
foreach ($Item in ($Known | Sort-Object Size -Descending))
{
	Out-Line ("  {0}  {1}" -f (Format-Gb $Item.Size), $Item.Path)
	Out-Line ("              {0}" -f $Item.Note)
}

Out-Line ''
Out-Line 'Not measured here: the Windows component store (C:\Windows\WinSxS), whose size on disk is overstated by hard links. Dism /Online /Cleanup-Image /AnalyzeComponentStore reports what can be reclaimed.'

# With a BOM, so Notepad and Windows PowerShell read non-ASCII folder names correctly.
[IO.File]::WriteAllLines((Join-Path (Get-Location) $OutFile), $Lines, [Text.Encoding]::UTF8)
Write-Host ''
Write-Host "Saved to $(Join-Path (Get-Location) $OutFile)" -ForegroundColor Cyan
