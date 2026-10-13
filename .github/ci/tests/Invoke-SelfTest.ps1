<#
.SYNOPSIS
Tests for the CI scripts themselves. No engine needed; runs anywhere Windows PowerShell 5.1 does.

.DESCRIPTION
Plain assertions rather than Pester, so it runs on a stock Windows install and on the GitHub
runner without installing anything. Exits non-zero if any check fails.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$Root = Split-Path $PSScriptRoot -Parent
Import-Module (Join-Path $Root 'scripts\KhaosPluginCI.psm1') -Force

$script:Failures = 0
$script:Passes = 0

function Check([string]$Name, [bool]$Condition, [string]$Detail = '')
{
	if ($Condition)
	{
		$script:Passes++
		Write-Host "  ok    $Name" -ForegroundColor DarkGray
	}
	else
	{
		$script:Failures++
		Write-Host "  FAIL  $Name $Detail" -ForegroundColor Red
	}
}

function Same($Actual, $Expected)
{
	return ((@($Actual) -join "`n") -ceq (@($Expected) -join "`n"))
}

function New-TestFile([string]$Path, [string]$Text)
{
	New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($Path)) -Force | Out-Null
	[IO.File]::WriteAllText($Path, $Text)
}

<# A small plugin that meets every requirement; cases break one thing each. #>
function New-FixturePlugin([string]$Dir)
{
	Reset-Directory $Dir
	New-TestFile "$Dir\Sample.uplugin" @'
{
	"FileVersion": 3,
	"Version": 1,
	"VersionName": "0.1",
	"FriendlyName": "Sample",
	"CreatedBy": "Khaos Games",
	"FabURL": "https://www.fab.com/listings/00000000-0000-0000-0000-000000000000",
	"CanContainContent": true,
	"Modules": [
		{
			"Name": "SampleEditor",
			"Type": "Editor",
			"LoadingPhase": "Default",
			"PlatformAllowList": [ "Win64" ]
		}
	]
}
'@
	New-TestFile "$Dir\Source\SampleEditor\SampleEditor.Build.cs" "// Copyright Khaos Games. All Rights Reserved.`n"
	New-TestFile "$Dir\Source\SampleEditor\Private\SampleEditor.cpp" "// Copyright Khaos Games. All Rights Reserved.`n"
	New-TestFile "$Dir\Source\ThirdParty\Lib\lib.h" "/* MIT, someone else */`n"
	New-TestFile "$Dir\Source\ThirdParty\Lib\LICENSE.txt" "MIT`n"
	New-TestFile "$Dir\Content\Docs\Readme.md" "# Sample`n"
	New-TestFile "$Dir\Config\FilterPlugin.ini" "[FilterPlugin]`n; comment`n/Extras/...`n-/Extras/Private/...`n"
	New-TestFile "$Dir\Resources\Icon128.png" "png"
	New-TestFile "$Dir\Extras\Notes.txt" "extra"
	New-TestFile "$Dir\Extras\Private\Secret.txt" "secret"
	New-TestFile "$Dir\README.md" "repo readme"
	New-TestFile "$Dir\.gitignore" "Binaries/"
	New-TestFile "$Dir\Tests\Gauntlet.txt" "not packaged"
	New-TestFile "$Dir\Binaries\Win64\UnrealEditor-SampleEditor.dll" "dll"
	New-TestFile "$Dir\Intermediate\Build\x.obj" "obj"
	New-TestFile "$Dir\Source\SampleEditor\Intermediate\y.obj" "obj"
}

<# Run a check and collect the annotations it printed. #>
function Get-Annotations([scriptblock]$Script)
{
	$env:GITHUB_ACTIONS = 'true'
	try
	{
		return @(& $Script 6>&1 | ForEach-Object { "$_" } | Where-Object { $_ -match '^::(error|warning|notice)' })
	}
	finally
	{
		Remove-Item Env:\GITHUB_ACTIONS -ErrorAction SilentlyContinue
	}
}

function Test-Fixture([string]$Dir)
{
	$Descriptor = Get-PluginDescriptor $Dir
	$Files = Get-PluginPackageFiles $Descriptor
	$script:LastResult = $null
	$Annotations = Get-Annotations { $script:LastResult = Test-PluginPackage -Descriptor $Descriptor -Files $Files }
	return [pscustomobject]@{ Result = $script:LastResult; Annotations = $Annotations; Errors = @($Annotations | Where-Object { $_ -like '::error*' }) }
}

$Temp = Join-Paths ([IO.Path]::GetTempPath()) ('kpci-selftest-' + [Guid]::NewGuid().ToString('N').Substring(0, 8))
$Plugin = Join-Paths $Temp 'Sample'

try
{
	Write-Host 'FileFilter patterns'
	$Cases = @(
		,@('/Content/...', 'Content/Docs/a.md', $true)
		,@('/Content/...', 'Source/Content/a.md', $false)
		,@('/Content/...', 'Content', $false)
		,@('*.md', 'README.md', $true)
		,@('*.md', 'Content/Docs/a.md', $true)
		,@('*.md', 'a.mdx', $false)
		,@('/README.md', 'README.md', $true)
		,@('/README.md', 'Docs/README.md', $false)
		,@('Docs/', 'Docs/a/b.txt', $true)
		,@('/Binaries/ThirdParty/*.dll', 'Binaries/ThirdParty/x.dll', $true)
		,@('/Binaries/ThirdParty/*.dll', 'Binaries/ThirdParty/Win64/x.dll', $false)
		,@('/Source/.../Tests/...', 'Source/A/Private/Tests/x.cpp', $true)
		,@('/Source/.../Tests/...', 'Source/Tests/x.cpp', $true)
		,@('/a?c.txt', 'abc.txt', $true)
		,@('/a?c.txt', 'a/c.txt', $false)
		,@('/Extras...', 'Extras2/x.txt', $true)
	)
	foreach ($Case in $Cases)
	{
		$Regex = ConvertTo-FilterRegex $Case[0]
		Check "'$($Case[0])' vs '$($Case[1])' is $($Case[2])" (([regex]::IsMatch($Case[1], $Regex)) -eq $Case[2]) "(regex $Regex)"
	}

	Write-Host 'Package contents'
	New-FixturePlugin $Plugin
	$Files = Get-PluginPackageFiles (Get-PluginDescriptor $Plugin)
	$Expected = @(
		'Config/FilterPlugin.ini'
		'Content/Docs/Readme.md'
		'Extras/Notes.txt'
		'Resources/Icon128.png'
		'Sample.uplugin'
		'Source/SampleEditor/Private/SampleEditor.cpp'
		'Source/SampleEditor/SampleEditor.Build.cs'
		'Source/ThirdParty/Lib/LICENSE.txt'
		'Source/ThirdParty/Lib/lib.h'
	)
	Check 'only the shippable files, sorted' (Same $Files $Expected) "got:`n$($Files -join "`n")"

	Write-Host 'Validation'
	$Clean = Test-Fixture $Plugin
	Check 'a compliant plugin has no errors or warnings' ($Clean.Annotations.Count -eq 0) ($Clean.Annotations -join "`n")
	Check 'counts are returned' ($Clean.Result.Errors -eq 0 -and $Clean.Result.Warnings -eq 0)

	$Breakages = @(
		@{
			Name = 'module without a platform list'
			Break = { $Path = "$Plugin\Sample.uplugin"; [IO.File]::WriteAllText($Path, ([IO.File]::ReadAllText($Path) -replace ',\s*"PlatformAllowList": \[ "Win64" \]', '')) }
			Expect = 'PlatformAllowList'
		}
		@{
			Name = 'source file without a copyright line'
			Break = { New-TestFile "$Plugin\Source\SampleEditor\Private\New.cpp" "#include `"x.h`"`n" }
			Expect = 'file=Source/SampleEditor/Private/New.cpp,line=1'
		}
		@{
			Name = "Epic's notice left in from a template"
			Break = { New-TestFile "$Plugin\Source\SampleEditor\Private\New.h" "// Copyright Epic Games, Inc. All Rights Reserved.`n" }
			Expect = "should name 'Khaos Games'"
		}
		@{
			Name = 'license file outside ThirdParty'
			Break = { New-TestFile "$Plugin\Content\LICENSE" "x" }
			Expect = 'file=Content/LICENSE'
		}
		@{
			Name = 'nested archive'
			Break = { New-TestFile "$Plugin\Content\Extra.zip" "x" }
			Expect = 'nested archives'
		}
		@{
			Name = 'no Config folder'
			Break = { Remove-Item "$Plugin\Config" -Recurse -Force }
			Expect = 'Config/ folder'
		}
		@{
			Name = 'module with no Build.cs'
			Break = { Remove-Item "$Plugin\Source\SampleEditor\SampleEditor.Build.cs" }
			Expect = 'no SampleEditor.Build.cs'
		}
	)
	foreach ($Breakage in $Breakages)
	{
		New-FixturePlugin $Plugin
		& $Breakage.Break
		$Outcome = Test-Fixture $Plugin
		$Hit = @($Outcome.Errors | Where-Object { $_.Contains($Breakage.Expect) })
		Check "reports $($Breakage.Name)" ($Hit.Count -gt 0 -and $Outcome.Result.Errors -gt 0) ($Outcome.Annotations -join "`n")
	}

	New-FixturePlugin $Plugin
	Remove-Item "$Plugin\Resources\Icon128.png"
	$Outcome = Test-Fixture $Plugin
	Check 'a missing icon only warns' ($Outcome.Result.Errors -eq 0 -and $Outcome.Result.Warnings -eq 1)

	Write-Host 'Descriptor stamping'
	$Text = "{`r`n`t`"FileVersion`": 3,`r`n`t`"Version`": 1,`r`n`t`"VersionName`": `"0.1`",`r`n`t`"Plugins`": [ { `"Name`": `"X`", `"Enabled`": true } ]`r`n}`r`n"
	$Stamped = Set-DescriptorField $Text 'VersionName' '1.2.3'
	$Stamped = Set-DescriptorField $Stamped 'Version' 1002003
	$Stamped = Set-DescriptorField $Stamped 'EngineVersion' '5.8.0'
	$Json = $Stamped | ConvertFrom-Json
	Check 'replaces and inserts fields' ($Json.VersionName -eq '1.2.3' -and $Json.Version -eq 1002003 -and $Json.EngineVersion -eq '5.8.0')
	Check 'keeps tabs and CRLF' ($Stamped.Contains("`r`n`t`"EngineVersion`": `"5.8.0`",`r`n") -and $Stamped.Contains("`t`"VersionName`": `"1.2.3`""))
	Check 'leaves the rest alone' ($Stamped.Contains('"Plugins": [ { "Name": "X", "Enabled": true } ]'))

	Write-Host 'Release versions'
	$Release = ConvertFrom-ReleaseVersion 'v1.2.3'
	Check 'v1.2.3 is 1002003' ($Release.VersionName -eq '1.2.3' -and $Release.Version -eq 1002003)
	Check 'prereleases keep their suffix' ((ConvertFrom-ReleaseVersion 'v2.0.0-rc.1').VersionName -eq '2.0.0-rc.1')
	Check 'later releases sort higher' ((ConvertFrom-ReleaseVersion '1.10.0').Version -gt (ConvertFrom-ReleaseVersion '1.9.99').Version)
	$Threw = $false
	try { ConvertFrom-ReleaseVersion 'v1.2' | Out-Null } catch { $Threw = $true }
	Check 'rejects a two-part version' $Threw

	Write-Host 'Zips'
	New-FixturePlugin $Plugin
	$Files = Get-PluginPackageFiles (Get-PluginDescriptor $Plugin)
	New-PluginZip -SourceDir $Plugin -Files $Files -RootFolder 'Sample' -ZipPath "$Temp\a.zip"
	Start-Sleep -Milliseconds 1100
	(Get-Item "$Plugin\Content\Docs\Readme.md").LastWriteTime = Get-Date
	New-PluginZip -SourceDir $Plugin -Files $Files -RootFolder 'Sample' -ZipPath "$Temp\b.zip"
	Check 'same sources give the same bytes' ((Get-FileSha256 "$Temp\a.zip") -eq (Get-FileSha256 "$Temp\b.zip"))

	Add-Type -AssemblyName System.IO.Compression.FileSystem
	$Zip = [System.IO.Compression.ZipFile]::OpenRead("$Temp\a.zip")
	try { $Entries = @($Zip.Entries | ForEach-Object { $_.FullName }) } finally { $Zip.Dispose() }
	Check 'entries sit under one root folder with / separators' (Same $Entries ($Files | ForEach-Object { "Sample/$_" }))

	Expand-PluginZip -ZipPath "$Temp\a.zip" -Destination "$Temp\x"
	Check 'extracts back to the plugin' (Test-Path "$Temp\x\Sample\Source\SampleEditor\Private\SampleEditor.cpp")

	Write-Host 'Compiler output'
	$Log = @(
		'C:\b\out\Sample\HostProject\Plugins\Sample\Source\SampleEditor\Private\A.cpp(12,5): error C2065: ''Foo'': undeclared identifier'
		'C:\b\out\Sample\HostProject\Plugins\Sample\Source\SampleEditor\Private\A.cpp(12,5): error C2065: ''Foo'': undeclared identifier'
		'C:\b\out\Sample\HostProject\Plugins\Sample\Source\SampleEditor\Private\B.cpp(3): warning C4996: ''Old'': deprecated'
		'C:\Program Files\Epic Games\UE_5.8\Engine\Source\Runtime\Core\Public\X.h(19,3): warning C4100: engine noise'
		'C:\Program Files\Epic Games\UE_5.8\Engine\Source\Runtime\Core\Public\X.h(19,3): error C2027: use of undefined type ''UThing'''
		'C:\b\out\Sample\HostProject\Plugins\Sample\Source\SampleEditor\Private\C.h(16,7): note: see declaration of ''UThing'''
		'[3/8] Compile [x64] A.cpp'
	)
	$Report = New-CompileDiagnosticReporter -PluginName 'Sample' -Label 'UE 5.8'
	$Annotations = Get-Annotations { foreach ($Line in $Log) { & $Report $Line } }
	Check 'reports each plugin diagnostic once' ($Annotations.Count -eq 3) ($Annotations -join "`n")
	Check 'maps errors to the repository path' (@($Annotations | Where-Object { $_ -like '::error file=Source/SampleEditor/Private/A.cpp,line=12,*' }).Count -eq 1)
	Check 'keeps warnings as warnings' (@($Annotations | Where-Object { $_ -like '::warning file=Source/SampleEditor/Private/B.cpp,line=3,*' }).Count -eq 1)
	Check 'pins engine-header errors on the plugin line that caused them' (@($Annotations | Where-Object { $_ -like '::error file=Source/SampleEditor/Private/C.h,line=16,*undefined type*' }).Count -eq 1)

	Write-Host 'Engine lookup'
	$FakeEngine = Join-Paths $Temp 'UE_9.9'
	New-TestFile "$FakeEngine\Engine\Build\BatchFiles\RunUAT.bat" '@echo off'
	New-TestFile "$FakeEngine\Engine\Build\Build.version" '{ "MajorVersion": 9, "MinorVersion": 9, "PatchVersion": 1, "Changelist": 123, "BranchName": "++UE9+Release-9.9" }'
	try
	{
		$env:UE_ROOT_9_9 = $FakeEngine
		Check 'an override that is the right engine is used' ((Find-UnrealEngine '9.9') -eq $FakeEngine)
		Check 'the build is described exactly' ((Get-UnrealEngineBuild $FakeEngine).Description -eq '9.9.1, changelist 123, ++UE9+Release-9.9')

		$env:UE_ROOT_9_9 = Join-Paths $Temp 'nowhere'
		$Annotations = Get-Annotations { try { Find-UnrealEngine '9.9' | Out-Null } catch { $script:Caught = $_.Exception.Message } }
		Check 'an override pointing nowhere is an error, not a fallback' ($script:Caught -like 'UE_ROOT_9_9 is set to*no engine there*' -and @($Annotations | Where-Object { $_ -like '::error*Unreal Engine 9.9 not found*' }).Count -eq 1) "$script:Caught"

		New-TestFile "$FakeEngine\Engine\Build\Build.version" '{ "MajorVersion": 9, "MinorVersion": 8, "PatchVersion": 0, "Changelist": 1, "BranchName": "x" }'
		$env:UE_ROOT_9_9 = $FakeEngine
		$script:Caught = $null
		$null = Get-Annotations { try { Find-UnrealEngine '9.9' | Out-Null } catch { $script:Caught = $_.Exception.Message } }
		Check 'an override pointing at another version is an error' ($script:Caught -like '*that is Unreal Engine 9.8*') "$script:Caught"

		Remove-Item Env:\UE_ROOT_9_9
		$script:Caught = $null
		$Annotations = Get-Annotations { try { Find-UnrealEngine '9.9' | Out-Null } catch { $script:Caught = $_.Exception.Message } }
		Check 'a missing engine is an error that says how to fix it' ($script:Caught -like 'Unreal Engine 9.9 is not installed on*Epic Games Launcher*UE_ROOT_9_9*' -and $Annotations.Count -eq 1) "$script:Caught"
	}
	finally
	{
		Remove-Item Env:\UE_ROOT_9_9 -ErrorAction SilentlyContinue
	}

	Write-Host 'Cache isolation'
	$Cache = Get-PluginCache (Join-Paths $Temp 'cache\UE5.8')
	Check 'cache folders are created under the given root' ((Test-Path $Cache.Ddc) -and (Test-Path $Cache.Zen) -and (Test-Path $Cache.Uba) -and $Cache.Ddc.StartsWith($Cache.Root))
	Check 'the Zen port avoids the shared default' ($Cache.ZenPort -ge 8600 -and $Cache.ZenPort -lt 9000)
	Check 'the Zen port is stable for a folder' ((Get-PluginCache $Cache.Root).ZenPort -eq $Cache.ZenPort)
	$Other = Get-PluginCache (Join-Paths $Temp 'cache\UE5.7')
	Check 'different folders usually get different ports' ($Other.ZenPort -ne $Cache.ZenPort)

	$Saved = @{}
	Get-ChildItem Env: | Where-Object { $_.Name -like 'UnrealBuildTool_*' } | ForEach-Object { $Saved[$_.Name] = $_.Value }
	try
	{
		Set-UnrealBuildToolIsolation $Cache
		Check 'UnrealBuildTool stays on this machine, with no cache server' ($env:UnrealBuildTool_BuildConfiguration__bAllowUBAExecutor -eq 'false' -and $env:UnrealBuildTool_BuildConfiguration__bAllowXGE -eq 'false' -and $env:UnrealBuildTool_UnrealBuildAccelerator__ForceNoCache -eq 'true' -and $env:UnrealBuildTool_UnrealBuildAccelerator__bDisableHorde -eq 'true')
		Check "UBA's root is the cache folder, with a small cap" ($env:UnrealBuildTool_UnrealBuildAccelerator__RootDir -eq $Cache.Uba -and $env:UnrealBuildTool_UnrealBuildAccelerator__StoreCapacityGb -eq '5')
	}
	finally
	{
		Get-ChildItem Env: | Where-Object { $_.Name -like 'UnrealBuildTool_*' } | ForEach-Object { Remove-Item "Env:\$($_.Name)" }
		foreach ($Name in $Saved.Keys) { [Environment]::SetEnvironmentVariable($Name, $Saved[$Name], 'Process') }
	}

	$EditorArguments = (Get-EditorCacheArguments $Cache) -join ' '
	Check 'the editor cache graph has no Zen, shared or cloud store' ($EditorArguments -match '(^| )-DDC=\(Local\)( |$)')
	Check 'the editor cache and Zen live in the cache folder' ($EditorArguments.Contains("-LocalDataCachePath=`"$($Cache.Ddc)`"") -and $EditorArguments.Contains("-ZenDataPath=`"$($Cache.Zen)`"") -and $EditorArguments.Contains("DesiredPort=$($Cache.ZenPort)"))

	Write-Host 'Release notes'
	$Repo = Join-Paths $Temp 'repo'
	New-Item -ItemType Directory -Path $Repo -Force | Out-Null
	$Git = { param([string[]]$Arguments) $null = Invoke-NativeQuiet git (@('-C', $Repo, '-c', 'user.name=Test', '-c', 'user.email=test@example.com') + $Arguments) }
	& $Git @('init', '-q')
	& $Git @('commit', '-q', '--allow-empty', '-m', 'feat: first thing')
	& $Git @('tag', 'v0.1.0')
	foreach ($Subject in @('feat(ui): new panel', 'fix: crash on save', 'ci: pipeline tweak', 'Update readme', 'docs: authoring guide'))
	{
		& $Git @('commit', '-q', '--allow-empty', '-m', $Subject)
	}
	& $Git @('tag', 'v0.2.0')

	$First = Get-ReleaseNotes -RepoDir $Repo -Tag 'v0.1.0'
	Check 'the first release lists everything before it' ($First -match '(?m)^### Features\n- first thing \([0-9a-f]+\)$') $First
	$Second = Get-ReleaseNotes -RepoDir $Repo -Tag 'v0.2.0'
	Check 'later releases start at the previous tag' (-not $Second.Contains('first thing')) $Second
	Check 'features and fixes get sections' ($Second -match '### Features\n- new panel \(' -and $Second -match '### Fixes\n- crash on save \(') $Second
	Check 'other commits are kept, conventional or not' ($Second.Contains('- authoring guide (') -and $Second.Contains('- Update readme (')) $Second
	Check 'housekeeping is left out' (-not $Second.Contains('pipeline tweak')) $Second

	Write-Host 'Scripts'
	foreach ($Script in Get-ChildItem (Join-Path $Root 'scripts'), (Join-Path $Root 'tests') -Include '*.ps1', '*.psm1' -Recurse)
	{
		$Bytes = [IO.File]::ReadAllBytes($Script.FullName)
		$NonAscii = @($Bytes | Where-Object { $_ -gt 127 }).Count
		# Windows PowerShell reads a BOM-less script as the ANSI code page, so anything beyond ASCII changes meaning.
		Check "$($Script.Name) is ASCII" ($NonAscii -eq 0)

		$Errors = $null
		[void][System.Management.Automation.Language.Parser]::ParseFile($Script.FullName, [ref]$null, [ref]$Errors)
		Check "$($Script.Name) parses" ($Errors.Count -eq 0) (($Errors | ForEach-Object { $_.Message }) -join '; ')
	}
}
finally
{
	if (Test-Path $Temp) { Remove-Item $Temp -Recurse -Force }
}

Write-Host ''
Write-Host "$script:Passes passed, $script:Failures failed."
if ($script:Failures -gt 0) { exit 1 }
