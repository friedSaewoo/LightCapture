param([string]$EvidenceDirectory='',[string]$ExecutableName='LightCapture.exe')
$ErrorActionPreference='Stop'
$projectRoot=$PSScriptRoot
if(-not $EvidenceDirectory){$EvidenceDirectory=Join-Path $projectRoot ('verification\run-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))}
New-Item -ItemType Directory -Path $EvidenceDirectory -Force | Out-Null
$EvidenceDirectory=(Resolve-Path -LiteralPath $EvidenceDirectory).Path
$exe=Join-Path $projectRoot $ExecutableName
$testExe=Join-Path $projectRoot 'build\LightCaptureTests.exe'
if(-not (Test-Path -LiteralPath $testExe)){throw 'build.ps1을 먼저 실행해야 합니다.'}
& $testExe (Join-Path $EvidenceDirectory 'core-data') | Set-Content -LiteralPath (Join-Path $EvidenceDirectory 'core-tests.txt') -Encoding utf8
if($LASTEXITCODE -ne 0){throw '핵심 자동 시험 실패'}
& (Join-Path $projectRoot 'build\OverlayTests.exe') (Join-Path $EvidenceDirectory 'overlay-data') | Set-Content -LiteralPath (Join-Path $EvidenceDirectory 'overlay-tests.txt') -Encoding utf8
if($LASTEXITCODE -ne 0){throw '영역 선택 렌더링 시험 실패'}
$verifyData=Join-Path $EvidenceDirectory 'app-data'
$process=Start-Process -FilePath $exe -ArgumentList @('--verify','--data',('"'+$verifyData+'"')) -WindowStyle Hidden -PassThru
if(-not $process.WaitForExit(30000)){throw '앱 내부 검증이 제한 시간에 끝나지 않았습니다. 실행 중인 별도 검증 프로세스를 확인하세요.'}
if($process.ExitCode -ne 0){throw ('앱 내부 검증 실패: '+$process.ExitCode)}
$internalResult=Join-Path $verifyData 'app-verification.json'
if(-not (Test-Path -LiteralPath $internalResult)){throw '앱 내부 검증 결과 파일이 생성되지 않았습니다.'}
$smokeData=Join-Path $EvidenceDirectory 'resident-data'
$process=Start-Process -FilePath $exe -ArgumentList @('--smoke','--data',('"'+$smokeData+'"')) -WindowStyle Hidden -PassThru
$samples=@()
$clock=[System.Diagnostics.Stopwatch]::StartNew()
for($index=0;$index -lt 12;$index++){
 Start-Sleep -Milliseconds 750
 $process.Refresh()
 if($process.HasExited){break}
 $samples += [pscustomobject]@{ElapsedMilliseconds=[math]::Round($clock.Elapsed.TotalMilliseconds,2);WorkingSetMiB=[math]::Round($process.WorkingSet64/1MB,3);PrivateCommitMiB=[math]::Round($process.PrivateMemorySize64/1MB,3);CPUSeconds=$process.TotalProcessorTime.TotalSeconds;Handles=$process.HandleCount}
}
if(-not $process.WaitForExit(12000)){throw '상주 시험 종료를 확인하지 못했습니다.'}
if($process.ExitCode -ne 0){throw '상주 시험 실패'}
$samples | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $EvidenceDirectory 'resident-smoke.json') -Encoding utf8
$os=Get-CimInstance Win32_OperatingSystem
$system=Get-CimInstance Win32_ComputerSystem
$metadata=[pscustomobject]@{CapturedAt=(Get-Date).ToString('o');ExecutableSHA256=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash;ExecutableBytes=(Get-Item -LiteralPath $exe).Length;Windows=$os.Caption;WindowsBuild=$os.BuildNumber;LogicalProcessors=$system.NumberOfLogicalProcessors;RAMGiB=[math]::Round($system.TotalPhysicalMemory/1GB,2);DesktopCapturePerformed=$false;ExternalInputInjected=$false;ComputerUseUsed=$false;Limitations='Short resident smoke only; no ALCapture equivalence or latency acceptance.'}
$metadata | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $EvidenceDirectory 'environment.json') -Encoding utf8
Get-Content -LiteralPath (Join-Path $EvidenceDirectory 'core-tests.txt') -Tail 1
Get-Content -LiteralPath $internalResult
$samples | Measure-Object -Property WorkingSetMiB -Minimum -Maximum
Write-Output ('검증 자료: '+$EvidenceDirectory)
