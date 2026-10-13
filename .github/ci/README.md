# Plugin CI

The pipeline that checks KhaosDocs against Fab's code plugin requirements, compiles it the way
Epic does when it receives a submission, runs its automation tests, and turns a version tag into
a release whose links and notes paste straight into the Fab publisher portal.

It lives here while it settles, and is written to move out to a shared repository for all the
Khaos Systems plugins once it runs smoothly; see [Moving it out](#moving-it-out). Nothing in it is
specific to KhaosDocs.

Fab has no publishing API, and none of the community tools submit for you either, so the last
step stays manual. It is now one link and one block of notes per engine version.

## Layout

| Path | |
| --- | --- |
| `.github/workflows/ci.yml` | Triggers and per-plugin settings. Calls the pipeline. |
| `.github/workflows/unreal-plugin.yml` | The pipeline, a reusable workflow. |
| `.github/ci/scripts/` | What the pipeline runs. Usable locally too. |
| `.github/ci/tests/Invoke-SelfTest.ps1` | Tests for the scripts. No engine needed. |

None of it ends up in the zip given to Fab, which only takes the descriptor, `Source`, `Content`,
`Config`, `Resources`, `Shaders` and what `Config/FilterPlugin.ini` adds.

## What runs

| Job | Where | When | What |
| --- | --- | --- | --- |
| Validate and package | GitHub-hosted Windows | Every push and pull request | Tests the scripts, checks the plugin against Fab's requirements, then writes the source zip Fab is given, one per engine version. |
| Build and test | Build machine | Pushes, tags, and pull requests from this repository | Compiles each zip with `BuildPlugin` against the Launcher-installed engine, again with strict includes, then loads the packaged plugin into a blank project and runs its automation tests. |
| Publish release | GitHub-hosted Windows | `vX.Y.Z` tags, once every engine passed | A GitHub release with the same zips, and a run summary with the Fab links and version notes. |
| Notify | GitHub-hosted Linux | Releases and failures | A Discord message, if `DISCORD_WEBHOOK_URL` is set. |
| Lint workflows | GitHub-hosted Linux | Every push and pull request | actionlint over the workflow files. |

The build works on the zip, not the checkout, so what passed is byte for byte what Fab receives.
Compile errors and failed tests are annotated on the source line, so they show up on the pull
request diff.

## Running it locally

From the plugin root, with the engine installed from the Launcher:

```powershell
.\.github\ci\scripts\New-SourcePackage.ps1 -PluginDir . -OutDir $env:TEMP\dist -EngineVersions 5.8
.\.github\ci\scripts\Build-Plugin.ps1 -PackageZip $env:TEMP\dist\KhaosDocs-0.1-UE5.8.zip -EngineVersion 5.8 -WorkDir C:\t\b -StrictIncludes
.\.github\ci\scripts\Invoke-PluginTests.ps1 -PluginDir C:\t\b\out\KhaosDocs -EngineVersion 5.8 -WorkDir C:\t\t
.\.github\ci\tests\Invoke-SelfTest.ps1
```

Keep the work folders short: plugin intermediates nest deep and Windows paths stop at 260
characters.

What the checks look for, and the usual first-run findings for a new plugin:
- Every module in the `.uplugin` needs a `PlatformAllowList` (or `PlatformDenyList`).
- Fab wants `Source/`, `Content/` and `Config/`. `Config/FilterPlugin.ini` also lists extra files
  to ship, for Epic's packaging and for the zip alike.
- The first line of every `.h`, `.cpp`, `.inl` and `.cs` outside `ThirdParty` must be a copyright
  notice naming the descriptor's `CreatedBy`.
- Strict includes: each file must include what it uses. Unity builds hide this locally; Epic's
  build of a fresh zip does not.
- Tests are named `<PluginName>.<Area>.<Case>`, which is the default filter. A run that matches no
  tests fails; a plugin with none yet sets `run-tests: false`.

## Releasing

```bash
git tag v1.2.0
git push origin v1.2.0
```

The tag is the version. It is stamped into the packaged descriptor as `VersionName` 1.2.0 and
`Version` 1002000 (major × 1,000,000 + minor × 1,000 + patch), so nobody edits the `.uplugin` to
release. `v2.0.0-rc.1` makes a prerelease.

When the run finishes, its summary has a table of project file links and the version notes. In
the [Fab publisher portal](https://www.fab.com/portal/listings): open the listing, open its Unreal
Engine format, and for each engine version paste the link and notes, then submit for review.
Keep the release until Fab approves the version, since Fab downloads the zip during review.

Release notes come from conventional commit subjects since the previous tag: `feat` and `fix`
get sections, `ci`, `chore`, `test`, `build` and `style` are left out, and everything else is
listed under other changes.

On the first submission there is no listing URL yet. Leave `FabURL` out of the `.uplugin`; once
the listing is live, set it and ship it in the next update.

### Private repositories

Release downloads of a private repository need a GitHub sign-in, which Fab does not have. The
release is still made for the team, and the run summary says so, but the zips need hosting
somewhere Fab can download them without signing in (Fab's docs suggest Google Drive, Dropbox or
OneDrive) before submitting. KhaosDocs is public, so this only matters once a private plugin
uses the pipeline.

## Using a plugin in a game

A stable release also moves the plugin's `release` branch to the tagged commit. It only moves
forward, and never for a prerelease, so it always holds the newest version that passed CI and was
published. Games add the plugin as a submodule tracking that branch:

```bash
git submodule add -b release https://github.com/KhaosSystems/KhaosDocs Plugins/KhaosDocs
```

and let Dependabot watch their submodules, in the game's `.github/dependabot.yml`:

```yaml
version: 2
updates:
  - package-ecosystem: gitsubmodule
    directory: /
    schedule:
      interval: daily
    allow:
      - dependency-name: Plugins/Khaos*
```

Dependabot follows the `branch` that `-b release` writes into `.gitmodules`, so within a day of a
release the game gets a pull request moving the submodule to it, with the plugin's commits listed.
`allow` keeps it to Khaos plugins; third-party submodules that track a moving branch are left
alone.

For the pull request to be tested before it is merged, the game's CI has to run on pull requests.

A private plugin also needs Dependabot to be able to read it: a fine-grained token with read-only
access to the plugin repositories, saved as a Dependabot secret in the game and given to Dependabot
as a `git` registry for `https://github.com`. The game's CI needs the same access to check the
submodule out.

## The build machine

The build job runs on a self-hosted runner with the labels in `build-runner`, by default
`self-hosted`, `Windows` and `unreal`. The `unreal` label marks runners set up by the script below,
with Unreal Engine installed and their own account and caches, so plugin builds never land on a
runner set up for something else.

Set one up with `scripts\Install-BuildRunner.ps1`, from an elevated Windows PowerShell on the
build machine. Try it with `-WhatIf` first; it changes nothing and shows what it would do.

```powershell
.\Install-BuildRunner.ps1 -Target KhaosSystems/KhaosDocs
```

It checks the machine, creates a dedicated standard Windows account, shuts that account out of the
other runners' folders and anything in `-ProtectPaths` (`C:\steamcmd` by default), lists any other
top-level folders every local account can read so you can add them, then installs a verified copy
of the GitHub Actions runner as a service in `C:\rp\<name>` running as that account. It refuses a
folder inside another runner's. It fetches the
registration token with the GitHub CLI when that is signed in, or asks for it.

The build machine needs:

- Each engine version in `engine-versions`, installed from the Epic Games Launcher. These are the
  same precompiled engines buyers use. An engine is found through the Launcher's install list,
  then the registry, then `C:\Program Files\Epic Games\UE_<version>`. Set `UE_ROOT_5_8` (and so
  on) in the runner's environment to point somewhere else; if that is set but wrong, the job
  fails rather than falling back to another install.

  A missing engine fails the job in its first step, with an annotation naming the machine and
  what to install. It is an error, not a skipped version, because Fab compiles the plugin for
  every engine version on the listing: one that cannot be built here would ship untested. Each
  run's summary records the exact engine build it used.
- The Visual Studio toolchain those engine versions require.
- Windows PowerShell 5.1, which ships with Windows. The scripts avoid anything newer.

Engine versions build one after another (`max-parallel: 1`); concurrent BuildPlugin runs on one
machine share UnrealBuildTool state and fail spuriously.

### Caches

The build machine also builds other projects, such as WhatSub, which keep their caches in their
own actions folders. A plugin build keeps its caches in its own: `.ue-cache\UE<version>` in the
repository's workspace on the runner, kept between runs. Nothing goes to the engine's default
cache locations, which are per user or per machine and so shared with everything else built
there:

| Default | Shared because | What the plugin build does instead |
| --- | --- | --- |
| UnrealBuildAccelerator store, `C:\ProgramData\Epic\UnrealBuildAccelerator` | One 40 GB store for every engine on the machine, so new entries evict others. Remote executors and UBA cache servers may also be set up for other projects. | UBA's root is moved into the cache folder. (Since 5.8 the UBA local executor is UnrealBuildTool's local executor and cannot be turned off.) Remote UBA, Horde, UBA cache servers, XGE, FASTBuild and SNDBS are off. Set through `UnrealBuildTool_<Section>__<Field>` environment variables, which UnrealBuildTool reads with the highest priority, on the build step only. |
| Zen local server, port 8558, data in `%LOCALAPPDATA%\UnrealEngine\Common\Zen` | One server for every engine of that Windows user. An editor that wants a different data folder or version shuts the running server down. | The test editor's derived data cache is a single file store in the cache folder (`-DDC=(Local)`), so no Zen server starts. Zen's data folder and port are pointed at the cache folder too, in case something starts it anyway. |
| Derived data cache, `%LOCALAPPDATA%\UnrealEngine\Common\DerivedDataCache`, plus any shared or cloud cache configured by environment variable | Every Launcher engine of that user, and any team-wide cache. | Not in the test editor's cache graph. |

Delete `.ue-cache` on the runner to start cold; nothing else depends on it.

### Public repositories

Building a plugin runs its code on the build machine, and `Build.cs` files are C#. The workflow
never builds a pull request from a fork, but a fork's pull request can change the workflow
itself, so that guard alone protects nothing. What does:

- Keep the organization's main build runner away from public repositories. In the Default runner
  group, "Allow public repositories" stays off.
- Run plugin builds on runners of their own, each under its own account, shut out of the other
  runners' folders, Steam and anything else private on the machine. Either one per repository
  (`-Target <org>/<repo>`), or one shared by several through a runner group, which the
  organization must be on the Team plan or above to have:
  - An `unreal` group with "Allow public repositories" on and the plugin repositories selected,
    and a runner in it: `-Target <org> -Name unreal -RunnerGroup unreal`.
  - Everything in one group shares one runner and account. If private repositories join, code
    from an approved public pull request could read their workspaces there. To rule that out,
    give private repositories a second group and runner, such as `-Name unreal-private
    -RunnerGroup unreal-private`.
- Require approval for workflows from all outside contributors, not only first-time ones, and
  read a fork's changes before approving a run:

  ```bash
  gh api -X PUT orgs/KhaosSystems/actions/permissions/fork-pr-contributor-approval -f approval_policy=all_external_contributors
  ```

## Inputs

Set in `ci.yml`.

| Input | Default | |
| --- | --- | --- |
| `engine-versions` | `'["5.8"]'` | JSON array of engine versions to build, test and package for. |
| `run-tests` | `true` | Run automation tests. |
| `test-filter` | plugin name | Automation test filter. |
| `strict-includes` | `true` | Also compile without unity builds or shared PCHs. |
| `build-runner` | `'["self-hosted", "Windows", "unreal"]'` | Runner labels for the build machine. |

## Moving it out

When it has run smoothly for a while:

1. Create a repository (public, so public plugins can call it) with `.github/ci`'s contents at its
   root and `unreal-plugin.yml` in its `.github/workflows`.
2. In `unreal-plugin.yml`, check that repository out at a fixed ref instead of using the plugin's
   `.github/ci`, and tag a release such as `v1`.
3. In each plugin, delete `.github/ci` and `unreal-plugin.yml`, and point `ci.yml` at
   `<org>/<repo>/.github/workflows/unreal-plugin.yml@v1`.

## Changing the scripts

Run `tests\Invoke-SelfTest.ps1` after a change; CI runs it before anything else. Keep the scripts
ASCII and Windows PowerShell 5.1 compatible: a BOM-less script is read in the ANSI code page, and
the build machine has no PowerShell 7.
