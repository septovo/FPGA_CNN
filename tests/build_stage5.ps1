param()
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$src = Join-Path $root 'vitis/dual_ov5640_lcd/src'
$out = Join-Path $root 'tests/build_stage5'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$gccDir = 'E:/xilinx/Vivado/2020.2/tps/win64/msys64/mingw64/bin'
$env:PATH = "$gccDir;$env:PATH"
$gcc = Join-Path $gccDir 'gcc.exe'
& $gcc -O2 -std=c99 -Wall -Wextra -I $src (Join-Path $src 'digit_model.c') (Join-Path $src 'digit_weights.c') (Join-Path $root 'tests/digit_model_host.c') -lm -o (Join-Path $out 'digit_model_host.exe')
if ($LASTEXITCODE -ne 0) { throw 'Host model build failed' }
& (Join-Path $root 'tests/.venv/Scripts/python.exe') (Join-Path $root 'tests/test_digit_model.py')
if ($LASTEXITCODE -ne 0) { throw 'Model golden comparison failed' }
