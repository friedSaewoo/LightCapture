param([string]$Toolchain = '',[switch]$DebugBuild,[string]$OutputName = 'LightCapture.exe')
$ErrorActionPreference='Stop'
$projectRoot=$PSScriptRoot
if([IO.Path]::GetFileName($OutputName) -ne $OutputName -or -not $OutputName.EndsWith('.exe')){throw 'OutputName에는 exe 파일명만 지정하세요.'}
if(-not $Toolchain){
 if($env:LIGHTCAPTURE_TOOLCHAIN){$Toolchain=$env:LIGHTCAPTURE_TOOLCHAIN}
 else{$compilerCommand=Get-Command 'g++.exe' -ErrorAction SilentlyContinue; if($compilerCommand){$Toolchain=Split-Path -Parent $compilerCommand.Source}}
}
if(-not $Toolchain){throw 'w64devkit의 bin 경로를 -Toolchain으로 지정하거나 LIGHTCAPTURE_TOOLCHAIN 환경변수에 설정하세요.'}
$compiler=Join-Path $Toolchain 'g++.exe'
if(-not (Test-Path -LiteralPath $compiler)){throw 'w64devkit의 bin 경로를 -Toolchain으로 지정하세요.'}
$buildDir=Join-Path $projectRoot 'build'
$oldPath=$env:PATH
$env:PATH="$Toolchain;$oldPath"
New-Item -ItemType Directory -Path $buildDir -Force | Out-Null
Push-Location (Join-Path $projectRoot 'src')
try{
 & (Join-Path $Toolchain 'windres.exe') '--codepage=65001' 'app.rc' '-o' (Join-Path $buildDir 'app.o')
 if($LASTEXITCODE -ne 0){throw '리소스 빌드 실패'}
 $optimization=if($DebugBuild){'-O0'}else{'-O2'}
 & $compiler '-std=c++17' $optimization '-Wall' '-Wextra' '-Wno-missing-field-initializers' '-municode' '-mwindows' '-static' '-static-libgcc' '-static-libstdc++' 'app.cpp' 'theme.cpp' 'clipboard.cpp' 'pixel-select.cpp' 'canvas-edit.cpp' 'verify-canvas.cpp' 'benchmark.cpp' 'editor.cpp' 'toolbar.cpp' 'history.cpp' 'inline-text.cpp' 'quick-bar.cpp' 'capture.cpp' 'overlay.cpp' 'store.cpp' 'verify.cpp' 'verify-editor.cpp' 'verify-history.cpp' 'verify-pixel.cpp' (Join-Path $buildDir 'app.o') '-o' (Join-Path $projectRoot $OutputName) '-lgdiplus' '-lcomctl32' '-lcomdlg32' '-lshell32' '-lole32' '-luuid' '-ldwmapi' '-lpsapi' '-lshlwapi' '-limm32'
 if($LASTEXITCODE -ne 0){throw '앱 빌드 실패'}
 if(-not $DebugBuild){ & (Join-Path $Toolchain 'strip.exe') '--strip-all' (Join-Path $projectRoot $OutputName); if($LASTEXITCODE -ne 0){throw '실행 파일 정리 실패'} }
 & $compiler '-std=c++17' '-O2' '-municode' '-static' 'tests.cpp' 'store.cpp' '-o' (Join-Path $buildDir 'LightCaptureTests.exe') '-lgdi32' '-lgdiplus' '-lcomctl32' '-lcomdlg32' '-lshell32' '-lole32' '-luuid' '-ldwmapi' '-lpsapi'
 if($LASTEXITCODE -ne 0){throw '테스트 빌드 실패'}
 & $compiler '-std=c++17' '-O2' '-municode' '-static' 'overlay-tests.cpp' 'overlay.cpp' 'store.cpp' '-o' (Join-Path $buildDir 'OverlayTests.exe') '-lgdi32' '-lgdiplus' '-lcomctl32' '-lcomdlg32' '-lshell32' '-lole32' '-luuid' '-ldwmapi' '-lpsapi'
 if($LASTEXITCODE -ne 0){throw '영역 선택 렌더링 테스트 빌드 실패'}
 & $compiler '-std=c++17' '-O2' '-municode' '-static' 'clipboard-tests.cpp' 'clipboard.cpp' '-o' (Join-Path $buildDir 'ClipboardTests.exe') '-lgdi32' '-lgdiplus' '-lcomctl32' '-lcomdlg32' '-lshell32' '-lole32' '-luuid' '-ldwmapi' '-lpsapi'
 if($LASTEXITCODE -ne 0){throw '클립보드 시험 빌드 실패'}
}finally{Pop-Location;$env:PATH=$oldPath}
