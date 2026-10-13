<#
.SYNOPSIS
Install a GitHub Actions runner for plugin builds on this machine, isolated from everything else
built here.

.DESCRIPTION
Run once per runner, from an elevated Windows PowerShell on the build machine. It:

  1. Checks the machine has what plugin builds need: the engine versions, Visual Studio's C++
     tools, Git, and an execution policy that lets the workflow's scripts run.
  2. Creates a local standard (not administrator) Windows account for the runner, with a random
     password that only the runner service keeps. Each runner gets its own account, so a job on one
     cannot read another's workspace, caches or Windows profile.
  3. Denies that account all access to the other runners' folders on this machine (their
     workspaces, logs and credentials) and to other sensitive folders, C:\steamcmd by default, and
     makes the new runner's own folder private to its account.
  4. Downloads the current GitHub Actions runner, checks its SHA-256 against the one GitHub
     publishes, and unpacks it into a short folder, C:\rp\<name>, since Unreal build paths nest
     deep. It refuses a folder inside another runner's.
  5. Registers it with the ue-plugins label and installs it as a service running as that account.

A runner for a public repository runs code from pull requests. Pair it with the organization
setting that requires approval before workflows run for all outside contributors.

Use -WhatIf to see what it would do without changing anything.

.PARAMETER Target
owner/repo for a runner only that repository can use, which is what a public repository needs. An
organization name for one that the organization's private repositories share.

.PARAMETER RegistrationToken
The token from Settings > Actions > Runners > New self-hosted runner (the value after --token).
If omitted it is fetched with the GitHub CLI when that is signed in, or asked for. Never printed.

.PARAMETER ProtectPaths
Folders the runner account must not read, on top of the other runners' folders, which are always
protected. The script lists the top-level folders the account could otherwise read.

.EXAMPLE
.\Install-BuildRunner.ps1 -Target KhaosGames/KhaosDocs

.EXAMPLE
.\Install-BuildRunner.ps1 -Target KhaosGames/KhaosDocs -ProtectPaths C:\steamcmd, C:\Dev -WhatIf
#>
[CmdletBinding(SupportsShouldProcess)]
param(
	[Parameter(Mandatory)][string]$Target,
	[string]$RegistrationToken,
	[string[]]$EngineVersions = @('5.8'),
	[string[]]$ProtectPaths = @('C:\steamcmd'),
	[string[]]$Labels = @('ue-plugins'),
	# Short, since Unreal build paths nest deep; "runners, plugins". Must not be inside another
	# runner's folder.
	[string]$RunnerRoot = 'C:\rp',
	[string]$Account
)

$ErrorActionPreference = 'Stop'
# Invoke-WebRequest in Windows PowerShell crawls while it draws a progress bar.
$ProgressPreference = 'SilentlyContinue'
Import-Module (Join-Path $PSScriptRoot 'KhaosPluginCI.psm1') -Force
# Imported up front with -WhatIf suspended, or it reports every alias the module defines.
$RequestedWhatIf = $WhatIfPreference
$WhatIfPreference = $false
Import-Module CimCmdlets
$WhatIfPreference = $RequestedWhatIf

function Write-Step([string]$Text)
{
	Write-Host ''
	Write-Host "== $Text" -ForegroundColor Cyan
}

function Write-Problem([string]$Text)
{
	$script:Problems += $Text
	Write-Host "  ! $Text" -ForegroundColor Yellow
}

<#
Whether a local account is directly in a local group, given by SID so it works in any language.
Read through ADSI: Get-LocalGroupMember throws on groups holding members it cannot resolve.
#>
function Test-LocalGroupMember([string]$GroupSid, [string]$UserName)
{
	$Group = Get-LocalGroup -SID $GroupSid
	$Adsi = [ADSI]"WinNT://$env:COMPUTERNAME/$($Group.Name),group"
	foreach ($Member in @($Adsi.Invoke('Members')))
	{
		$Name = $Member.GetType().InvokeMember('Name', 'GetProperty', $null, $Member, $null)
		if ($Name -eq $UserName) { return $true }
	}
	return $false
}

<#
Whether any local account can list a folder: an allow for Users, Authenticated Users or Everyone
with read access. The runner account gets those, so this is what it could read.
#>
function Test-ReadableByAnyUser([string]$Path)
{
	$Broad = @('S-1-5-32-545', 'S-1-5-11', 'S-1-1-0')
	try { $Acl = Get-Acl -LiteralPath $Path } catch { return $false }
	foreach ($Rule in $Acl.Access)
	{
		if ($Rule.AccessControlType -ne 'Allow') { continue }
		if (([int]$Rule.FileSystemRights -band [int][Security.AccessControl.FileSystemRights]::ListDirectory) -eq 0) { continue }
		try { $Sid = $Rule.IdentityReference.Translate([Security.Principal.SecurityIdentifier]).Value } catch { continue }
		if ($Broad -contains $Sid) { return $true }
	}
	return $false
}

$script:Problems = @()
$IsRepository = $Target.Contains('/')
$ShortName = ($Target.Split('/') | Select-Object -Last 1).ToLowerInvariant()
if (-not $Account)
{
	# Windows account names stop at 20 characters.
	$Account = ('gh-' + $ShortName)
	if ($Account.Length -gt 20) { $Account = $Account.Substring(0, 20) }
}
$QualifiedAccount = "$env:COMPUTERNAME\$Account"
$RunnerName = "$env:COMPUTERNAME-$ShortName"
$RunnerDir = Join-Paths $RunnerRoot $ShortName
$Url = "https://github.com/$Target"
if ($IsRepository)
{
	$TokenEndpoint = "repos/$Target/actions/runners/registration-token"
	$TokenPage = "$Url/settings/actions/runners/new"
}
else
{
	$TokenEndpoint = "orgs/$Target/actions/runners/registration-token"
	$TokenPage = "https://github.com/organizations/$Target/settings/actions/runners/new"
}

$Principal = New-Object Security.Principal.WindowsPrincipal ([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $Principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator) -and -not $WhatIfPreference)
{
	throw 'Run this from an elevated PowerShell (Run as administrator).'
}

if (Test-Path -LiteralPath (Join-Paths $RunnerDir '.runner'))
{
	throw "A runner is already configured in $RunnerDir. Remove it first: in an elevated prompt run '$RunnerDir\config.cmd remove', delete the folder, and remove the runner from $Url/settings/actions/runners if it is still listed."
}

Write-Host "Runner $RunnerName for $Target, as $QualifiedAccount, in $RunnerDir"

# 1. What plugin builds need ---------------------------------------------------------------------

Write-Step 'Checking the machine'

foreach ($Version in $EngineVersions)
{
	try
	{
		$Root = Find-UnrealEngine $Version
		Write-Host "  Unreal Engine $((Get-UnrealEngineBuild $Root).Description) at $Root"
	}
	catch
	{
		Write-Problem "Unreal Engine $Version is not installed. Install it from the Epic Games Launcher; until then its builds fail at their first step."
	}
}

$VsWhere = Join-Paths ${env:ProgramFiles(x86)} 'Microsoft Visual Studio' 'Installer' 'vswhere.exe'
$VsPath = $null
if (Test-Path -LiteralPath $VsWhere)
{
	$VsPath = (Invoke-NativeQuiet $VsWhere @('-latest', '-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath')).Output | Select-Object -First 1
}
if ($VsPath) { Write-Host "  Visual Studio C++ tools at $VsPath" }
else { Write-Problem 'No Visual Studio with the C++ x64 tools found; BuildPlugin cannot compile without them.' }

$MachinePath = [Environment]::GetEnvironmentVariable('Path', 'Machine').Split(';') | Where-Object { $_ }
$Git = $MachinePath | Where-Object { Test-Path -LiteralPath (Join-Paths $_ 'git.exe') } | Select-Object -First 1
if ($Git) { Write-Host "  Git in the machine PATH ($Git)" }
else { Write-Problem "git.exe is not on the machine-wide PATH, so the runner's account will not find it. Install Git for Windows for all users." }

# The runner's account has no user-level policy of its own, so the machine's decides.
$Policy = [string](Get-ExecutionPolicy -Scope MachinePolicy)
if ($Policy -eq 'Undefined') { $Policy = [string](Get-ExecutionPolicy -Scope LocalMachine) }
if (@('RemoteSigned', 'Unrestricted', 'Bypass') -contains $Policy) { Write-Host "  Execution policy $Policy" }
else { Write-Problem "Execution policy for new accounts is '$Policy', which blocks the workflow's scripts. Set it with: Set-ExecutionPolicy RemoteSigned -Scope LocalMachine" }

# 2. What the account must not read --------------------------------------------------------------

Write-Step 'Finding folders to protect'

$Protect = New-Object System.Collections.Generic.List[string]
foreach ($Service in @(Get-CimInstance Win32_Service -Filter "Name LIKE 'actions.runner.%'"))
{
	$Exe = $Service.PathName -replace '^"([^"]+)".*$', '$1'
	$Root = Split-Path (Split-Path $Exe -Parent) -Parent
	$RootFull = (Get-FullPath $Root).TrimEnd('\')
	$OwnFull = (Get-FullPath $RunnerDir).TrimEnd('\')
	if ($RootFull -eq $OwnFull) { continue }

	# Nested runner folders would share permissions and workspaces, which is what this avoids.
	if ($OwnFull.StartsWith("$RootFull\", [StringComparison]::OrdinalIgnoreCase) -or $RootFull.StartsWith("$OwnFull\", [StringComparison]::OrdinalIgnoreCase))
	{
		throw "$RunnerDir and the folder of runner $($Service.Name), $Root, would be inside one another. Pass -RunnerRoot with a folder outside it, such as C:\rp."
	}

	Write-Host "  Runner $($Service.Name), as $($Service.StartName), in $Root"
	$Protect.Add($Root)

	# A runner can keep its work folder outside its install.
	$Settings = Join-Paths $Root '.runner'
	if (Test-Path -LiteralPath $Settings)
	{
		$Work = (([IO.File]::ReadAllText($Settings)).TrimStart([char]0xFEFF) | ConvertFrom-Json).workFolder
		if ($Work -and [IO.Path]::IsPathRooted($Work) -and -not (Get-FullPath $Work).StartsWith((Get-FullPath $Root), [StringComparison]::OrdinalIgnoreCase))
		{
			Write-Host "    work folder $Work"
			$Protect.Add($Work)
		}
	}
}
foreach ($Path in $ProtectPaths)
{
	if (Test-Path -LiteralPath $Path) { $Protect.Add($Path) }
	else { Write-Host "  $Path does not exist; skipped" }
}
$Protect = @($Protect | ForEach-Object { (Get-FullPath $_).TrimEnd('\') } | Select-Object -Unique)

# Everything outside the user profiles that inherits the default ACL is readable by any local user.
# List the top-level folders the account would be able to read, so nothing private is missed.
$Ignore = @('Windows', 'Program Files', 'Program Files (x86)', 'ProgramData', 'Users', '$Recycle.Bin', 'System Volume Information', 'Recovery', 'PerfLogs', 'Config.Msi', 'Documents and Settings', 'OneDriveTemp')
$Readable = @()
foreach ($Drive in @(Get-CimInstance Win32_LogicalDisk -Filter 'DriveType = 3'))
{
	foreach ($Folder in @(Get-ChildItem -LiteralPath "$($Drive.DeviceID)\" -Directory -Force -ErrorAction SilentlyContinue))
	{
		if ($Ignore -contains $Folder.Name) { continue }
		$Full = $Folder.FullName.TrimEnd('\')
		if ($Protect -contains $Full) { continue }
		if ((Get-FullPath $RunnerRoot).TrimEnd('\') -eq $Full) { continue }
		if (Test-ReadableByAnyUser $Full) { $Readable += $Full }
	}
}

Write-Host '  Will deny the runner account all access to:'
$Protect | ForEach-Object { Write-Host "    $_" }
if ($Readable.Count -gt 0)
{
	Write-Host '  Other top-level folders the runner account could read. Re-run with them in -ProtectPaths if any hold source, credentials or other private data:' -ForegroundColor Yellow
	$Readable | ForEach-Object { Write-Host "    $_" -ForegroundColor Yellow }
}

# 3. Registration token, before anything changes -------------------------------------------------

if (-not $RegistrationToken -and -not $WhatIfPreference)
{
	if (Get-Command gh -ErrorAction SilentlyContinue)
	{
		$Result = Invoke-NativeQuiet gh @('api', '-X', 'POST', $TokenEndpoint, '--jq', '.token')
		if ($Result.ExitCode -eq 0 -and $Result.Output) { $RegistrationToken = ([string]$Result.Output[0]).Trim() }
	}
	if (-not $RegistrationToken)
	{
		Write-Host ''
		Write-Host "Open $TokenPage and copy the token from the configure command (the value after --token)."
		$SecureToken = Read-Host 'Registration token' -AsSecureString
		$Pointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($SecureToken)
		try { $RegistrationToken = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($Pointer) }
		finally { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($Pointer) }
	}
	if (-not $RegistrationToken) { throw 'No registration token.' }
}

# 4. Account -------------------------------------------------------------------------------------

Write-Step "Account $QualifiedAccount"

# Characters that survive config.cmd's trip through cmd.exe unquoted.
$Classes = @('ABCDEFGHJKLMNPQRSTUVWXYZ', 'abcdefghijkmnopqrstuvwxyz', '23456789', '-_.+=@#*')
$Alphabet = -join $Classes
$Random = New-Object byte[] 32
$Generator = New-Object Security.Cryptography.RNGCryptoServiceProvider
try { $Generator.GetBytes($Random) } finally { $Generator.Dispose() }
$PasswordChars = foreach ($Index in 0..($Random.Length - 1))
{
	# One of each class first, so any complexity policy is met.
	if ($Index -lt $Classes.Count) { $Classes[$Index][$Random[$Index] % $Classes[$Index].Length] }
	else { $Alphabet[$Random[$Index] % $Alphabet.Length] }
}
$Password = -join $PasswordChars
$SecurePassword = ConvertTo-SecureString $Password -AsPlainText -Force

if ($PSCmdlet.ShouldProcess($QualifiedAccount, 'Create or reset local standard account'))
{
	if (Get-LocalUser -Name $Account -ErrorAction SilentlyContinue)
	{
		Set-LocalUser -Name $Account -Password $SecurePassword -PasswordNeverExpires $true
		Enable-LocalUser -Name $Account
		Write-Host '  Existed; password reset for the new service.'
	}
	else
	{
		New-LocalUser -Name $Account -Password $SecurePassword -PasswordNeverExpires -UserMayNotChangePassword -AccountNeverExpires -Description "GitHub Actions runner for $Target" | Out-Null
		Write-Host '  Created.'
	}

	# New-LocalUser adds no groups. Users gives it what a normal account can read, such as the engine.
	if (-not (Test-LocalGroupMember 'S-1-5-32-545' $Account))
	{
		Add-LocalGroupMember -SID 'S-1-5-32-545' -Member $Account
	}
	if (Test-LocalGroupMember 'S-1-5-32-544' $Account)
	{
		throw "$QualifiedAccount is an administrator. A runner that builds pull requests must not be; remove it from Administrators and run this again."
	}
}

# 5. Folder access -------------------------------------------------------------------------------

Write-Step 'Folder access'

foreach ($Path in $Protect)
{
	# Inherited down the tree, so a large workspace takes a while. Deny wins over any allow.
	if ($PSCmdlet.ShouldProcess($Path, "Deny $QualifiedAccount all access"))
	{
		Write-Host "  Denying $Path ..."
		& icacls.exe $Path /deny "${QualifiedAccount}:(OI)(CI)F" /C /Q | Out-Null
		if ($LASTEXITCODE -ne 0) { Write-Problem "icacls could not fully protect $Path (exit $LASTEXITCODE). Check it by hand." }
	}
}

if ($PSCmdlet.ShouldProcess($RunnerDir, "Create, readable only by $QualifiedAccount, SYSTEM and Administrators"))
{
	New-Item -ItemType Directory -Path $RunnerDir -Force | Out-Null
	& icacls.exe $RunnerDir /inheritance:r /grant:r '*S-1-5-18:(OI)(CI)F' '*S-1-5-32-544:(OI)(CI)F' "${QualifiedAccount}:(OI)(CI)M" /Q | Out-Null
	if ($LASTEXITCODE -ne 0) { throw "Setting permissions on $RunnerDir failed (exit $LASTEXITCODE)." }
}

# 6. Runner --------------------------------------------------------------------------------------

Write-Step 'GitHub Actions runner'

[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
$Release = Invoke-RestMethod -Uri 'https://api.github.com/repos/actions/runner/releases/latest' -Headers @{ 'User-Agent' = 'KhaosPluginCI' }
$RunnerVersion = $Release.tag_name.TrimStart('v')
$AssetName = "actions-runner-win-x64-$RunnerVersion.zip"
$Asset = $Release.assets | Where-Object { $_.name -eq $AssetName } | Select-Object -First 1
if (-not $Asset) { throw "Release $($Release.tag_name) has no $AssetName." }

$Published = @()
if ($Asset.PSObject.Properties['digest'] -and $Asset.digest -match '^sha256:([0-9a-f]{64})$') { $Published += $Matches[1] }
if ($Release.body -match '<!-- BEGIN SHA win-x64 -->([0-9a-f]{64})<!-- END SHA win-x64 -->') { $Published += $Matches[1] }
$Published = @($Published | Select-Object -Unique)
if ($Published.Count -ne 1) { throw "Could not find one published SHA-256 for $AssetName (found $($Published.Count))." }
Write-Host "  $AssetName, $([math]::Round($Asset.size / 1MB)) MB, sha256 $($Published[0])"

if ($PSCmdlet.ShouldProcess($RunnerDir, "Download $AssetName, verify and unpack"))
{
	$Zip = Join-Paths ([IO.Path]::GetTempPath()) $AssetName
	Invoke-WebRequest -Uri $Asset.browser_download_url -OutFile $Zip -UseBasicParsing
	$Actual = Get-FileSha256 $Zip
	if ($Actual -ne $Published[0])
	{
		Remove-Item -LiteralPath $Zip -Force
		throw "$AssetName has SHA-256 $Actual, not the published $($Published[0]). Not installing it."
	}
	Add-Type -AssemblyName System.IO.Compression.FileSystem
	[System.IO.Compression.ZipFile]::ExtractToDirectory($Zip, $RunnerDir)
	Remove-Item -LiteralPath $Zip -Force
	Write-Host '  Verified and unpacked.'
}

if ($PSCmdlet.ShouldProcess($RunnerName, "Register with $Url and install as a service running as $QualifiedAccount"))
{
	Push-Location $RunnerDir
	try
	{
		& .\config.cmd --unattended --url $Url --token $RegistrationToken --name $RunnerName --labels ($Labels -join ',') --work _w --runasservice --windowslogonaccount $QualifiedAccount --windowslogonpassword $Password --replace
		if ($LASTEXITCODE -ne 0) { throw "config.cmd failed with exit code $LASTEXITCODE." }
	}
	finally
	{
		Pop-Location
	}

	$Service = Get-Service | Where-Object { $_.Name -like "actions.runner.*.$RunnerName" } | Select-Object -First 1
	if (-not $Service) { throw 'The runner service was not installed.' }
	if ($Service.Status -ne 'Running') { Start-Service -Name $Service.Name }
	Write-Host "  Service $($Service.Name) is $((Get-Service -Name $Service.Name).Status)."
}

# Done -------------------------------------------------------------------------------------------

Write-Step 'Done'
Write-Host "Runner $RunnerName for $Target with labels self-hosted, Windows, X64, $($Labels -join ', ')."
if ($IsRepository)
{
	Write-Host 'Public repository? Require approval for workflows from all outside contributors:'
	Write-Host "  gh api -X PUT orgs/$($Target.Split('/')[0])/actions/permissions/fork-pr-contributor-approval -f approval_policy=all_external_contributors"
}
if ($script:Problems.Count -gt 0)
{
	Write-Host ''
	Write-Host 'Still to fix on this machine:' -ForegroundColor Yellow
	$script:Problems | ForEach-Object { Write-Host "  - $_" -ForegroundColor Yellow }
}
