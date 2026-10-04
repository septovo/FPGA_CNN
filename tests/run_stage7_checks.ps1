param()
$root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
& (Join-Path $root 'tests/build_stage7.ps1')
if($LASTEXITCODE -ne 0){throw 'runtime stats check failed'}
& (Join-Path $root 'tests/build_preprocess_host.ps1')
if($LASTEXITCODE -ne 0){throw 'preprocess host build failed'}
& (Join-Path $root 'tests/build_stage5.ps1')
if($LASTEXITCODE -ne 0){throw 'CNN regression failed'}
& (Join-Path $root 'tests/.venv/Scripts/python.exe') (Join-Path $root 'tests/test_stage7_reports.py')
if($LASTEXITCODE -ne 0){throw 'report analyzer test failed'}
& (Join-Path $root 'tests/build_stage7_arm.ps1')
if($LASTEXITCODE -ne 0){throw 'ARM build failed'}
Write-Output 'STAGE7_HOST_CHECKS_PASS'
