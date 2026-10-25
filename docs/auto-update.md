# Release launcher updates

The release launcher checks `https://api.github.com/repos/rafaelkenedy/genma-recomp/releases/latest` at startup. Only strictly newer `vMAJOR.MINOR.PATCH` stable releases and the exact `GenmaOnimusha-<tag>-win64.zip` asset qualify. Drafts, prereleases, development/local versions, missing SHA-256 digests and ambiguous assets are ignored. `--no-update` skips the check. Failed checks fall through to the installed game; the worker has a four-second HTTP timeout and the launcher kills a stalled check after eight seconds.

The existing native message box asks before any download. Choosing No starts the installed game. Yes opens the existing progress window, downloads into `.update/`, verifies size and the GitHub release asset's SHA-256 digest, and extracts into a separate stage. Cancel terminates this preparation worker, before installation has changed any application file.

The ZIP is checked for traversal, absolute paths, streams, reserved names, case collisions, links, excessive sizes and incomplete content. A version marker and the launcher, runtime, symbols, recompiler and compiler must all be present. The package script writes `sdk/update-version.txt` and includes the trusted worker at `sdk/update.ps1`.

Only `sdk/`, `toolchain/`, the launcher and README are replaced. `assets/`, `work/` and the entire `game/` tree (including saves, settings, built game and texture packs) are excluded. This also preserves user replacements of bundled fonts. New bundled texture/font assets can still be installed manually from a release ZIP.

## Installation and recovery

The updater copies its current bundled worker to `.update/worker.ps1`; it never executes a downloaded updater script. It starts that copy in Apply mode, exits the launcher and releases the installation lock. The worker waits for the parent to exit, takes the lock and checks that the built game is not running before moving application components. It refuses linked installation/staging paths. No elevation is requested.

The worker acknowledges readiness before the launcher exits. Each replacement first journals its intent atomically. Directories move through `.update/backup/`; files use atomic replacement with a backup, keeping the launcher present throughout. A failure restores the recorded components in reverse order. If the process is interrupted, the next launcher starts the same worker in Recover mode and exits before recovery replaces the launcher's own executable. Recovery is idempotent. An unrecoverable filesystem error retains the journal and backups for another attempt, and prevents starting a mixed installation.

After a successful installation or recovery, staging/download/backup cleanup is best effort, and the launcher restarts with its original arguments plus `--skip-update-once`. Updater flags are removed before starting the game. The existing `game/build.txt` release stamp triggers a rebuild using the new runtime. Recovery covers application-file installation, not failures in the subsequent game rebuild. HTTPS and the digest from the configured GitHub repository establish package integrity; there is no independent publisher signature.

## Validation

`tools/test_updater.ps1` runs in Windows PowerShell 5.1 on isolated fixtures under `out/`. It covers stable version/asset selection, ZIP validation, SHA-256 and size mismatch, preservation of game data, a locked-launcher rollback, interrupted-install recovery, detached install/recovery and relaunch arguments. When given the compiled `-Launcher` and matching `-FixtureVersion`, it also tests the actual launcher with a valid existing build stamp, paths containing spaces, argument escaping, flag filtering, failed checks and stalled checks. No real installation or release is changed.

```
powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File tools/test_updater.ps1
```

The GitHub build runs the same tests against its compiled launcher before packaging the ZIP. A separate read-only live check against GitHub selected v0.3.1 from a v0.0.1 fixture. Full release-to-release download and user confirmation still need testing after publishing the first release containing this updater; v0.3.1 itself has no updater or version marker.
