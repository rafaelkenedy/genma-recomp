# Windows PowerShell 5.1. The launcher only runs this bundled worker, never a downloaded script.
param(
    [ValidateSet('Check', 'Prepare', 'Apply', 'Recover')][string]$Mode,
    [string]$Root, [string]$Version, [string]$Repository, [string]$Exe,
    [int]$ParentProcessId = 0
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Write-Utf8([string]$Path, [string]$Text) {
    [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false))
}

function Assert-PlainTree([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return }
    $item = Get-Item -LiteralPath $Path -Force
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Linked path is not supported: $Path" }
    if ($item.PSIsContainer) {
        foreach ($child in [IO.Directory]::GetFileSystemEntries($Path)) { Assert-PlainTree $child }
    }
}

function Get-UpdateVersion([string]$Tag) {
    if ($Tag -cnotmatch '^v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') { return $null }
    try { return [version]$Tag.Substring(1) } catch { return $null }
}

function Select-Update($Release, [string]$Current, [string]$Repo, [string]$Name) {
    $installed = Get-UpdateVersion $Current
    $latest = Get-UpdateVersion ([string]$Release.tag_name)
    if (-not $installed -or -not $latest -or $latest -le $installed -or $Release.draft -or $Release.prerelease) { return $null }
    $assetName = "$Name-$($Release.tag_name)-win64.zip"
    $assets = @($Release.assets | Where-Object { $_.name -ceq $assetName -and $_.state -eq 'uploaded' })
    if ($assets.Count -ne 1) { return $null }
    $asset = $assets[0]
    if ($asset.digest -cnotmatch '^sha256:[0-9a-f]{64}$' -or $asset.size -le 0 -or $asset.size -gt 1GB) { return $null }
    $url = "https://github.com/$Repo/releases/download/$($Release.tag_name)/$assetName"
    if ($asset.browser_download_url -cne $url) { return $null }
    return [pscustomobject]@{ version = $Release.tag_name; url = $url; sha256 = $asset.digest.Substring(7); size = [long]$asset.size }
}

function New-HttpClient([int]$Seconds) {
    Add-Type -AssemblyName System.Net.Http
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    $client = [Net.Http.HttpClient]::new()
    $client.Timeout = [TimeSpan]::FromSeconds($Seconds)
    $client.DefaultRequestHeaders.UserAgent.ParseAdd('GenmaOnimusha-updater/1')
    $client.DefaultRequestHeaders.Accept.ParseAdd('application/vnd.github+json')
    return $client
}

function Assert-UpdateHash([string]$Path, [string]$Digest, [long]$Size) {
    if ($Digest -cnotmatch '^[0-9a-f]{64}$' -or ([IO.FileInfo]$Path).Length -ne $Size -or
        (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() -cne $Digest) {
        throw 'Update SHA-256 or size mismatch.'
    }
}

function Get-EntryPath([string]$Name, [string]$Package) {
    $name = $Name.Replace('\', '/')
    $parts = $name.TrimEnd('/').Split('/')
    if ($parts.Count -lt 1 -or $parts[0] -cne $Package) { throw 'Unexpected archive root.' }
    foreach ($part in $parts) {
        if (-not $part -or $part -eq '.' -or $part -eq '..' -or $part -match '[:<>"|?*\x00-\x1f]' -or
            $part.EndsWith('.') -or $part.EndsWith(' ') -or $part -match '^(?i:CON|PRN|AUX|NUL|COM[0-9]|LPT[0-9])(?:\.|$)') {
            throw "Unsafe archive path: $Name"
        }
    }
    return ($parts -join '/')
}

function Expand-Update([string]$Zip, [string]$Stage, [string]$Name, [string]$Tag) {
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    Assert-PlainTree $Stage
    if (Test-Path -LiteralPath $Stage) { Remove-Item -LiteralPath $Stage -Recurse -Force }
    [IO.Directory]::CreateDirectory($Stage) | Out-Null
    $archive = [IO.Compression.ZipFile]::OpenRead($Zip)
    try {
        $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        [long]$expanded = 0
        if ($archive.Entries.Count -gt 50000) { throw 'Too many archive entries.' }
        foreach ($entry in $archive.Entries) {
            $relative = Get-EntryPath $entry.FullName $Name
            if (-not $seen.Add($relative)) { throw 'Duplicate archive path.' }
            if (($entry.ExternalAttributes -band 0x400) -or (($entry.ExternalAttributes -shr 16) -band 0xf000) -eq 0xa000) {
                throw 'Linked archive entry.'
            }
            $expanded += $entry.Length
            if ($entry.Length -gt 2GB -or $expanded -gt 4GB) { throw 'Expanded update is too large.' }
        }
        foreach ($entry in $archive.Entries) {
            $relative = Get-EntryPath $entry.FullName $Name
            $tail = $relative.Substring($Name.Length).TrimStart('/')
            # Only managed application components are installed. game/, assets/ and work/ belong to the user.
            if ($tail -notmatch '^(sdk/|toolchain/)' -and $tail -cne "$Name.exe" -and $tail -cne 'README.md') { continue }
            $destination = Join-Path $Stage $tail
            if ($entry.FullName.EndsWith('/') -or $entry.FullName.EndsWith('\')) {
                [IO.Directory]::CreateDirectory($destination) | Out-Null
                continue
            }
            [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination)) | Out-Null
            $inputStream = $entry.Open()
            $outputStream = [IO.File]::Open($destination, [IO.FileMode]::CreateNew)
            try {
                $buffer = [byte[]]::new(131072)
                [long]$copied = 0
                while (($count = $inputStream.Read($buffer, 0, $buffer.Length)) -gt 0) {
                    $copied += $count
                    if ($copied -gt $entry.Length) { throw 'Expanded entry exceeds its declared size.' }
                    $outputStream.Write($buffer, 0, $count)
                }
                if ($copied -ne $entry.Length) { throw 'Incomplete archive entry.' }
            } finally { $inputStream.Dispose(); $outputStream.Dispose() }
        }
    } finally { $archive.Dispose() }
    foreach ($required in @("$Name.exe", 'sdk/bin/xbe_recomp.exe', 'sdk/include/image_symbols.h',
        "sdk/lib/lib${Name}_runtime.a", 'sdk/update.ps1', 'toolchain/bin/x86_64-w64-mingw32-clang++.exe')) {
        if (-not [IO.File]::Exists((Join-Path $Stage $required))) { throw "Incomplete update: $required" }
    }
    $marker = Join-Path $Stage 'sdk/update-version.txt'
    if (-not [IO.File]::Exists($marker) -or [IO.File]::ReadAllText($marker).Trim() -cne $Tag) { throw 'Update version mismatch.' }
}

function Save-Journal([string]$Directory, $Journal) {
    $path = Join-Path $Directory 'transaction.json'
    $temp = Join-Path $Directory 'transaction.tmp'
    $bytes = [Text.UTF8Encoding]::new($false).GetBytes((ConvertTo-Json -InputObject @($Journal) -Depth 4))
    $stream = [IO.File]::Open($temp, [IO.FileMode]::Create)
    try { $stream.Write($bytes, 0, $bytes.Length); $stream.Flush($true) } finally { $stream.Dispose() }
    if ([IO.File]::Exists($path)) { [IO.File]::Replace($temp, $path, [NullString]::Value) }
    else { [IO.File]::Move($temp, $path) }
}

function Restore-Update([string]$InstallRoot, [string]$Name) {
    $directory = Join-Path $InstallRoot '.update'
    $path = Join-Path $directory 'transaction.json'
    if (-not [IO.File]::Exists($path)) { return }
    $journal = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
    $journal = @($journal)
    [array]::Reverse($journal)
    foreach ($item in $journal) {
        if ($item.name -cnotin @('sdk', 'toolchain', "$Name.exe", 'README.md')) { throw "Invalid update journal entry: $($item.name) ($Name)." }
        $destination = Join-Path $InstallRoot $item.name
        $backup = Join-Path $directory "backup/$($item.name)"
        Assert-PlainTree $destination
        Assert-PlainTree $backup
        if (Test-Path -LiteralPath $backup) {
            if ([IO.File]::Exists($backup) -and [IO.File]::Exists($destination)) {
                [IO.File]::Replace($backup, $destination, [NullString]::Value)
            } else {
                if (Test-Path -LiteralPath $destination) { Remove-Item -LiteralPath $destination -Recurse -Force }
                Move-Item -LiteralPath $backup -Destination $destination
            }
        } elseif (-not $item.hadOriginal -and (Test-Path -LiteralPath $destination)) {
            Remove-Item -LiteralPath $destination -Recurse -Force
        }
    }
    Remove-Item -LiteralPath $path -Force
}

function Install-Update([string]$InstallRoot, [string]$Name) {
    $directory = Join-Path $InstallRoot '.update'
    $stage = Join-Path $directory 'stage'
    $backup = Join-Path $directory 'backup'
    if (-not [IO.File]::Exists((Join-Path $InstallRoot "$Name.exe"))) { throw 'Installed launcher is missing.' }
    if (Test-Path -LiteralPath (Join-Path $directory 'transaction.json')) { throw 'An interrupted update needs recovery.' }
    Assert-PlainTree $backup
    Assert-PlainTree $stage
    if (Test-Path -LiteralPath $backup) { Remove-Item -LiteralPath $backup -Recurse -Force }
    [IO.Directory]::CreateDirectory($backup) | Out-Null
    $journal = @()
    try {
        foreach ($component in @('sdk', 'toolchain', 'README.md', "$Name.exe")) {
            $source = Join-Path $stage $component
            if (-not (Test-Path -LiteralPath $source)) { if ($component -eq 'README.md') { continue }; throw "Missing $component" }
            $destination = Join-Path $InstallRoot $component
            Assert-PlainTree $destination
            $journal += [pscustomobject]@{ name = $component; hadOriginal = [bool](Test-Path -LiteralPath $destination) }
            Save-Journal $directory $journal
            if ([IO.File]::Exists($source) -and [IO.File]::Exists($destination)) {
                # Replace files atomically, so a crash never leaves the launcher absent.
                [IO.File]::Replace($source, $destination, (Join-Path $backup $component))
            } else {
                if (Test-Path -LiteralPath $destination) { Move-Item -LiteralPath $destination -Destination (Join-Path $backup $component) }
                Move-Item -LiteralPath $source -Destination $destination
            }
        }
        Remove-Item -LiteralPath (Join-Path $directory 'transaction.json') -Force
    } catch {
        $reason = $_
        Restore-Update $InstallRoot $Name
        throw $reason
    }
}

# Dot-sourcing loads the pure functions for fixture tests; no network or installation starts.
if ($MyInvocation.InvocationName -eq '.') { return }
$directory = $null
$installLock = $null
try {
    if ($Repository -notmatch '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$' -or $Exe -notmatch '^[A-Za-z0-9_-]+$') { throw 'Invalid updater configuration.' }
    $Root = [IO.Path]::GetFullPath($Root).TrimEnd('\')
    if (-not [IO.Directory]::Exists($Root)) { throw 'Missing installation directory.' }
    # Check every ancestor before touching staging or installation paths.
    $ancestor = $Root
    while ($ancestor) {
        $item = Get-Item -LiteralPath $ancestor -Force
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Linked installation directory.' }
        $ancestor = [IO.Path]::GetDirectoryName($ancestor)
    }
    $directory = Join-Path $Root '.update'
    Assert-PlainTree $directory
    [IO.Directory]::CreateDirectory($directory) | Out-Null
    $planPath = Join-Path $directory 'plan.json'
    $statusPath = Join-Path $directory 'status.txt'
    if ($Mode -in @('Apply', 'Recover')) {
        if ($ParentProcessId -le 0) { throw 'Missing launcher process.' }
        Write-Utf8 (Join-Path $directory 'ready.txt') ([string]$ParentProcessId)
        $parent = Get-Process -Id $ParentProcessId -ErrorAction SilentlyContinue
        if ($parent -and -not $parent.WaitForExit(30000)) { throw 'The launcher did not close.' }
        $installLock = [IO.File]::Open((Join-Path $directory 'lock'), [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
        $game = Join-Path $Root "game/$Exe.exe"
        if ([IO.File]::Exists($game)) {
            $gameLock = [IO.File]::Open($game, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
            $gameLock.Dispose()
        }
    }
    switch ($Mode) {
        Check {
            Write-Utf8 (Join-Path $directory 'available.txt') ''
            if (-not (Get-UpdateVersion $Version)) { exit 0 }
            $client = New-HttpClient 4
            try {
                $json = $client.GetStringAsync("https://api.github.com/repos/$Repository/releases/latest").GetAwaiter().GetResult()
                $plan = Select-Update ($json | ConvertFrom-Json) $Version $Repository $Exe
            } finally { $client.Dispose() }
            if ($null -ne $plan) {
                Write-Utf8 $planPath ($plan | ConvertTo-Json)
                Write-Utf8 (Join-Path $directory 'available.txt') $plan.version
            }
        }
        Prepare {
            $plan = Get-Content -LiteralPath $planPath -Raw | ConvertFrom-Json
            if (-not (Get-UpdateVersion $plan.version) -or $plan.sha256 -cnotmatch '^[0-9a-f]{64}$' -or
                $plan.url -cne "https://github.com/$Repository/releases/download/$($plan.version)/$Exe-$($plan.version)-win64.zip" -or
                $plan.size -le 0 -or $plan.size -gt 1GB) { throw 'Invalid update plan.' }
            $zip = Join-Path $directory 'package.zip'
            $client = New-HttpClient 600
            try {
                $response = $client.GetAsync($plan.url, [Net.Http.HttpCompletionOption]::ResponseHeadersRead).GetAwaiter().GetResult()
                try {
                    $response.EnsureSuccessStatusCode() | Out-Null
                    $inputStream = $response.Content.ReadAsStreamAsync().GetAwaiter().GetResult()
                    $outputStream = [IO.File]::Open($zip, [IO.FileMode]::Create)
                    try {
                        $buffer = [byte[]]::new(131072)
                        [long]$received = 0
                        $lastStatus = [datetime]::MinValue
                        while (($n = $inputStream.Read($buffer, 0, $buffer.Length)) -gt 0) {
                            $received += $n
                            if ($received -gt $plan.size) { throw 'Download exceeds the declared size.' }
                            $outputStream.Write($buffer, 0, $n)
                            if (([datetime]::UtcNow - $lastStatus).TotalMilliseconds -gt 500) {
                                Write-Utf8 $statusPath ("Downloading update: {0:N0} of {1:N0} MB..." -f ($received / 1MB), ($plan.size / 1MB))
                                $lastStatus = [datetime]::UtcNow
                            }
                        }
                        if ($received -ne $plan.size) { throw 'Incomplete download.' }
                    } finally { $inputStream.Dispose(); $outputStream.Dispose() }
                } finally { $response.Dispose() }
            } finally { $client.Dispose() }
            Write-Utf8 $statusPath 'Verifying the update...'
            Assert-UpdateHash $zip $plan.sha256 $plan.size
            Write-Utf8 $statusPath 'Unpacking the update...'
            Expand-Update $zip (Join-Path $directory 'stage') $Exe $plan.version
            Copy-Item -LiteralPath $PSCommandPath -Destination (Join-Path $directory 'worker.ps1') -Force
        }
        Recover { Restore-Update $Root $Exe }
        Apply {
            Install-Update $Root $Exe
        }
        default { throw 'Missing updater mode.' }
    }
    if ($Mode -in @('Apply', 'Recover')) {
        # Installation is committed (or recovered). Cleanup failure must not undo it.
        try {
            foreach ($leftover in @('package.zip', 'backup', 'stage')) {
                $path = Join-Path $directory $leftover
                Assert-PlainTree $path
                if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Recurse -Force }
            }
        } catch { }
        $installLock.Dispose()
        $installLock = $null
        $arguments = ' --skip-update-once'
        $restart = Join-Path $directory 'restart.txt'
        if ([IO.File]::Exists($restart)) { $arguments += [IO.File]::ReadAllText($restart, [Text.Encoding]::UTF8) }
        $start = [Diagnostics.ProcessStartInfo]::new()
        $start.FileName = Join-Path $Root "$Exe.exe"
        $start.WorkingDirectory = $Root
        $start.UseShellExecute = $false
        $start.CreateNoWindow = $true
        $start.Arguments = $arguments
        [Diagnostics.Process]::Start($start) | Out-Null
    }
    exit 0
} catch {
    if ($directory -and [IO.Directory]::Exists($directory)) {
        try { Write-Utf8 (Join-Path $directory 'error.txt') $_.Exception.Message } catch { }
    }
    if ($Mode -in @('Apply', 'Recover')) {
        Add-Type -AssemblyName System.Windows.Forms
        [Windows.Forms.MessageBox]::Show('The update could not be installed. Close other copies of the game and start the launcher again. Details: .update/error.txt', 'Game update') | Out-Null
    }
    exit 1
} finally { if ($installLock) { $installLock.Dispose() } }
