# builds, runs the game for a while and prints the end of its log
param([int]$Seconds = 60, [switch]$NoBuild, [switch]$Verbose, [int]$Tail = 60, [string]$Capture = '')
$root = $PSScriptRoot
$out = Join-Path $root 'out\build\release'
if (-not $NoBuild) {
    cmd /c "$root\dev.bat cmake --build out\build\release" 2>&1 | Select-String -Pattern ' error|FAILED|Linking' | Select-Object -Last 10
}
if ($Verbose) { $env:XBE_VERBOSE = '1' } else { Remove-Item Env:XBE_VERBOSE -ErrorAction SilentlyContinue }
if ($Capture) { $env:XBE_CAPTURE_DIR = 'captures'; $env:XBE_CAPTURE_FRAMES = $Capture } else { Remove-Item Env:XBE_CAPTURE_FRAMES -ErrorAction SilentlyContinue }
$exe = Join-Path $out 'GenmaOnimusha.exe'
$p = Start-Process -FilePath $exe -WorkingDirectory $out -PassThru
if (-not $p.WaitForExit($Seconds * 1000)) {
    # a closed window ends the game normally and the log is flushed
    $null = $p.CloseMainWindow()
    if (-not $p.WaitForExit(5000)) { Stop-Process -Id $p.Id -Force }
    "-- still running after $Seconds s, closed"
} else { "-- exit code $($p.ExitCode)" }
$log = Get-ChildItem (Join-Path $out 'logs') -Filter 'genma*.log' | Sort-Object LastWriteTime | Select-Object -Last 1
"-- log $($log.Name)"
Get-Content $log.FullName | Select-Object -Skip 16 | Select-Object -Last $Tail
