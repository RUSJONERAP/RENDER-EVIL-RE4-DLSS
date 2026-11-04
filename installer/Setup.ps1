[CmdletBinding()]
param([ValidateSet('ru','en')][string]$Language,[string]$Preview,[switch]$CheckUi)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'Installer.Core.ps1')
Add-Type -AssemblyName System.Windows.Forms,System.Drawing
[Windows.Forms.Application]::EnableVisualStyles()
if(!$Language){if([Globalization.CultureInfo]::CurrentUICulture.TwoLetterISOLanguageName -eq 'ru'){$Language='ru'}else{$Language='en'}}
$script:setupLang=$Language;$script:c=@{};$script:setupPlan=$null;$script:setupBusy=$false
function L([string]$Ru,[string]$En){if($script:setupLang -eq 'ru'){return $Ru};return $En}
function Localize-SetupMessage([string]$Text){
 if($script:setupLang -ne 'ru'){return $Text}
 $messages=[ordered]@{
  'Select the game folder containing re4.exe.'='Выберите папку игры с файлом re4.exe.'
  'Close RE4 before installing or changing profiles.'='Закройте RE4 перед установкой или сменой профиля.'
  'Close RE4 before installing.'='Закройте RE4 перед установкой.'
  'This package supports the documented Steam RE4 1.5.9.0 executable only. No files changed.'='Поддерживается указанная Steam-версия RE4 1.5.9.0. Хеш EXE не совпал; файлы не изменены.'
  'Native FG requires a selected RTX 40/50 GPU. Choose FG off or the experimental older-GPU profile.'='Для штатного FG выберите RTX 40/50. На старых картах доступен вариант без FG или экспериментальный профиль.'
  'The experimental adapter is restricted to RTX 20/30.'='Экспериментальный адаптер предназначен только для RTX 20/30.'
  'Explicitly acknowledge experimental RTX 20/30 support.'='Подтвердите галочкой экспериментальный статус варианта RTX 20/30.'
  'The older-GPU adapter requires a detectable NVIDIA R580 or newer driver.'='Для адаптера старых GPU требуется определяемый драйвер NVIDIA R580 или новее.'
  'RTX 20/30 + our SR/NR/FG combination is not game-tested. NR starts off on a fresh older-GPU installation; enable it separately after checking performance.'='Связка SR/NR/FG на RTX 20/30 ещё не проверена в игре. На новой установке NR выключен; включайте его отдельно после проверки производительности.'
  'Hardware-accelerated GPU scheduling is disabled. Enable HAGS in Windows, reboot, then install FG; FG off remains available.'='Аппаратное планирование GPU выключено. Включите HAGS в Windows и перезагрузите компьютер либо выберите установку без FG.'
  'HAGS state is unknown. Windows defaults cannot be inferred from a missing registry key; the game must confirm FG support.'='Состояние HAGS не определено: отсутствие ключа реестра не означает отключение. Поддержку FG подтвердит запуск игры.'
  'GPU family is not recognized. FG stays off; DLSS runtime support is checked by the game. No GPU identity is spoofed.'='Семейство GPU не распознано. FG выключен; совместимость DLSS проверяется в игре. Данные видеокарты не подменяются.'
  'This DLSS package requires an NVIDIA RTX GPU. Select the discrete RTX adapter on a multi-GPU system.'='Для этой сборки нужна NVIDIA RTX. На системе с несколькими GPU выберите дискретную RTX.'
  'An existing injector needs manual review: '='Найден инжектор, который нужно проверить вручную: '
  'An unknown component occupies '='Неизвестный компонент занимает файл '
  'It will not be overwritten.'='Он не будет перезаписан.'
  'Package checksum mismatch: '='Не совпал хеш файла комплекта: '
  'No files changed.'='Файлы не изменены.'
  'Existing ui.json is invalid; preserve or repair it before installation.'='Существующий ui.json повреждён. Сохраните копию и исправьте его перед установкой.'
  'Not enough free space in the game drive for this profile.'='Недостаточно свободного места на диске игры для выбранного профиля.'
 }
 foreach($key in $messages.Keys){$Text=$Text.Replace($key,$messages[$key])};return $Text
}
function Ink([int]$R,[int]$G,[int]$B){[Drawing.Color]::FromArgb($R,$G,$B)}
$script:amber=Ink 230 172 80;$script:white=Ink 236 233 226;$script:muted=Ink 151 156 165;$script:card=Ink 20 22 26
$script:form=New-Object Windows.Forms.Form
$form.Text='RE:NDER EVIL — Setup';$form.ClientSize=New-Object Drawing.Size(1000,800)
$form.StartPosition='CenterScreen';$form.FormBorderStyle='FixedSingle';$form.MaximizeBox=$false
$form.BackColor=Ink 10 11 13;$form.ForeColor=$white;$form.Font=New-Object Drawing.Font('Segoe UI',10)
$form.AutoScaleMode=[Windows.Forms.AutoScaleMode]::Dpi
function Label-At($Parent,[string]$Id,[int]$X,[int]$Y,[int]$W,[int]$H,[float]$Size=10){
 $v=New-Object Windows.Forms.Label;$v.Location=New-Object Drawing.Point($X,$Y);$v.Size=New-Object Drawing.Size($W,$H)
 $v.Font=New-Object Drawing.Font('Segoe UI',$Size);$v.ForeColor=$white;$Parent.Controls.Add($v);$script:c[$Id]=$v;return $v
}
function Card-At([int]$Y,[int]$Height){$p=New-Object Windows.Forms.Panel;$p.Location=New-Object Drawing.Point(28,$Y);$p.Size=New-Object Drawing.Size(944,$Height);$p.BackColor=$card;$form.Controls.Add($p);return $p}
function Combo-At($Parent,[string]$Id,[int]$X,[int]$Y,[int]$W){
 $v=New-Object Windows.Forms.ComboBox;$v.Location=New-Object Drawing.Point($X,$Y);$v.Size=New-Object Drawing.Size($W,32);$v.DropDownStyle='DropDownList';$v.FlatStyle='Flat';$v.BackColor=Ink 35 39 45;$v.ForeColor=$white
 $v.DrawMode='OwnerDrawFixed';$v.ItemHeight=24
 $v.Add_DrawItem({param($sender,$e)
  if($e.Index -lt 0){return};$background=New-Object Drawing.SolidBrush($sender.BackColor);$foreground=New-Object Drawing.SolidBrush($sender.ForeColor)
  $e.Graphics.FillRectangle($background,$e.Bounds);$e.Graphics.DrawString([string]$sender.Items[$e.Index],$sender.Font,$foreground,[float]($e.Bounds.X+6),[float]($e.Bounds.Y+3));$background.Dispose();$foreground.Dispose()
 })
 $Parent.Controls.Add($v);$script:c[$Id]=$v;return $v
}
function Button-At($Parent,[string]$Id,[int]$X,[int]$Y,[int]$W,[int]$H){
 $v=New-Object Windows.Forms.Button;$v.Location=New-Object Drawing.Point($X,$Y);$v.Size=New-Object Drawing.Size($W,$H);$v.FlatStyle='Flat';$v.FlatAppearance.BorderColor=Ink 55 60 67;$v.BackColor=Ink 35 39 45;$v.ForeColor=$white;$Parent.Controls.Add($v);$script:c[$Id]=$v;return $v
}
$title=Label-At $form 'title' 28 18 710 47 27;$title.Text='RE:NDER EVIL';$title.ForeColor=$amber
$null=Label-At $form 'subtitle' 31 66 760 27 10
$languages=Combo-At $form 'language' 862 34 110;$null=$languages.Items.AddRange(@('Русский','English'));if($Language -eq 'ru'){$languages.SelectedIndex=0}else{$languages.SelectedIndex=1}
$pathCard=Card-At 108 110;$null=Label-At $pathCard 'pathLabel' 18 12 840 27 12
$path=New-Object Windows.Forms.TextBox;$path.Location=New-Object Drawing.Point(18,51);$path.Size=New-Object Drawing.Size(758,29);$path.BackColor=Ink 35 39 45;$path.ForeColor=$white;$path.BorderStyle='FixedSingle';$pathCard.Controls.Add($path);$c.path=$path
$browse=Button-At $pathCard 'browse' 790 48 135 34
$gpuCard=Card-At 230 108;$null=Label-At $gpuCard 'gpuLabel' 18 10 880 26 12
$gpuCombo=Combo-At $gpuCard 'gpu' 18 43 907;$gpuInfo=Label-At $gpuCard 'gpuInfo' 18 76 907 23 9;$gpuInfo.ForeColor=$muted
$options=Card-At 350 211;$null=Label-At $options 'fgLabel' 18 10 440 25 12;$null=Label-At $options 'nrLabel' 488 10 437 25 12
$mode=Combo-At $options 'mode' 18 43 440;$nr=Combo-At $options 'nr' 488 43 437
$ack=New-Object Windows.Forms.CheckBox;$ack.Location=New-Object Drawing.Point(18,82);$ack.Size=New-Object Drawing.Size(440,58);$ack.ForeColor=$amber;$options.Controls.Add($ack);$c.ack=$ack
$nrHelp=Label-At $options 'nrHelp' 488 82 437 58 9;$nrHelp.ForeColor=$muted
$explain=Label-At $options 'explain' 18 150 907 50 9;$explain.ForeColor=$muted
$reviewCard=Card-At 574 132;$reviewCard.AutoScroll=$true
$review=Label-At $reviewCard 'review' 18 12 896 110 10;$review.AutoSize=$true;$review.MaximumSize=New-Object Drawing.Size(896,0)
$status=Label-At $form 'status' 29 716 943 26 9;$status.ForeColor=$muted
$progress=New-Object Windows.Forms.ProgressBar;$progress.Location=New-Object Drawing.Point(30,757);$progress.Size=New-Object Drawing.Size(535,15);$progress.Style='Marquee';$progress.Visible=$false;$form.Controls.Add($progress);$c.progress=$progress
$check=Button-At $form 'check' 660 746 145 40;$install=Button-At $form 'install' 819 746 153 40;$install.BackColor=$amber;$install.ForeColor=Ink 10 11 13
if($Preview){
 $script:setupHardware=[pscustomobject]@{gpus=@([pscustomobject]@{name='NVIDIA GeForce RTX 5070 Ti';generation=50;driver='32.0.16.2101';driverNumber=62101;pnp='PCI\VEN_10DE'});hags='enabled';is64Bit=$true;error=$null}
 $path.Text='D:\SteamLibrary\steamapps\common\RESIDENT EVIL 4 BIOHAZARD RE4'
}else{
 $script:setupHardware=Get-InstallerHardware
 if(!$setupHardware.is64Bit){throw 'Windows x64 is required.'}
 $games=@(Get-GameCandidates|Select-Object -Unique);if($games.Count -eq 1){$path.Text=$games[0]}
}
foreach($g in @($setupHardware.gpus)){$null=$gpuCombo.Items.Add($g.name)}
if($gpuCombo.Items.Count){
 $choice=0;for($i=0;$i -lt $setupHardware.gpus.Count;$i++){if($setupHardware.gpus[$i].generation -in @(20,30,40,50)){$choice=$i;break}};$gpuCombo.SelectedIndex=$choice
}else{$null=$gpuCombo.Items.Add('GPU detection unavailable');$gpuCombo.SelectedIndex=0}
function Update-SetupText {
 $c.subtitle.Text=L 'DLSS SR + Neural Rendering + Frame Generation · единый установщик' 'DLSS SR + Neural Rendering + Frame Generation · unified installer'
 $c.pathLabel.Text=L '01  Папка Resident Evil 4' '01  Resident Evil 4 folder';$c.browse.Text=L 'Выбрать…' 'Browse…'
 $c.gpuLabel.Text=L '02  Видеокарта' '02  Graphics card';$c.fgLabel.Text=L '03  Генерация кадров' '03  Frame Generation';$c.nrLabel.Text=L 'Neural Rendering при запуске игры' 'Neural Rendering when the game starts'
 $m=$c.mode.SelectedIndex;if($m -lt 0){$m=0};$n=$c.nr.SelectedIndex;if($n -lt 0){$n=0}
 $c.mode.Items.Clear();$c.mode.Items.AddRange(@((L 'Автоматически · рекомендуется' 'Automatic · recommended'),(L 'Без генерации кадров' 'Frame Generation off'),(L 'Штатный FG · RTX 40 / 50' 'Native FG · RTX 40 / 50'),(L 'Экспериментальный FG · RTX 20 / 30' 'Experimental FG · RTX 20 / 30')));$c.mode.SelectedIndex=$m
 $c.nr.Items.Clear();$c.nr.Items.AddRange(@((L 'Не менять (первая установка — выкл.)' 'Keep current setting (first install: off)'),(L 'Запускать игру с выключенным NR' 'Start the game with NR off'),(L 'Запускать игру с включённым NR' 'Start the game with NR on')));$c.nr.SelectedIndex=$n
 $c.nrHelp.Text=L 'NR устанавливается всегда. Здесь выбирается только его состояние при запуске. Потом его можно включать и выключать в игре через Insert.' 'NR is always installed. This sets whether it starts on or off. You can turn it on or off later through Insert in the game.'
 $c.ack.Text=L 'Я понимаю: вариант RTX 20/30 с этой сборкой ещё не проверен в игре.' 'I understand: the RTX 20/30 path has not been game-tested with this build.'
 $c.check.Text=L 'Проверить' 'Check';$c.install.Text=L 'Установить' 'Install'
 if(!$script:setupPlan){$c.review.Text=L "  Все компоненты уже в комплекте. Установщик не скачивает файлы.`n`n  Нажмите «Проверить», чтобы увидеть выбранный профиль и возможные конфликты.`n  Предыдущие файлы и настройки будут сохранены для отката." "  All components are included. The installer does not download files.`n`n  Click Check to review the selected profile and detect conflicts.`n  Previous files and settings will be backed up for rollback."}
 $c.status.Text=L 'Сборка 0.2.0 preview.1 · Закройте игру перед установкой.' 'Build 0.2.0 preview.1 · Close the game before installing.'
}
function Update-SetupChoice {
 $gpu=$null;if($c.gpu.SelectedIndex -ge 0 -and $c.gpu.SelectedIndex -lt $setupHardware.gpus.Count){$gpu=$setupHardware.gpus[$c.gpu.SelectedIndex]}
 $driver='—';if($gpu -and $gpu.driverNumber){$driver=('{0:F2}' -f ($gpu.driverNumber/100.0))}
 $hags=switch($setupHardware.hags){'enabled'{L 'включено' 'enabled'}'disabled'{L 'выключено' 'disabled'}default{L 'не определено' 'unknown'}}
 $c.gpuInfo.Text=(L 'Драйвер NVIDIA: ' 'NVIDIA driver: ')+$driver+'  ·  HAGS: '+$hags
 $legacy=$c.mode.SelectedIndex -eq 3;$c.ack.Enabled=$legacy
 if(!$legacy){$c.ack.Checked=$false}
 if($legacy){$c.explain.Text=L 'Опциональный адаптер использует свой winmm.dll. Нужен драйвер R580+. NR и SR остаются под управлением нашей панели; поддержка этого сочетания экспериментальная.' 'The optional adapter uses its own winmm.dll and needs driver R580+. Our panel retains SR/NR control; this combination is experimental.'}
 else{$c.explain.Text=L 'RTX 40/50: штатный FG. RTX 20/30: автоматически без FG; эксперимент включается явно. Множитель начинается с 2×. HAGS проверяется без изменения Windows.' 'RTX 40/50: native FG. RTX 20/30: FG off automatically; opt in to the experiment. Starts at 2x. HAGS is checked without changing Windows.'}
 $script:setupPlan=$null
}
function Get-SetupPlan {
 if($Preview){throw 'Offline preview does not install files.'}
 $selected=$null;if($c.gpu.SelectedIndex -ge 0 -and $c.gpu.SelectedIndex -lt $setupHardware.gpus.Count){$selected=$setupHardware.gpus[$c.gpu.SelectedIndex]}
 $modes=@('auto','off','native','legacy');$nrModes=@('preserve','off','on')
 return Get-InstallPlan -PackageRoot $PSScriptRoot -GameDirectory $c.path.Text -Gpu $selected -Mode $modes[$c.mode.SelectedIndex] -Hags $setupHardware.hags -AcceptedExperimental $c.ack.Checked -NrMode $nrModes[$c.nr.SelectedIndex]
}
function Show-SetupPlan($Plan){
 $label=switch($Plan.profile.mode){'native'{L 'Штатный FG · 2×' 'Native FG · 2x'}'legacy'{L 'Экспериментальный FG · 2×' 'Experimental FG · 2x'}default{L 'Генерация кадров выключена' 'Frame Generation off'}}
 $text=(L '  Профиль: ' '  Profile: ')+$label+"`n"+(L '  Файлов: ' '  Files: ')+$Plan.changes.Count+'  ·  '+[math]::Round($Plan.bytes/1MB,1)+" MB`n"+(L '  В игре: FSR 2 — ВКЛ., FSR 1 — ВЫКЛ. Панель: Insert.' '  In game: FSR 2 ON, FSR 1 OFF. Panel: Insert.')+"`n"+(L '  Резервная копия: LocalAppData\RENDER-EVIL\SmartInstaller\Backups' '  Backup: LocalAppData\RENDER-EVIL\SmartInstaller\Backups')
 $nrState=Get-SetupNrState $Plan
 $text+="`n"+(L '  NR при запуске игры: ' '  NR when the game starts: ')+$(if($nrState){L 'включён' 'on'}else{L 'выключен' 'off'})
 foreach($warning in $Plan.warnings){$text+="`n  ! "+(Localize-SetupMessage $warning)};$c.review.Text=$text
}
function Get-SetupNrState($Plan){
 $config=@($Plan.changes|Where-Object path -eq 'OptiScaler.ini')[0].text
 $section=''
 foreach($line in [regex]::Split($config,'\r?\n')){
  if($line -match '^\s*\[([^\]]+)\]\s*$'){$section=$Matches[1]}
  elseif($section -ieq 'DlssNr' -and $line -match '^\s*Enabled\s*=\s*(true|false)\s*$'){return $Matches[1] -ieq 'true'}
 }
 throw 'Cannot determine NR startup setting.'
}
function Busy-Setup([bool]$Busy){$script:setupBusy=$Busy;foreach($id in @('check','install','browse','language','gpu','mode','nr','path')){$c[$id].Enabled=!$Busy};$c.ack.Enabled=(!$Busy -and $c.mode.SelectedIndex -eq 3);$c.progress.Visible=$Busy;$form.ControlBox=!$Busy;$form.UseWaitCursor=$Busy;[Windows.Forms.Application]::DoEvents()}
$form.Add_FormClosing({param($sender,$e)if($script:setupBusy){$e.Cancel=$true}})
$languages.Add_SelectedIndexChanged({$script:setupLang=if($c.language.SelectedIndex -eq 0){'ru'}else{'en'};Update-SetupText;Update-SetupChoice})
$mode.Add_SelectedIndexChanged({if($c.mode.SelectedIndex -ge 0){Update-SetupChoice}})
$gpuCombo.Add_SelectedIndexChanged({Update-SetupChoice})
$nr.Add_SelectedIndexChanged({$script:setupPlan=$null})
$path.Add_TextChanged({$script:setupPlan=$null})
$browse.Add_Click({$dialog=New-Object Windows.Forms.FolderBrowserDialog;$dialog.Description=L 'Выберите папку с re4.exe' 'Select the folder containing re4.exe';if($c.path.Text -and (Test-Path -LiteralPath $c.path.Text)){$dialog.SelectedPath=$c.path.Text};if($dialog.ShowDialog() -eq [Windows.Forms.DialogResult]::OK){$c.path.Text=$dialog.SelectedPath};$dialog.Dispose()})
$check.Add_Click({try{Busy-Setup $true;$script:setupPlan=Get-SetupPlan;Show-SetupPlan $setupPlan;$c.status.Text=L 'Проверка пройдена. Можно устанавливать.' 'Checks passed. Ready to install.'}catch{$c.review.Text=Localize-SetupMessage $_.Exception.Message;$c.status.Text=L 'Ничего не изменено. Исправьте указанную проблему.' 'Nothing changed. Resolve the issue shown above.'}finally{Busy-Setup $false}})
$install.Add_Click({try{Busy-Setup $true;$c.status.Text=L 'Проверка файлов и создание резервной копии…' 'Checking files and creating a backup…';[Windows.Forms.Application]::DoEvents();$script:setupPlan=Get-SetupPlan;Show-SetupPlan $setupPlan;$result=Invoke-InstallPlan $setupPlan;$c.review.Text=(L '  Установка завершена.' '  Installation complete.')+"`n`n"+(L '  В игре оставьте FSR 2 включённым и FSR 1 выключенным.' '  Keep in-game FSR 2 enabled and FSR 1 disabled.')+"`n"+(L '  Insert: настройки SR, NR и FG. Для отката используйте Uninstall.cmd.' '  Insert: SR, NR and FG controls. Use Uninstall.cmd for rollback.')+"`n`n  "+$result.manifest;$c.status.Text=L 'Готово. Можно закрыть установщик и запустить игру.' 'Done. Close setup and start the game.'}catch{$c.review.Text=Localize-SetupMessage $_.Exception.Message;$c.status.Text=L 'Установка остановлена. Подробности выше.' 'Installation stopped. Details are shown above.'}finally{Busy-Setup $false}})
Update-SetupText;Update-SetupChoice
if($CheckUi){
 # Exercise the real Check button and localization using detected hardware/game.
 # No install event is invoked. Useful for validating the GUI without a visible window.
 $form.ShowInTaskbar=$false;$form.StartPosition='Manual';$form.Location=New-Object Drawing.Point(-20000,-20000)
 $form.Show();[Windows.Forms.Application]::DoEvents();$c.check.PerformClick()
 if(!$script:setupPlan){throw ('GUI preflight failed: '+$c.review.Text)}
 $originalNrIndex=$c.nr.SelectedIndex
 foreach($index in @(2,1)){
  $c.nr.SelectedIndex=$index;$c.check.PerformClick()
  if(!$script:setupPlan -or (Get-SetupNrState $setupPlan) -ne ($index -eq 2)){throw 'NR startup choice did not match the generated configuration.'}
 }
 $c.nr.SelectedIndex=$originalNrIndex;$c.check.PerformClick()
 if(!$script:setupPlan){throw 'Could not restore the original UI preflight choice.'}
 [pscustomobject]@{passed=$true;language=$script:setupLang;profile=$setupPlan.profile.mode;nrSelectionsVerified=3;review=$c.review.Text;gameModified=$false}|ConvertTo-Json -Depth 4
 $form.Close()
}elseif($Preview){
 $c.review.Text=L "  ПРЕДПРОСМОТР — установка не выполняется.`n`n  Профиль: штатный FG · 2×`n  FSR 2 — ВКЛ. · FSR 1 — ВЫКЛ. · Панель: Insert`n  Предыдущие файлы и настройки сохраняются для отката." "  OFFLINE PREVIEW — no installation is performed.`n`n  Profile: native FG · 2x`n  FSR 2 ON · FSR 1 OFF · Panel: Insert`n  Previous files and settings are saved for rollback."
 $form.ShowInTaskbar=$false;$form.StartPosition='Manual';$form.Location=New-Object Drawing.Point(-20000,-20000)
 $form.CreateControl();$form.Show();[Windows.Forms.Application]::DoEvents();$image=New-Object Drawing.Bitmap($form.Width,$form.Height);$form.DrawToBitmap($image,(New-Object Drawing.Rectangle(0,0,$form.Width,$form.Height)));$image.Save([IO.Path]::GetFullPath($Preview),[Drawing.Imaging.ImageFormat]::Png);$image.Dispose();$form.Close()
}else{$null=$form.ShowDialog()}
$form.Dispose()
