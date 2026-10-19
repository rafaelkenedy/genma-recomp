# builds and starts the game in the background with the detailed log and file driven captures on
param([switch]$NoBuild, [string]$ShadowBias = '', [switch]$NoRenderThread)
$root = $PSScriptRoot
$out = Join-Path $root 'out\build\release'
Get-Process GenmaOnimusha -ErrorAction SilentlyContinue | ForEach-Object { $null = $_.CloseMainWindow(); $_.WaitForExit(5000) | Out-Null; if (-not $_.HasExited) { $_.Kill() } }
if (-not $NoBuild) {
    cmd /c "$root\dev.bat cmake --build out\build\release" 2>&1 | Select-String -Pattern ' error|FAILED|Linking' | Select-Object -Last 10
}
$env:XBE_VERBOSE = '1'
$env:XBE_CAPTURE_DIR = 'captures'
if ($ShadowBias) { $env:XBE_SHADOW_BIAS = $ShadowBias } else { Remove-Item Env:XBE_SHADOW_BIAS -ErrorAction SilentlyContinue }
Remove-Item Env:XBE_CAPTURE_FRAMES -ErrorAction SilentlyContinue
if ($NoRenderThread) { $env:XBE_RENDER_THREAD = '0' } else { Remove-Item Env:XBE_RENDER_THREAD -ErrorAction SilentlyContinue }
$p = Start-Process -FilePath (Join-Path $out 'GenmaOnimusha.exe') -WorkingDirectory $out -PassThru
"started pid $($p.Id)"
