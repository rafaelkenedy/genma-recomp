# No network and no real installation: exercise the shipped worker on disposable fixtures.
param([string]$Launcher = '', [string]$FixtureVersion = 'local')
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../sdk/launcher/update.ps1')
function Assert($Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
function Assert-Throws([scriptblock]$Action, [string]$Message) {
    $threw = $false
    try { & $Action } catch { $threw = $true }
    Assert $threw $Message
}
$testRoot = Join-Path ([IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../out'))) ('updater-test-' + [guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($testRoot) | Out-Null
$repo = 'rafaelkenedy/genma-recomp'
$name = 'GenmaOnimusha'
$native = $null
function New-Release([string]$Tag = 'v0.4.0') {
    return [pscustomobject]@{ tag_name = $Tag; draft = $false; prerelease = $false; assets = @(
        [pscustomobject]@{ name = "$name-$Tag-win64.zip"; state = 'uploaded'; digest = 'sha256:' + ('a' * 64);
            size = 100; browser_download_url = "https://github.com/$repo/releases/download/$Tag/$name-$Tag-win64.zip" }) }
}
function Put([string]$Path, [string]$Text) {
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Path)) | Out-Null
    Write-Utf8 $Path $Text
}
function Make-Stage([string]$Install) {
    Put (Join-Path $Install ".update/stage/$name.exe") 'new launcher'
    Put (Join-Path $Install '.update/stage/sdk/new.txt') 'new sdk'
    Put (Join-Path $Install '.update/stage/toolchain/new.txt') 'new compiler'
}
function Make-Install([string]$Install) {
    Put (Join-Path $Install "$name.exe") 'old launcher'
    Put (Join-Path $Install 'sdk/old.txt') 'old sdk'
    Put (Join-Path $Install 'toolchain/old.txt') 'old compiler'
    foreach ($path in @('game/settings.toml', 'game/hdd/save.dat', 'game/textures/custom.dds',
        'game/GenmaOnimusha.exe', 'assets/game_path.txt', 'assets/own.iso', 'work/default.xbe')) { Put (Join-Path $Install $path) 'user data' }
}
function New-Zip([string]$Path, [string[]]$Entries) {
    $zip = [IO.Compression.ZipFile]::Open($Path, [IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($entry in $Entries) {
            $item = $zip.CreateEntry($entry)
            $writer = [IO.StreamWriter]::new($item.Open())
            try { $writer.Write('fixture') } finally { $writer.Dispose() }
        }
    } finally { $zip.Dispose() }
}
try {
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    Assert ($null -ne (Select-Update (New-Release) 'v0.3.1' $repo $name)) 'New stable version was not selected.'
    $mixed = New-Release
    $mixed.assets = @([pscustomobject]@{name="$name-v0.4.0-linux-x86_64.tar.gz"; state='uploaded'; digest='sha256:' + ('b' * 64); size=200; browser_download_url="https://github.com/$repo/releases/download/v0.4.0/$name-v0.4.0-linux-x86_64.tar.gz"}) + @($mixed.assets)
    $selected = Select-Update $mixed 'v0.3.1' $repo $name
    Assert ($null -ne $selected -and $selected.url.EndsWith('-win64.zip')) 'Mixed-platform release did not select Windows ZIP.'
    Assert ($null -ne (Select-Update (New-Release 'v0.10.0') 'v0.9.0' $repo $name)) 'Versions compared lexically.'
    foreach ($current in @('v0.4.0', 'v0.5.0', 'dev', 'local', 'dev-1234')) {
        Assert ($null -eq (Select-Update (New-Release) $current $repo $name)) "Unexpected update from $current"
    }
    $release = New-Release
    $release.prerelease = $true
    Assert ($null -eq (Select-Update $release 'v0.3.1' $repo $name)) 'Prerelease selected.'
    $release = New-Release
    $release.draft = $true
    Assert ($null -eq (Select-Update $release 'v0.3.1' $repo $name)) 'Draft selected.'
    $release = New-Release
    $release.assets[0].digest = ''
    Assert ($null -eq (Select-Update $release 'v0.3.1' $repo $name)) 'Missing digest accepted.'
    $release = New-Release
    $release.assets[0].browser_download_url = 'https://example.com/untrusted.zip'
    Assert ($null -eq (Select-Update $release 'v0.3.1' $repo $name)) 'Foreign download URL accepted.'
    $release = New-Release
    $release.assets += $release.assets[0]
    Assert ($null -eq (Select-Update $release 'v0.3.1' $repo $name)) 'Ambiguous assets accepted.'
    foreach ($entry in @('GenmaOnimusha/../escape', 'GenmaOnimusha/C:/escape', '/GenmaOnimusha/a',
        'OtherPackage/sdk/a', 'GenmaOnimusha/sdk/a:stream', 'GenmaOnimusha/sdk/NUL.txt', 'GenmaOnimusha/sdk/a.')) {
        Assert-Throws { Get-EntryPath $entry $name } "Unsafe path accepted: $entry"
    }
    $evilZip = Join-Path $testRoot 'traversal.zip'
    New-Zip $evilZip @('GenmaOnimusha/../escape')
    Assert-Throws { Expand-Update $evilZip (Join-Path $testRoot 'bad-stage') $name 'v0.4.0' } 'ZIP traversal accepted.'
    Assert (-not (Test-Path -LiteralPath (Join-Path $testRoot 'escape'))) 'ZIP escaped staging.'
    $duplicateZip = Join-Path $testRoot 'duplicate.zip'
    New-Zip $duplicateZip @('GenmaOnimusha/sdk/a', 'GenmaOnimusha/sdk/A')
    Assert-Throws { Expand-Update $duplicateZip (Join-Path $testRoot 'dup-stage') $name 'v0.4.0' } 'Case collision accepted.'

    $packageRoot = Join-Path $testRoot 'package'
    foreach ($path in @("$name.exe", 'sdk/bin/xbe_recomp.exe', 'sdk/include/image_symbols.h',
        "sdk/lib/lib${name}_runtime.a", 'sdk/update.ps1', 'toolchain/bin/x86_64-w64-mingw32-clang++.exe',
        'game/settings.toml', 'assets/own.iso')) { Put (Join-Path $packageRoot "$name/$path") 'release data' }
    Put (Join-Path $packageRoot "$name/sdk/update-version.txt") 'v0.4.0'
    $validZip = Join-Path $testRoot 'valid.zip'
    [IO.Compression.ZipFile]::CreateFromDirectory($packageRoot, $validZip)
    $digest = (Get-FileHash -LiteralPath $validZip -Algorithm SHA256).Hash.ToLowerInvariant()
    $zipSize = ([IO.FileInfo]$validZip).Length
    Assert-UpdateHash $validZip $digest $zipSize
    Assert-Throws { Assert-UpdateHash $validZip ('0' * 64) $zipSize } 'Tampered download accepted.'
    Assert-Throws { Assert-UpdateHash $validZip $digest ($zipSize + 1) } 'Truncated download accepted.'
    $stage = Join-Path $testRoot 'valid-stage'
    Expand-Update $validZip $stage $name 'v0.4.0'
    Assert ([IO.File]::Exists((Join-Path $stage "$name.exe"))) 'Launcher not extracted.'
    Assert (-not (Test-Path -LiteralPath (Join-Path $stage 'game'))) 'game/ was extracted for installation.'
    Assert (-not (Test-Path -LiteralPath (Join-Path $stage 'assets'))) 'assets/ was extracted for installation.'
    Assert-Throws { Expand-Update $validZip $stage $name 'v0.5.0' } 'Wrong package version accepted.'

    $install = Join-Path $testRoot 'success'
    Make-Install $install
    Make-Stage $install
    Install-Update $install $name
    Assert ([IO.File]::ReadAllText((Join-Path $install "$name.exe")) -eq 'new launcher') 'Launcher not updated.'
    Assert (-not (Test-Path -LiteralPath (Join-Path $install 'sdk/old.txt'))) 'Old SDK was mixed with new SDK.'
    foreach ($path in @('game/settings.toml', 'game/hdd/save.dat', 'game/textures/custom.dds',
        'game/GenmaOnimusha.exe', 'assets/game_path.txt', 'assets/own.iso', 'work/default.xbe')) {
        Assert ([IO.File]::ReadAllText((Join-Path $install $path)) -eq 'user data') "User data changed: $path"
    }

    $install = Join-Path $testRoot 'rollback'
    Make-Install $install
    Make-Stage $install
    $locked = [IO.File]::Open((Join-Path $install "$name.exe"), [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::None)
    try {
        $failed = $false
        try { Install-Update $install $name } catch { $failed = $true; Write-Output ('Rollback fixture: ' + $_.Exception.Message) }
        Assert $failed 'Locked launcher update unexpectedly succeeded.'
    } finally { $locked.Dispose() }
    Assert ([IO.File]::ReadAllText((Join-Path $install 'sdk/old.txt')) -eq 'old sdk') 'SDK was not restored after failure.'
    Assert ([IO.File]::ReadAllText((Join-Path $install 'toolchain/old.txt')) -eq 'old compiler') 'Compiler was not restored after failure.'
    Assert ([IO.File]::ReadAllText((Join-Path $install "$name.exe")) -eq 'old launcher') 'Original launcher was lost.'

    $install = Join-Path $testRoot 'interrupted'
    Make-Install $install
    [IO.Directory]::CreateDirectory((Join-Path $install '.update/backup')) | Out-Null
    Save-Journal (Join-Path $install '.update') @([pscustomobject]@{name='sdk'; hadOriginal=$true})
    Move-Item -LiteralPath (Join-Path $install 'sdk') -Destination (Join-Path $install '.update/backup/sdk')
    Put (Join-Path $install 'sdk/partial.txt') 'partial install'
    Restore-Update $install $name
    Restore-Update $install $name
    Assert ([IO.File]::ReadAllText((Join-Path $install 'sdk/old.txt')) -eq 'old sdk') 'Interrupted update was not recovered.'
    Assert (-not (Test-Path -LiteralPath (Join-Path $install 'sdk/partial.txt'))) 'Partial SDK survived recovery.'

    # Run the actual detached worker and verify it relaunches the installed executable.
    $fixtureExe = Join-Path $testRoot 'fixture.exe'
    Add-Type -OutputAssembly $fixtureExe -OutputType ConsoleApplication -TypeDefinition @'
using System;
using System.IO;
public class UpdaterFixture {
    public static void Main(string[] args) {
        File.WriteAllLines(Path.Combine(Environment.CurrentDirectory, ".update", "restarted.txt"), args);
    }
}
'@
    $install = Join-Path $testRoot 'handoff'
    Make-Install $install
    Make-Stage $install
    Copy-Item -LiteralPath $fixtureExe -Destination (Join-Path $install ".update/stage/$name.exe") -Force
    Put (Join-Path $install '.update/restart.txt') ' "argument with spaces" "C:\path with spaces\"'
    $parent = Start-Process -FilePath "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" -ArgumentList '-NoProfile -Command "Start-Sleep -Seconds 2"' -WindowStyle Hidden -PassThru
    $workerScript = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../sdk/launcher/update.ps1'))
    & "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $workerScript -Mode Apply -Root $install -Version v0.3.1 -Repository $repo -Exe $name -ParentProcessId $parent.Id
    Assert ($LASTEXITCODE -eq 0) 'Detached install failed.'
    $restarted = Join-Path $install '.update/restarted.txt'
    for ($i = 0; $i -lt 30 -and -not [IO.File]::Exists($restarted); $i++) { Start-Sleep -Milliseconds 100 }
    $arguments = [IO.File]::ReadAllLines($restarted)
    Assert ($arguments[0] -eq '--skip-update-once' -and $arguments[1] -eq 'argument with spaces') 'Restart arguments were lost.'

    # Recover also runs out of process: its launcher must exit before restoring its own executable.
    $install = Join-Path $testRoot 'recovery-handoff'
    Make-Install $install
    [IO.Directory]::CreateDirectory((Join-Path $install '.update/backup')) | Out-Null
    Copy-Item -LiteralPath $fixtureExe -Destination (Join-Path $install "$name.exe") -Force
    Save-Journal (Join-Path $install '.update') @([pscustomobject]@{name="$name.exe"; hadOriginal=$true})
    Move-Item -LiteralPath (Join-Path $install "$name.exe") -Destination (Join-Path $install ".update/backup/$name.exe")
    Put (Join-Path $install "$name.exe") 'interrupted launcher'
    & "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $workerScript -Mode Recover -Root $install -Version v0.3.1 -Repository $repo -Exe $name -ParentProcessId $parent.Id
    Assert ($LASTEXITCODE -eq 0) 'Detached recovery failed.'
    $restarted = Join-Path $install '.update/restarted.txt'
    for ($i = 0; $i -lt 30 -and -not [IO.File]::Exists($restarted); $i++) { Start-Sleep -Milliseconds 100 }
    Assert ([IO.File]::Exists($restarted)) 'Recovered launcher did not restart.'

    if ($Launcher) {
        $install = Join-Path $testRoot 'native launcher with spaces'
        Make-Install $install
        Copy-Item -LiteralPath $Launcher -Destination (Join-Path $install "$name.exe") -Force
        Copy-Item -LiteralPath $fixtureExe -Destination (Join-Path $install "game/$name.exe") -Force
        [IO.Directory]::CreateDirectory((Join-Path $install 'game/.update')) | Out-Null
        Put (Join-Path $install 'game/build.txt') "$FixtureVersion 08bfe432eb55877b5af2dd401b713743c5b751cd"
        $start = [Diagnostics.ProcessStartInfo]::new()
        $start.FileName = Join-Path $install "$name.exe"
        $start.WorkingDirectory = $testRoot
        $start.UseShellExecute = $false
        $start.Arguments = '--no-update "argument with spaces" "C:\trailing slash\\"'
        $native = [Diagnostics.Process]::Start($start)
        Assert ($native.WaitForExit(15000)) 'Native launcher did not exit.'
        Assert ($native.ExitCode -eq 0) 'Native launcher failed.'
        $restarted = Join-Path $install 'game/.update/restarted.txt'
        for ($i = 0; $i -lt 30 -and -not [IO.File]::Exists($restarted); $i++) { Start-Sleep -Milliseconds 100 }
        $arguments = [IO.File]::ReadAllLines($restarted)
        Assert ($arguments.Count -eq 2 -and $arguments[0] -eq 'argument with spaces' -and $arguments[1] -eq 'C:\trailing slash\') 'Native launcher lost arguments or forwarded updater flags.'
        Write-Output 'PASS: compiled launcher, existing build stamp, paths with spaces, trailing backslash and updater flag filtering'
        # Model offline/API failure and a stalled check without using the network.
        foreach ($checkBody in @('exit 1', 'Start-Sleep -Seconds 60')) {
            Remove-Item -LiteralPath $restarted -Force
            Put (Join-Path $install 'sdk/update.ps1') $checkBody
            $start.Arguments = '"offline fallback"'
            $timer = [Diagnostics.Stopwatch]::StartNew()
            $native = [Diagnostics.Process]::Start($start)
            Assert ($native.WaitForExit(15000)) 'Failed update check blocked the installed game.'
            Assert ($native.ExitCode -eq 0 -and $timer.Elapsed.TotalSeconds -lt 12) 'Update fallback exceeded its deadline.'
            for ($i = 0; $i -lt 30 -and -not [IO.File]::Exists($restarted); $i++) { Start-Sleep -Milliseconds 100 }
            Assert ([IO.File]::ReadAllText($restarted).Trim() -eq 'offline fallback') 'Installed game did not start after a failed check.'
        }
        Write-Output 'PASS: offline check and stalled check both fall back to the installed game'

        Remove-Item -LiteralPath $restarted -Force
        [IO.Directory]::CreateDirectory((Join-Path $install '.update/backup')) | Out-Null
        Copy-Item -LiteralPath $Launcher -Destination (Join-Path $install ".update/backup/$name.exe") -Force
        Copy-Item -LiteralPath $workerScript -Destination (Join-Path $install '.update/worker.ps1') -Force
        Put (Join-Path $install '.update/restart.txt') ' "recovered argument"'
        Save-Journal (Join-Path $install '.update') @([pscustomobject]@{name="$name.exe"; hadOriginal=$true})
        $start.Arguments = '--no-update'
        $native = [Diagnostics.Process]::Start($start)
        Assert ($native.WaitForExit(15000)) 'Native recovery did not release the launcher.'
        Assert ($native.ExitCode -eq 0) 'Native recovery handoff failed.'
        for ($i = 0; $i -lt 100 -and -not [IO.File]::Exists($restarted); $i++) { Start-Sleep -Milliseconds 100 }
        Assert ([IO.File]::ReadAllText($restarted).Trim() -eq 'recovered argument') 'Native recovery failed to replace and restart its own executable.'
        Write-Output 'PASS: compiled launcher recovery handshake, executable replacement and restart'
    }
    Write-Output 'PASS: stable versions/assets, safe ZIP paths, package validation, user data preservation, rollback and interrupted-update recovery'
    Write-Output 'PASS: checksum/size validation, detached installation/recovery and relaunch arguments'
} finally {
    if ($native -and -not $native.HasExited) { $native.Kill(); $native.WaitForExit() }
    # The absolute GUID fixture path was created above inside this workspace's out/.
    if ($testRoot -and [IO.Directory]::Exists($testRoot)) {
        for ($cleanupAttempt = 0; $cleanupAttempt -lt 10; $cleanupAttempt++) {
            try { Remove-Item -LiteralPath $testRoot -Recurse -Force; break }
            catch { if ($cleanupAttempt -eq 9) { throw }; Start-Sleep -Milliseconds 100 }
        }
    }
}
