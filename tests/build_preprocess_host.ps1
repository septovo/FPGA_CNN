$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$sourceRoot = Join-Path $projectRoot 'vitis/dual_ov5640_lcd/src'
$outputRoot = Join-Path $projectRoot 'tests/build_stage4'
$mingwBin = 'E:/xilinx/Vivado/2020.2/tps/win64/msys64/mingw64/bin'
$env:PATH = "$mingwBin;$env:PATH"
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null
& (Join-Path $mingwBin 'gcc.exe') -O2 -std=c99 -Wall -Wextra -Werror -I $sourceRoot (Join-Path $PSScriptRoot 'preprocess_cli.c') (Join-Path $sourceRoot 'digit_preprocess.c') -o (Join-Path $outputRoot 'preprocess_cli.exe')
if ($LASTEXITCODE -ne 0) { throw 'Host C compile failed' }
$python = Join-Path $PSScriptRoot '.venv/Scripts/python.exe'
if (-not (Test-Path $python)) { throw 'Create tests/.venv from tests/requirements-pc.txt first' }
& $python (Join-Path $PSScriptRoot 'test_digit_preprocess.py')
if ($LASTEXITCODE -ne 0) { throw 'Preprocess comparison failed' }
