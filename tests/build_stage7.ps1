param()
$root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$src=Join-Path $root 'vitis/dual_ov5640_lcd/src'
$out=Join-Path $root 'tests/build_stage7'
New-Item -ItemType Directory -Force $out | Out-Null
$gccDir='E:/xilinx/Vivado/2020.2/tps/win64/msys64/mingw64/bin'
$env:PATH="$gccDir;$env:PATH"
& (Join-Path $gccDir 'gcc.exe') -O2 -std=c99 -Wall -Wextra -Werror -I $src (Join-Path $src 'runtime_stats.c') (Join-Path $root 'tests/runtime_stats_test.c') -o (Join-Path $out 'runtime_stats_test.exe')
if($LASTEXITCODE -ne 0){throw 'runtime stats build failed'}
& (Join-Path $out 'runtime_stats_test.exe')
if($LASTEXITCODE -ne 0){throw 'runtime stats test failed'}
