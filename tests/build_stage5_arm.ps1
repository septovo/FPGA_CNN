param()
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$sourceRoot = Join-Path $projectRoot 'vitis/dual_ov5640_lcd/src'
$bspRoot = Join-Path $projectRoot 'vitis/system_wrapper/ps7_cortexa9_0/standalone_ps7_cortexa9_0/bsp/ps7_cortexa9_0'
$outputRoot = Join-Path $projectRoot 'tests/build_stage5'
$compiler = 'E:/xilinx/Vitis/2020.2/gnu/aarch32/nt/gcc-arm-none-eabi/bin/arm-none-eabi-gcc.exe'
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null
$objects = @()
foreach ($source in Get-ChildItem -LiteralPath $sourceRoot -Recurse -Filter '*.c') {
    $object = Join-Path $outputRoot ($source.BaseName + '.o')
    & $compiler -O2 -g -std=c99 -mcpu=cortex-a9 -mfpu=vfpv3 -mfloat-abi=hard -Wall -Wextra -I (Join-Path $bspRoot 'include') -I $sourceRoot -c $source.FullName -o $object
    if ($LASTEXITCODE -ne 0) { throw "Compile failed: $($source.FullName)" }
    $objects += $object
}
$elf = Join-Path $outputRoot 'dual_ov5640_lcd.elf'
$spec = Join-Path $sourceRoot 'Xilinx.spec'
$linkerScript = Join-Path $sourceRoot 'lscript.ld'
$bspLib = Join-Path $bspRoot 'lib'
& $compiler '-mcpu=cortex-a9' '-mfpu=vfpv3' '-mfloat-abi=hard' "-specs=$spec" '-Wl,-build-id=none' "-Wl,-T,$linkerScript" "-L$bspLib" -o $elf @objects '-Wl,--start-group' -lxil -lm -lgcc -lc '-Wl,--end-group'
if ($LASTEXITCODE -ne 0) { throw 'Link failed' }
Write-Output "STAGE5_ELF=$elf"

