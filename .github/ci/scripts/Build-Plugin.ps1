<#
.SYNOPSIS
Compile a plugin source zip against an installed engine the way Fab does, with BuildPlugin.

.DESCRIPTION
Epic compiles every code plugin from the submitted source for each engine version on the listing,
and one failure declines the whole submission. This runs the same BuildPlugin on the same zip
first.

The normal build is what Epic runs, and its output, with binaries, is what the tests load. An
optional second pass with -StrictIncludes turns off unity builds and precompiled headers, which
catches a .cpp that only compiles because some other file included what it needed.

Compiler errors and warnings in the plugin's own sources are reported as annotations on the
original file, so they show up on the pull request diff.

.EXAMPLE
./Build-Plugin.ps1 -PackageZip ./dist/KhaosDocs-0.2.0-UE5.8.zip -EngineVersion 5.8 -WorkDir C:\t\b -StrictIncludes
#>
[CmdletBinding()]
param(
	[Parameter(Mandatory)][string]$PackageZip,
	[Parameter(Mandatory)][string]$EngineVersion,

	# Scratch folder. Keep it short: plugin intermediates nest deep and Windows paths stop at 260.
	[Parameter(Mandatory)][string]$WorkDir,

	[string]$TargetPlatforms = 'Win64',
	[switch]$StrictIncludes,

	# Where UnrealBuildTool may keep anything, instead of the machine-wide defaults other projects
	# on the build machine use. Defaults to a folder in WorkDir.
	[string]$CacheDir
)

$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'KhaosPluginCI.psm1') -Force

$EngineRoot = Find-UnrealEngine $EngineVersion
$RunUAT = Join-Paths $EngineRoot 'Engine' 'Build' 'BatchFiles' 'RunUAT.bat'
Write-Host "Unreal Engine $((Get-UnrealEngineBuild $EngineRoot).Description) at $EngineRoot"

$WorkDir = Get-FullPath $WorkDir
if (-not $CacheDir) { $CacheDir = Join-Paths $WorkDir 'cache' }
$Cache = Get-PluginCache $CacheDir
Set-UnrealBuildToolIsolation $Cache
Write-Host "UnrealBuildTool caches in $($Cache.Root), local executor only"
$SourceRoot = Join-Paths $WorkDir 'src'
$LogDir = Join-Paths $WorkDir 'logs'
Expand-PluginZip -ZipPath (Get-FullPath $PackageZip) -Destination $SourceRoot
New-Item -ItemType Directory -Path $LogDir -Force | Out-Null

$PluginFolder = @(Get-ChildItem -LiteralPath $SourceRoot -Directory)
if ($PluginFolder.Count -ne 1) { throw "Expected one plugin folder at the root of $PackageZip." }
$Descriptor = Get-PluginDescriptor $PluginFolder[0].FullName
$PluginName = $Descriptor.Name

$ReportDiagnostic = New-CompileDiagnosticReporter -PluginName $PluginName -Label "UE $EngineVersion"

function Invoke-BuildPlugin([string]$PackageDir, [string]$LogName, [string[]]$Extra)
{
	# UAT writes its own and UBT's logs here, so they can be uploaded with the run.
	$env:uebp_LogFolder = Join-Paths $LogDir $LogName
	New-Item -ItemType Directory -Path $env:uebp_LogFolder -Force | Out-Null

	$Arguments = @(
		'BuildPlugin'
		"-Plugin=$($Descriptor.Path)"
		"-Package=$PackageDir"
		"-TargetPlatforms=$TargetPlatforms"
		'-nop4'
		'-utf8output'
	) + $Extra

	$ExitCode = Invoke-NativeCommand -FilePath $RunUAT -ArgumentList $Arguments -OnLine $ReportDiagnostic
	if ($ExitCode -ne 0)
	{
		throw "BuildPlugin ($LogName) failed with exit code $ExitCode against Unreal Engine $EngineVersion. Fab would decline this submission."
	}
}

$PackageDir = Join-Paths $WorkDir 'out' $PluginName

Start-CiGroup "BuildPlugin $PluginName for UE $EngineVersion"
Invoke-BuildPlugin -PackageDir $PackageDir -LogName 'BuildPlugin' -Extra @()
Stop-CiGroup

if ($StrictIncludes)
{
	Start-CiGroup "BuildPlugin $PluginName for UE $EngineVersion, strict includes"
	$StrictDir = Join-Paths $WorkDir 'strict' $PluginName
	Invoke-BuildPlugin -PackageDir $StrictDir -LogName 'BuildPlugin-StrictIncludes' -Extra @('-StrictIncludes')
	Remove-Item -LiteralPath (Join-Paths $WorkDir 'strict') -Recurse -Force
	Stop-CiGroup
}

$Modules = Join-Paths $PackageDir 'Binaries' 'Win64' 'UnrealEditor.modules'
if ($TargetPlatforms -match 'Win64' -and -not (Test-Path -LiteralPath $Modules))
{
	throw "BuildPlugin reported success but $Modules is missing; the editor would not load the plugin."
}

Write-Host "Built $PluginName for Unreal Engine $EngineVersion into $PackageDir"
Set-CiOutput 'plugin-dir' $PackageDir
Set-CiOutput 'plugin' $PluginName
