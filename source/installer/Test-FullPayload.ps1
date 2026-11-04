$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'Installer.Core.ps1')
$package='E:\RENDER-EVIL\release\RENDER-EVIL-v0.2.0-preview.1-Smart-Installer'
$test=Join-Path $PSScriptRoot ('tests\full_'+(Get-Date -Format 'yyyyMMdd_HHmmss_fff'))
$clone=Join-Path $test 'package';$game=Join-Path $test 'game';$stateRoot=Join-Path $test 'state'
$null=New-Item -ItemType Directory -Path $clone,$game
$catalog=Get-Content -LiteralPath (Join-Path $package 'catalog.json') -Raw -Encoding UTF8|ConvertFrom-Json
foreach($file in $catalog.files){$source=Join-SafePath $package $file.source;$target=Join-SafePath $clone $file.source;$null=New-Item -ItemType Directory -Force -Path (Split-Path $target);$null=New-Item -ItemType HardLink -Path $target -Target $source}
Copy-Item -LiteralPath (Join-Path $package 'defaults') -Destination (Join-Path $clone 'defaults') -Recurse
Write-Utf8 (Join-Path $game 're4.exe') 'NOT AN EXECUTABLE - installer fixture only'
Write-Utf8 (Join-Path $game 'dinput8.dll') 'previous framework'
Write-Utf8 (Join-Path $game 'reframework/data/re4unified/ui.json') '{"language":"ru","quality":3,"fpsLimitEnabled":true,"fpsLimit":90,"customField":true}'
Write-Utf8 (Join-Path $game 'reframework/data/re4unified/engine_command.json') '{"quality":3,"requested":false,"revision":48}'
Write-Utf8 (Join-Path $game 'OptiScaler.ini') "[DlssNr]`r`nEnabled=true`r`nIntensity=1.23`r`nPreset=2`r`n[Other]`r`nKeep=1"
Write-Utf8 (Join-Path $game 'local_config.ini') "[AppRender]`r`nAppTextureQuality=7`r`nAppUpScalingTypeFSR1=1"
Write-Utf8 (Join-Path $game 'unrelated-save.txt') 'unchanged'
$catalog.supportedGameSha256=Get-FileDigest (Join-Path $game 're4.exe')
foreach($guard in $catalog.guards){if($guard.path -eq 'dinput8.dll'){$guard.acceptedHashes+=Get-FileDigest (Join-Path $game 'dinput8.dll')}}
Write-Utf8 (Join-Path $clone 'catalog.json') ($catalog|ConvertTo-Json -Depth 12)
$before=@{};Get-ChildItem -LiteralPath $game -Recurse -File|ForEach-Object {$before[$_.FullName.Substring($game.Length+1)]=Get-FileDigest $_.FullName}
$gpu=[pscustomobject]@{name='NVIDIA GeForce RTX 5070 Ti';generation=50;driverNumber=62101;driver='32.0.16.2101';pnp='PCI\VEN_10DE'}
$plan=Get-InstallPlan $clone $game $gpu native enabled $false
$result=Invoke-InstallPlan $plan $stateRoot
$checked=0
foreach($file in $catalog.files){if($file.group -in @('base','fg') -and !$file.preserveExisting){if((Get-FileDigest (Join-SafePath $game $file.path)) -ne $file.sha256){throw 'Full-payload installed hash mismatch'};$checked++}}
if((Read-GameText $game 'reframework/data/re4unified/engine_command.json') -ne '{"quality":3,"requested":false,"revision":48}'){throw 'Quality command was not preserved'}
if((Read-GameText $game 'OptiScaler.ini') -notmatch 'Intensity=1.23'){throw 'NR intensity was not preserved'}
$null=Restore-SmartInstall $result.manifest
$after=@(Get-ChildItem -LiteralPath $game -Recurse -File)
if($after.Count -ne $before.Count){throw 'Full-payload rollback left added files'}
foreach($entry in $before.GetEnumerator()){if((Get-FileDigest (Join-SafePath $game $entry.Key)) -ne $entry.Value){throw 'Full-payload original restore mismatch'}}
$report=[ordered]@{passed=$true;realPayloadFilesVerified=$checked;transactionFiles=$plan.changes.Count;allOriginalsRestored=$true;allAddedFilesRemoved=$true;qualityCommandPreserved=$true;nrPreferencesPreserved=$true;powerShell=$PSVersionTable.PSVersion.ToString();gameExecuted=$false;realGameModified=$false;testRoot=$test;at=(Get-Date -Format o)}
Write-Utf8 (Join-Path $PSScriptRoot 'full_payload_roundtrip.json') ($report|ConvertTo-Json -Depth 5)
$report|ConvertTo-Json -Depth 5
