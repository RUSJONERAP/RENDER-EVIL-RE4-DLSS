# Shared engine for the GUI and CLI. No driver/Windows settings are written.
$ErrorActionPreference='Stop'
function Get-FileDigest([string]$Path){(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash}
function Join-SafePath([string]$Root,[string]$Relative){
 if([IO.Path]::IsPathRooted($Relative) -or $Relative.Contains(':') -or $Relative -match '(^|[\\/])\.\.([\\/]|$)'){throw "Unsafe relative path: $Relative"}
 $base=[IO.Path]::GetFullPath($Root).TrimEnd('\')+'\';$path=[IO.Path]::GetFullPath((Join-Path $base $Relative))
 if(!$path.StartsWith($base,[StringComparison]::OrdinalIgnoreCase) -or $path -eq $base.TrimEnd('\')){throw 'Path escapes the selected folder.'}
 $existing=Get-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue
 if($existing -and ($existing.Attributes -band [IO.FileAttributes]::ReparsePoint)){throw "Linked target file requires manual review: $path"}
 $parent=Split-Path $path
 while($parent -and $parent.Length -gt $base.TrimEnd('\').Length){
  if((Test-Path -LiteralPath $parent) -and ((Get-Item -LiteralPath $parent -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw "Nested linked folder requires manual review: $parent"}
  $parent=Split-Path $parent
 }
 return $path
}
function Write-Utf8([string]$Path,[string]$Text){
 $null=New-Item -ItemType Directory -Force -Path (Split-Path $Path)
 [IO.File]::WriteAllText($Path,$Text,(New-Object Text.UTF8Encoding($false)))
}
function Copy-AtomicFile([string]$Source,[string]$Destination){
 # Replace the directory entry, never overwrite the data of a mod-manager hardlink.
 $temp=$Destination+'.render-evil-'+[guid]::NewGuid().ToString('N')+'.tmp'
 $replaced=$temp+'.old'
 try{
  Copy-Item -LiteralPath $Source -Destination $temp
  if(Test-Path -LiteralPath $Destination){[IO.File]::Replace($temp,$Destination,$replaced)}else{[IO.File]::Move($temp,$Destination)}
 }finally{foreach($temporary in @($temp,$replaced)){if(Test-Path -LiteralPath $temporary){Remove-Item -LiteralPath $temporary -Force}}}
}
function Set-IniValue([string]$Source,[string]$Section,[string]$Key,[string]$Value){
 $lines=[regex]::Split($Source,'\r?\n');$output=New-Object Collections.Generic.List[string]
 $current='';$found=$false;$seen=($Section -eq '')
 foreach($line in $lines){
  if($line -match '^\s*\[([^\]]+)\]\s*$'){
   if($current -ieq $Section -and !$found){$output.Add("$Key=$Value");$found=$true}
   $current=$Matches[1];if($current -ieq $Section){$seen=$true}
  }
  if($current -ieq $Section -and $line -match ('^\s*'+[regex]::Escape($Key)+'\s*=')){
   if(!$found){$output.Add("$Key=$Value");$found=$true}
  }else{$output.Add($line)}
 }
 if(!$found){if(!$seen){$output.Add("[$Section]")};$output.Add("$Key=$Value")}
 return ($output -join "`r`n")
}
function Get-GameCandidates {
 $roots=New-Object Collections.Generic.List[string]
 foreach($key in @('HKCU:\Software\Valve\Steam','HKLM:\SOFTWARE\WOW6432Node\Valve\Steam')){
  $value=Get-ItemProperty -LiteralPath $key -ErrorAction SilentlyContinue
  if($value.SteamPath){$roots.Add($value.SteamPath)}elseif($value.InstallPath){$roots.Add($value.InstallPath)}
 }
 $libraries=New-Object Collections.Generic.List[string]
 foreach($root in ($roots|Select-Object -Unique)){
  $libraries.Add($root);$file=Join-Path $root 'steamapps\libraryfolders.vdf'
  if(Test-Path -LiteralPath $file){foreach($m in [regex]::Matches([IO.File]::ReadAllText($file),'"path"\s+"([^"]+)"')){$libraries.Add($m.Groups[1].Value.Replace('\\','\'))}}
 }
 $found=New-Object 'Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
 foreach($library in ($libraries|Select-Object -Unique)){
  $acf=Join-Path $library 'steamapps\appmanifest_2050650.acf'
  if(Test-Path -LiteralPath $acf){$m=[regex]::Match([IO.File]::ReadAllText($acf),'"installdir"\s+"([^"]+)"');if($m.Success){$candidate=Join-Path $library ('steamapps\common\'+$m.Groups[1].Value);if((Test-Path -LiteralPath (Join-Path $candidate 're4.exe')) -and $found.Add([IO.Path]::GetFullPath($candidate))){$candidate}}}
 }
}
function Get-GpuGeneration([string]$Name){
 if($Name -match '(?i)\bRTX\s*(20|30|40|50)\d{2}\b'){return [int]$Matches[1]}
 return 0
}
function Get-NvidiaDriverNumber([string]$Version){
 try{$parts=$Version.Split('.');if($parts.Count -eq 4){return (([int]$parts[2]%10)*10000+[int]$parts[3])}}catch{}
 return 0
}
function Get-InstallerHardware {
 $gpu=@();$problem=$null
 try{$gpu=@(Get-CimInstance Win32_VideoController|ForEach-Object {
  [pscustomobject]@{name=$_.Name;generation=(Get-GpuGeneration $_.Name);driver=$_.DriverVersion;driverNumber=(Get-NvidiaDriverNumber $_.DriverVersion);pnp=$_.PNPDeviceID}
 })}catch{$problem=$_.Exception.Message}
 $hags='unknown';$key=Get-ItemProperty -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Control\GraphicsDrivers' -ErrorAction SilentlyContinue
 if($key -and $null -ne $key.HwSchMode){if($key.HwSchMode -eq 2){$hags='enabled'}elseif($key.HwSchMode -eq 1){$hags='disabled'}}
 return [pscustomobject]@{gpus=$gpu;hags=$hags;is64Bit=[Environment]::Is64BitOperatingSystem;error=$problem}
}
function Resolve-InstallProfile($Gpu,[string]$Mode,[string]$Hags,[bool]$AcceptedExperimental){
 $warnings=New-Object Collections.Generic.List[string];$errors=New-Object Collections.Generic.List[string]
 $generation=0;if($Gpu){$generation=[int]$Gpu.generation}
 $selected=$Mode
 if($Mode -eq 'auto'){if($generation -in @(40,50) -and $Hags -ne 'disabled'){$selected='native'}else{$selected='off'}}
 if($selected -eq 'native' -and $generation -notin @(40,50)){$errors.Add('Native FG requires a selected RTX 40/50 GPU. Choose FG off or the experimental older-GPU profile.')}
 if($selected -eq 'legacy'){
  if($generation -notin @(20,30)){$errors.Add('The experimental adapter is restricted to RTX 20/30.')}
  if(!$AcceptedExperimental){$errors.Add('Explicitly acknowledge experimental RTX 20/30 support.')}
  if(!$Gpu -or $Gpu.driverNumber -lt 58000){$errors.Add('The older-GPU adapter requires a detectable NVIDIA R580 or newer driver.')}
  $warnings.Add('RTX 20/30 + our SR/NR/FG combination is not game-tested. NR starts off on a fresh older-GPU installation; enable it separately after checking performance.')
 }
 if($selected -ne 'off' -and $Hags -eq 'disabled'){$errors.Add('Hardware-accelerated GPU scheduling is disabled. Enable HAGS in Windows, reboot, then install FG; FG off remains available.')}
 if($selected -ne 'off' -and $Hags -eq 'unknown'){$warnings.Add('HAGS state is unknown. Windows defaults cannot be inferred from a missing registry key; the game must confirm FG support.')}
 if($generation -eq 0){$warnings.Add('GPU family is not recognized. FG stays off; DLSS runtime support is checked by the game. No GPU identity is spoofed.')}
 return [pscustomobject]@{mode=$selected;generation=$generation;warnings=@($warnings.ToArray());errors=@($errors.ToArray())}
}
function Test-GameRunning([string]$GameDirectory){
 foreach($process in @(Get-Process re4 -ErrorAction SilentlyContinue)){
  try{if([IO.Path]::GetFullPath($process.Path) -ieq (Join-SafePath $GameDirectory 're4.exe')){return $true}}catch{return $true}
 }
 return $false
}
function Get-InstallIndex([string]$GameDirectory,[string]$StateRoot){
 $bytes=[Text.Encoding]::UTF8.GetBytes([IO.Path]::GetFullPath($GameDirectory).TrimEnd('\').ToLowerInvariant())
 $sha=[Security.Cryptography.SHA256]::Create();try{$id=([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-','')}finally{$sha.Dispose()}
 return Join-Path $StateRoot ('games\'+$id+'\last-install.json')
}
function Read-GameText([string]$Root,[string]$Relative){$p=Join-SafePath $Root $Relative;if(Test-Path -LiteralPath $p){return [IO.File]::ReadAllText($p)};return ''}
function Get-InstallPlan {
 param([string]$PackageRoot,[string]$GameDirectory,$Gpu,[string]$Mode='auto',[string]$Hags='unknown',[bool]$AcceptedExperimental=$false,[string]$NrMode='preserve')
 $catalog=Get-Content -LiteralPath (Join-Path $PackageRoot 'catalog.json') -Raw -Encoding UTF8|ConvertFrom-Json
 $game=[IO.Path]::GetFullPath($GameDirectory).TrimEnd('\')
 if(Test-GameRunning $game){throw 'Close RE4 before installing or changing profiles.'}
 $exe=Join-SafePath $game 're4.exe';if(!(Test-Path -LiteralPath $exe)){throw 'Select the game folder containing re4.exe.'}
 if((Get-FileDigest $exe) -ne $catalog.supportedGameSha256){throw 'This package supports the documented Steam RE4 1.5.9.0 executable only. No files changed.'}
 if($Gpu -and $Gpu.pnp -and $Gpu.pnp -notmatch '(?i)VEN_10DE') {throw 'This DLSS package requires an NVIDIA RTX GPU. Select the discrete RTX adapter on a multi-GPU system.'}
 $profile=Resolve-InstallProfile $Gpu $Mode $Hags $AcceptedExperimental
 if($profile.errors.Count){throw ($profile.errors -join "`n")}
 $warnings=New-Object Collections.Generic.List[string];foreach($w in $profile.warnings){$warnings.Add($w)}
 foreach($name in @('dxgi.dll','d3d12.dll','PDPerfPlugin.dll','UpscalerBasePlugin.dll','reframework/plugins/PDPerfPlugin.dll','reframework/plugins/UpscalerBasePlugin.dll','MFGUnlocker.asi','MFGUnlocker.dll','OptiScaler/plugins/MFGUnlocker.asi','OptiScaler/plugins/MFGUnlocker.dll','winhttp.dll','dbghelp.dll')){
  if(Test-Path -LiteralPath (Join-SafePath $game $name)){throw "An existing injector needs manual review: $name. No files changed."}
 }
 foreach($guard in $catalog.guards){
  $path=Join-SafePath $game $guard.path
  if((Test-Path -LiteralPath $path) -and (Get-FileDigest $path) -notin $guard.acceptedHashes){throw "An unknown component occupies $($guard.path). It will not be overwritten."}
 }
 # Verify ALL bundled dependencies, including optional files, before a transaction.
 foreach($file in $catalog.files){$src=Join-SafePath $PackageRoot $file.source;if((Get-FileDigest $src) -ne $file.sha256){throw "Package checksum mismatch: $($file.source)"}}
 $changes=New-Object Collections.Generic.List[object]
 $groups=@('base');if($profile.mode -ne 'off'){$groups+='fg'};if($profile.mode -eq 'legacy'){$groups+='legacy'}
 foreach($file in $catalog.files){
  $dst=Join-SafePath $game $file.path
  if($file.group -in $groups){
   if($file.preserveExisting -and (Test-Path -LiteralPath $dst)){continue}
   $changes.Add([pscustomobject]@{path=$file.path;source=(Join-SafePath $PackageRoot $file.source);text=$null;remove=$false;mutable=[bool]$file.mutable;expectedHash=$file.sha256})
  }elseif(Test-Path -LiteralPath $dst){
   if($file.mutable -or (Get-FileDigest $dst) -eq $file.sha256){
    if(!$file.mutable -or $file.path -eq 'dlssg_sm86.ini'){$changes.Add([pscustomobject]@{path=$file.path;source=$null;text=$null;remove=$true;mutable=[bool]$file.mutable;expectedHash=$null})}
   }else{throw "A changed optional component requires review: $($file.path)"}
  }
 }
 # Ensure no duplicate payload/config paths can overwrite each other in the plan.
 function Add-Config([string]$Relative,[string]$Text){
  if(@($changes|Where-Object path -eq $Relative).Count){throw "Duplicate planned target: $Relative"}
  $changes.Add([pscustomobject]@{path=$Relative;source=$null;text=$Text;remove=$false;mutable=$true;expectedHash=$null})
 }
 $opti=Read-GameText $game 'OptiScaler.ini';if(!$opti){$opti=[IO.File]::ReadAllText((Join-Path $PackageRoot 'defaults\OptiScaler.ini'))}
 foreach($row in @(@('FrameGen','Enabled','false'),@('FrameGen','FGInput','NoFG'),@('FrameGen','FGOutput','NoFG'),@('FrameGen','FGNvngxReplacement','None'),@('Menu','OverlayMenu','false'),@('Menu','ShortcutKey','-1'),@('Inputs','EnableDlssInputs','false'),@('Spoofing','DxgiSpoofing','false'),@('Spoofing','StreamlineSpoofing','false'),@('Spoofing','SpoofHAGS','false'),@('DlssNr','Placement','evaluate'),@('DlssNr','LiveReload','true'),@('DlssNr','PanelKey','0x79'),@('Framerate','FramerateLimit','0'),@('Pacing','FpsLimit','off'))){$opti=Set-IniValue $opti $row[0] $row[1] $row[2]}
 if($NrMode -in @('on','off')){$opti=Set-IniValue $opti 'DlssNr' 'Enabled' ([string]($NrMode -eq 'on')).ToLowerInvariant()}
 Add-Config 'OptiScaler.ini' $opti
 $actual=Read-GameText $game 'reframework/data/re4dlss/re4dlss.ini';if(!$actual){$actual=[IO.File]::ReadAllText((Join-Path $PackageRoot 'defaults\re4dlss.ini'))}
 $actual=Set-IniValue $actual 'FrameGen' 'FrameGen' ([int]($profile.mode -ne 'off'))
 $actual=Set-IniValue $actual 'FrameGen' 'Multiplier' '2';$actual=Set-IniValue $actual 'FrameGen' 'Reflex' '1'
 Add-Config 'reframework/data/re4dlss/re4dlss.ini' $actual
 $cfg=Read-GameText $game 're2_fw_config.txt'
 foreach($key in @('TemporalUpscaler_Enabled','TemporalUpscaler_HUDTrace','REFrameworkConfig_MenuOpen','REFrameworkConfig_RememberMenuState')){$cfg=Set-IniValue $cfg '' $key 'false'}
 $cfg=Set-IniValue $cfg '' 'REFrameworkConfig_MenuKey_V2' '45';Add-Config 're2_fw_config.txt' $cfg
 $cfg=Read-GameText $game 'local_config.ini';foreach($pair in @(@('AppUpScalingTypeFSR1','0'),@('AppUpScalingTypeFSR2','1'),@('AppFrameRate','3'))){$cfg=Set-IniValue $cfg 'AppRender' $pair[0] $pair[1]};Add-Config 'local_config.ini' $cfg
 $uiText=Read-GameText $game 'reframework/data/re4unified/ui.json';$ui=$null
 if($uiText){try{$ui=$uiText|ConvertFrom-Json}catch{throw 'Existing ui.json is invalid; preserve or repair it before installation.'}}
 if($ui -and $ui -isnot [pscustomobject]){throw 'Existing ui.json must contain a settings object.'}
 if(!$ui){$ui=[pscustomobject]@{language='en';quality=1;fpsLimit=72;fpsLimitEnabled=$false}}
 if($profile.mode -ne 'off'){$ui|Add-Member -NotePropertyName fpsLimitEnabled -NotePropertyValue $false -Force}
 Add-Config 'reframework/data/re4unified/ui.json' ($ui|ConvertTo-Json -Depth 12)
 Add-Config 'reframework/data/re4dlss/command.txt' ''
 Add-Config 'reframework/data/re4dlss/status.txt' 'Waiting for a new game session.'
 if($profile.mode -eq 'legacy'){
  $cfg=[IO.File]::ReadAllText((Join-Path $PackageRoot 'defaults\dlssg_sm86.ini'))
  # Restrict the optional adapter to FG: own SR/NR and menus retain ownership.
  foreach($row in @(@('General','Enabled','1'),@('FrameGeneration','Engine','DLSS'),@('FrameGeneration','MaxGeneratedFrames','1'),@('DLSSG','Optimized','1'),@('SuperResolution','Mode','Off'),@('Logging','Level','2'))){$cfg=Set-IniValue $cfg $row[0] $row[1] $row[2]}
  Add-Config 'dlssg_sm86.ini' $cfg
 }
 foreach($name in @('reframework/autorun/re4dlss_ui.lua','reframework/autorun/0_hudlab_probe.lua','reframework/autorun/1_hudlab_menu.lua')){
  if(Test-Path -LiteralPath (Join-SafePath $game $name)){$changes.Add([pscustomobject]@{path=$name;source=$null;text=$null;remove=$true;mutable=$true;expectedHash=$null})}
 }
 $total=0L
 foreach($item in $changes){
  $target=Join-SafePath $game $item.path
  $exists=Test-Path -LiteralPath $target;$hash=$null;if($exists){$hash=Get-FileDigest $target}
  $item|Add-Member -NotePropertyName originalExisted -NotePropertyValue $exists
  $item|Add-Member -NotePropertyName originalHash -NotePropertyValue $hash
  if($item.source){$total+=(Get-Item -LiteralPath $item.source).Length}
 }
 return [pscustomobject]@{schema=1;version=$catalog.version;game=$game;gameSha256=$catalog.supportedGameSha256;profile=$profile;gpu=$Gpu;hags=$Hags;nrMode=$NrMode;warnings=@($warnings.ToArray());changes=@($changes.ToArray());bytes=$total;catalogHash=(Get-FileDigest (Join-Path $PackageRoot 'catalog.json'))}
}
function Invoke-InstallPlan {
 param($Plan,[string]$StateRoot=(Join-Path $env:LOCALAPPDATA 'RENDER-EVIL\SmartInstaller'))
 if(Test-GameRunning $Plan.game){throw 'Close RE4 before installing.'}
 if((Get-FileDigest (Join-SafePath $Plan.game 're4.exe')) -ne $Plan.gameSha256){throw 'The game executable changed after preflight.'}
 foreach($item in $Plan.changes){$dst=Join-SafePath $Plan.game $item.path;if((Test-Path -LiteralPath $dst) -ne $item.originalExisted){throw 'A target appeared/disappeared after preflight.'};if($item.originalExisted -and (Get-FileDigest $dst) -ne $item.originalHash){throw 'A file changed after preflight; check the installation again.'}}
 $drive=New-Object IO.DriveInfo ([IO.Path]::GetPathRoot($Plan.game))
 if($drive.AvailableFreeSpace -lt ($Plan.bytes+32MB)){throw 'Not enough free space in the game drive for this profile.'}
 $stateRootFull=[IO.Path]::GetFullPath($StateRoot).TrimEnd('\')+'\';$game=[IO.Path]::GetFullPath($Plan.game).TrimEnd('\')+'\'
 if($stateRootFull.StartsWith($game,[StringComparison]::OrdinalIgnoreCase)){throw 'Backup/state storage must be outside the game.'}
 $backup=Join-Path $StateRoot ('Backups\'+(Get-Date -Format 'yyyyMMdd_HHmmss_fff')+'_'+[guid]::NewGuid().ToString('N').Substring(0,6))
 $null=New-Item -ItemType Directory -Path $backup -Force
 $index=Get-InstallIndex $Plan.game $StateRoot;$oldIndex=$null;if(Test-Path -LiteralPath $index){$oldIndex=[IO.File]::ReadAllText($index)}
 $state=[ordered]@{schema=1;version=$Plan.version;status='prepared';game=$Plan.game;backup=$backup;profile=$Plan.profile.mode;gpu=$Plan.gpu;created=(Get-Date -Format o);index=$index;previousIndex=$oldIndex;files=@()}
 $journal=Join-Path $backup 'installed.json'
 foreach($item in $Plan.changes){
  $dst=Join-SafePath $Plan.game $item.path;$src=$item.source
  if($null -ne $item.text){$src=Join-SafePath $backup ('candidate/'+$item.path);Write-Utf8 $src $item.text}
  $entry=[ordered]@{path=$item.path;source=$src;remove=$item.remove;mutable=$item.mutable;existed=(Test-Path -LiteralPath $dst);originalHash=$null;newHash=$null;attributes=0}
  if(!$item.remove){$entry.newHash=Get-FileDigest $src;if($item.expectedHash -and $entry.newHash -ne $item.expectedHash){throw 'Payload changed after preflight.'}}
  if($entry.existed){$original=Join-SafePath $backup ('originals/'+$item.path);$null=New-Item -ItemType Directory -Force -Path (Split-Path $original);Copy-Item -LiteralPath $dst -Destination $original;$entry.originalHash=Get-FileDigest $original;$entry.attributes=[int][IO.File]::GetAttributes($dst);if((Get-FileDigest $dst) -ne $entry.originalHash){throw 'Original changed during backup.'}}
  $state.files+=,$entry
 }
 $receipt=[ordered]@{version=$Plan.version;profile=$Plan.profile.mode;manifest=$journal;experimental=($Plan.profile.mode -eq 'legacy')}
 $receiptPath='reframework/data/re4unified/installation.json';$dst=Join-SafePath $Plan.game $receiptPath;$receiptSource=Join-SafePath $backup ('candidate/'+$receiptPath);Write-Utf8 $receiptSource ($receipt|ConvertTo-Json)
 $receiptEntry=[ordered]@{path=$receiptPath;source=$receiptSource;remove=$false;mutable=$false;existed=(Test-Path -LiteralPath $dst);originalHash=$null;newHash=(Get-FileDigest $receiptSource);attributes=0}
 if($receiptEntry.existed){$old=Join-SafePath $backup ('originals/'+$receiptPath);$null=New-Item -ItemType Directory -Force -Path (Split-Path $old);Copy-Item -LiteralPath $dst -Destination $old;$receiptEntry.originalHash=Get-FileDigest $old;$receiptEntry.attributes=[int][IO.File]::GetAttributes($dst)}
 $state.files+=,$receiptEntry
 Write-Utf8 $journal ($state|ConvertTo-Json -Depth 12)
 if(Test-GameRunning $Plan.game){throw 'RE4 started during preparation. No game files changed.'}
 foreach($entry in $state.files){$dst=Join-SafePath $Plan.game $entry.path;if($entry.existed){if((Get-FileDigest $dst) -ne $entry.originalHash){throw 'Original changed before commit.'}}elseif(Test-Path -LiteralPath $dst){throw 'A new target appeared before commit.'}}
 $applied=New-Object Collections.Generic.List[object]
 try{
  foreach($entry in $state.files){
   $dst=Join-SafePath $Plan.game $entry.path;$applied.Add($entry)
   if(Test-Path -LiteralPath $dst){[IO.File]::SetAttributes($dst,[IO.FileAttributes]::Normal)}
   if($entry.remove){Remove-Item -LiteralPath $dst -Force}else{$null=New-Item -ItemType Directory -Force -Path (Split-Path $dst);Copy-AtomicFile $entry.source $dst;if((Get-FileDigest $dst) -ne $entry.newHash){throw "Installed checksum mismatch: $($entry.path)"}}
  }
  $state.status='installed';Write-Utf8 $journal ($state|ConvertTo-Json -Depth 12)
  Write-Utf8 $index (@{manifest=$journal;game=$Plan.game;version=$Plan.version}|ConvertTo-Json)
 }catch{
  $failure=$_
  foreach($entry in $applied){$dst=Join-SafePath $Plan.game $entry.path;if(Test-Path -LiteralPath $dst){[IO.File]::SetAttributes($dst,[IO.FileAttributes]::Normal)};if($entry.existed){Copy-AtomicFile (Join-SafePath $backup ('originals/'+$entry.path)) $dst;[IO.File]::SetAttributes($dst,[IO.FileAttributes]$entry.attributes)}elseif(Test-Path -LiteralPath $dst){Remove-Item -LiteralPath $dst -Force}}
  $state.status='failed_restored';Write-Utf8 $journal ($state|ConvertTo-Json -Depth 12)
  if($null -ne $oldIndex){Write-Utf8 $index $oldIndex}elseif(Test-Path -LiteralPath $index){Remove-Item -LiteralPath $index -Force}
  throw $failure
 }
 return [pscustomobject]@{manifest=$journal;backup=$backup;profile=$Plan.profile.mode;files=$state.files.Count}
}
function Restore-SmartInstall([string]$Manifest){
 $state=Get-Content -LiteralPath $Manifest -Raw -Encoding UTF8|ConvertFrom-Json
 if($state.schema -ne 1 -or $state.status -ne 'installed'){throw 'This is not an active smart-installer backup.'}
 if(Test-GameRunning $state.game){throw 'Close RE4 before restoring files.'}
 foreach($entry in $state.files){
  $dst=Join-SafePath $state.game $entry.path
  if($entry.existed -and (Get-FileDigest (Join-SafePath $state.backup ('originals/'+$entry.path))) -ne $entry.originalHash){throw 'A backup checksum is invalid; no files changed.'}
  if(!$entry.mutable -and !$entry.remove -and (Test-Path -LiteralPath $dst) -and (Get-FileDigest $dst) -ne $entry.newHash){throw "A component was changed after installation: $($entry.path). No files changed."}
  if($entry.remove -and (Test-Path -LiteralPath $dst)){throw 'An archived script was recreated; review it before restoring.'}
 }
 $saved=Join-Path $state.backup ('after_install_'+(Get-Date -Format 'yyyyMMdd_HHmmss_fff'))
 foreach($entry in $state.files){
  $dst=Join-SafePath $state.game $entry.path
  if(Test-Path -LiteralPath $dst){$capture=Join-SafePath $saved $entry.path;$null=New-Item -ItemType Directory -Force -Path (Split-Path $capture);Copy-Item -LiteralPath $dst -Destination $capture;[IO.File]::SetAttributes($dst,[IO.FileAttributes]::Normal)}
  if($entry.existed){$null=New-Item -ItemType Directory -Force -Path (Split-Path $dst);Copy-AtomicFile (Join-SafePath $state.backup ('originals/'+$entry.path)) $dst;if((Get-FileDigest $dst) -ne $entry.originalHash){throw 'Restored checksum mismatch.'};[IO.File]::SetAttributes($dst,[IO.FileAttributes]$entry.attributes)}elseif(Test-Path -LiteralPath $dst){Remove-Item -LiteralPath $dst -Force}
 }
 $state.status='rolled_back';Write-Utf8 $Manifest ($state|ConvertTo-Json -Depth 12)
 if($null -ne $state.previousIndex){Write-Utf8 $state.index $state.previousIndex}elseif(Test-Path -LiteralPath $state.index){Remove-Item -LiteralPath $state.index -Force}
 return [pscustomobject]@{restored=$true;game=$state.game;settingsSaved=$saved}
}
