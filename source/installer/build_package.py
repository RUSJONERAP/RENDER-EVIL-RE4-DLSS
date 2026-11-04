"""Build a new, offline installer preview. Does not publish or modify the game."""
from pathlib import Path
from datetime import datetime,timezone,timedelta
import hashlib,json,shutil,zipfile,re,sys
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
OUT=ROOT/'release/RENDER-EVIL-v0.2.0-preview.1-Smart-Installer'
FLAT_SOURCES='--nexus-flat-sources' in sys.argv
if FLAT_SOURCES:OUT=ROOT/'release/RENDER-EVIL-v0.2.0-preview.1-Nexus-FlatSources'
VERSION='0.2.0-preview.1'
BASE=ROOT/'release/RENDER-EVIL-0.1.0-rc.2_20261101_231309'
FG=ROOT/'engineering/fg_candidate_20261104'
LEGACY=ROOT/'third_party/dlssg-for-sm86-0.4.1/original'
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest().upper()
def copy(src,dst):
    dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dst)
def write(relative,value):
    dst=OUT/relative;dst.parent.mkdir(parents=True,exist_ok=True);dst.write_bytes(value.encode('utf-8'))
def ini_set(source,section,key,value):
    out=[];current='';seen=False;done=False
    for line in source.splitlines():
        m=re.match(r'\s*\[([^\]]+)\]',line)
        if m:
            if current.lower()==section.lower() and not done:out.append(f'{key}={value}');done=True
            current=m[1];seen |= current.lower()==section.lower()
        if current.lower()==section.lower() and re.match(r'\s*'+re.escape(key)+r'\s*=',line,re.I):
            if not done:out.append(f'{key}={value}');done=True
        else:out.append(line)
    if not done:
        if not seen:out.append(f'[{section}]')
        out.append(f'{key}={value}')
    return '\r\n'.join(out)+'\r\n'
# Output is a preview workspace, never the immutable published RC.2 ZIP.
OUT.mkdir(parents=True,exist_ok=True)
files=[]
def payload(src,target,group='base',preserve=False,mutable=False):
    relative='components/'+group+'/'+target
    copy(src,OUT/relative)
    files.append(dict(path=target,source=relative,group=group,sha256=sha(src),bytes=src.stat().st_size,preserveExisting=preserve,mutable=mutable))
for p in sorted((BASE/'payload').rglob('*')):
    if not p.is_file():continue
    rel=p.relative_to(BASE/'payload').as_posix()
    if rel in {'reframework/data/re4unified/ui.json','reframework/data/re4dlss/re4dlss.ini'}:continue
    if rel.startswith('reframework/data/re4unified/notices/'):continue
    if rel=='dinput8.dll':p=FG/'payload/dinput8.dll'
    if rel=='reframework/plugins/re4dlss.dll':p=FG/'payload/reframework/plugins/re4dlss.dll'
    payload(p,rel,preserve=rel.endswith('engine_command.json'),mutable=rel.endswith('engine_command.json'))
sdk=ROOT/'third_party/nvidia-streamline-2.14.16'
record=json.loads((sdk/'extracted_manifest.json').read_text())
for entry in record['files']:
    if sha(sdk/entry['name'])!=entry['sha256'].upper():raise RuntimeError('NVIDIA SDK dependency changed')
    payload(sdk/entry['name'],entry['target'],'fg')
marker=FG/'payload/reframework/data/re4unified/actual_fg.txt'
payload(marker,'reframework/data/re4unified/actual_fg.txt','fg')
payload(LEGACY/'alternatives/winmm.dll','winmm.dll','legacy')
# Adapter-only FG dependencies. INT8 SR and XeSS backends are deliberately absent.
for rel in ['backend/sm86_backend.dll','kernels/fg_kernels.dll','runtime/nvngx_dlssg.dll']:
    payload(LEGACY/'dlssg_sm86'/rel,'dlssg_sm86/'+rel,'legacy')
copy(LEGACY/'dlssg_sm86.ini',OUT/'defaults/dlssg_sm86.ini')
copy(BASE/'payload/reframework/data/re4dlss/re4dlss.ini',OUT/'defaults/re4dlss.ini')
defaults=(BASE/'defaults/OptiScaler.ini').read_text(encoding='utf-8-sig')
for section,key,value in [('DlssNr','Enabled','false'),('DlssNr','Preset','2'),('DlssNr','Intensity','1.0'),('DlssNr','WorkingScale','1.0'),('DlssNr','Passes','1'),('DlssNr','RunBeforeSR','false'),('DlssNr','CacheEnabled','false')]:defaults=ini_set(defaults,section,key,value)
write('defaults/OptiScaler.ini',defaults)
for p in HERE.iterdir():
    if p.name in {'Install.ps1','Install.cmd','Uninstall.ps1','Uninstall.cmd','Setup.ps1','Installer.Core.ps1'}:
        # Windows PowerShell 5.1 requires BOM to read Cyrillic source reliably.
        text=p.read_text(encoding='utf-8-sig').replace('\r\n','\n').replace('\n','\r\n')
        (OUT/p.name).write_bytes(text.encode('utf-8-sig' if p.suffix=='.ps1' else 'ascii'))
for p in (BASE/'licenses').rglob('*'):
    if p.is_file():copy(p,OUT/'licenses'/p.relative_to(BASE/'licenses'))
for name in ['ACTUAL-DLSS-PROVENANCE.txt']:
    copy(ROOT/'engineering/permissions'/name,OUT/'licenses'/name)
for p in sdk.glob('license_*'):copy(p,OUT/'licenses/Streamline'/p.name)
for name in ['LICENSE.md','THIRD_PARTY_NOTICES.txt','README.en.md','MANUAL.en.html']:
    copy(LEGACY/name,OUT/'licenses/dlssg-for-sm86'/name)
copy(ROOT/'engineering/permissions/ACTUAL_DLSS_PERMISSION.md',OUT/'docs/ACTUAL_DLSS_PERMISSION.md')
# Updated licenses/credits travel with the installed binaries.
for p in sorted((OUT/'licenses').rglob('*')):
    if p.is_file():payload(p,'reframework/data/re4unified/notices/licenses/'+p.relative_to(OUT/'licenses').as_posix())
credit=HERE/'CREDITS.txt';payload(credit,'reframework/data/re4unified/notices/CREDITS.txt')
# Include existing corresponding F5 source and our exact rendering/installer source.
if FLAT_SOURCES:
    # Nexus explicitly requires nested archives to be extracted for inspection.
    # Retain every source file and all runtime bytes; this does not resolve AV findings.
    source_root=OUT/'source/OptiScaler-fde33d31'
    with zipfile.ZipFile(ROOT/'third_party/downloads/alt_source.zip') as source_zip:
        for entry in source_zip.infolist():
            relative=Path(entry.filename.replace('\\','/'))
            if relative.is_absolute() or '..' in relative.parts or ':' in str(relative):raise RuntimeError('Unsafe source archive path')
            if entry.is_dir():continue
            if relative.suffix.lower() in {'.zip','.7z','.rar','.gz','.tar'}:raise RuntimeError('Nested source archive requires explicit review: '+str(relative))
            target=source_root/relative;target.parent.mkdir(parents=True,exist_ok=True)
            target.write_bytes(source_zip.read(entry))
    write('docs/NEXUS_PACKAGING_REVISION.txt','Packaging revision only: bundled F5 sources are extracted instead of nested in a ZIP, as required by Nexus. Runtime DLLs and installation behavior are unchanged. This does not establish safety or resolve the Bkav detection on the optional fg_kernels.dll. The original quarantined upload remains available for moderator review.\n')
else:
    copy(ROOT/'third_party/downloads/alt_source.zip',OUT/'source/OptiScaler-fde33d31.zip')
copy(BASE/'source/REFramework.patch',OUT/'source/REFramework-original-integration.patch')
for p in (ROOT/'engineering/REFramework/src/mods').glob('Unified*.hpp'):copy(p,OUT/'source/REFramework-overlay/src/mods'/p.name)
copy(ROOT/'engineering/REFramework/src/mods/ActualOptiBridge.hpp',OUT/'source/REFramework-overlay/src/mods/ActualOptiBridge.hpp')
for name in ['REFramework.cpp','D3D12Hook.cpp']:copy(ROOT/'engineering/REFramework/src'/name,OUT/'source/REFramework-overlay/src'/name)
for name in ['unified_framegen_test.cpp','unified_pacing_runtime_test.cpp']:copy(ROOT/'engineering/tests'/name,OUT/'source/tests'/name)
for p in HERE.iterdir():
    if p.suffix in {'.ps1','.py'}:copy(p,OUT/'source/installer'/p.name)
copy(ROOT/'engineering/unified_native.lua',OUT/'source/unified_native.lua')
for name in ['README_EN.md','README_RU.md','START_HERE.md']:
    copy(HERE/name,OUT/name)
for name in ['test_results.json','full_payload_roundtrip.json','ui_validation.json']:
    if (HERE/name).exists():copy(HERE/name,OUT/'docs'/name)
for lang in ['en','ru']:
    if (HERE/f'preview_{lang}.png').exists():copy(HERE/f'preview_{lang}.png',OUT/'previews'/f'OFFLINE_installer_{lang}.png')
expected={
 'version.dll':['7ABE6F92FECFF80C60C7598BFC524E11C207BB1E385A29EE0BF925853C7F737D'],
 'dinput8.dll':['2CE6D8A279925931758381932B4611028EC8CE5908A72B773B80586FD1271C9F','622B0473F948C982EC0905E242DFC2B8D8378CC9F4BCE63E9E29E6F5DA16999A','85365A1EC4BBAE8D0FDD643D198989C9BB240F8E06D685B0F662AE22C0E2B152'],
 'reframework/plugins/re4dlss.dll':['8488D097EED140F96AE8C0833F5B73F9318FAB0B978920C8A47D1044FA2DF4C8'],
}
for f in files:
    if f['path'] in ['version.dll','dinput8.dll','reframework/plugins/re4dlss.dll','winmm.dll','nvngx_dlss.dll','nvngx_dlssnr.dll','nvngx.dll_dlssnr.dll']:
        expected.setdefault(f['path'],[]).append(f['sha256'])
catalog=dict(schema=1,name='RE:NDER EVIL (DLSS SR/NR)',version=VERSION,created=datetime.now(timezone(timedelta(hours=3))).isoformat(),
 supportedGameSha256='A1082B154105FAC7CA22668FFC1C99A8BFC9F1B945439276C556EDEA7597A5B7',
 profiles={'off':'Base SR/NR, no FG libraries or adapter','native':'RTX 40/50, NVIDIA Streamline, default 2x','legacy':'Experimental RTX 20/30, sm86 0.4.1 winmm proxy, default 2x'},
 validation={'nativeRuntime':'RTX 5070 Ti: SR+NR+FG 2x, 3x generation observed','rtx40RuntimeTested':False,'legacyRuntimeTested':False},
 guards=[dict(path=k,acceptedHashes=sorted(set(v))) for k,v in expected.items()],files=files)
write('catalog.json',json.dumps(catalog,indent=2,ensure_ascii=False)+'\n')
checks=[]
for p in sorted(OUT.rglob('*')):
    if p.is_file() and p.name!='SHA256SUMS.txt':checks.append(sha(p)+'  '+p.relative_to(OUT).as_posix())
write('SHA256SUMS.txt','\n'.join(checks)+'\n')
if '--stage-only' in sys.argv:
    print(json.dumps(dict(directory=str(OUT),payloadFiles=len(files),staged=True),indent=2));sys.exit(0)
archive=Path(str(OUT)+'.zip')
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for p in sorted(OUT.rglob('*')):
        if p.is_file():z.write(p,Path(OUT.name)/p.relative_to(OUT))
with zipfile.ZipFile(archive) as z:
    bad=z.testzip()
    if bad:raise RuntimeError('ZIP CRC failure: '+bad)
result=dict(directory=str(OUT),archive=str(archive),sha256=sha(archive),sizeMiB=round(archive.stat().st_size/1048576,2),payloadFiles=len(files),published=False,gameModified=False)
(HERE/('nexus_flat_build_result.json' if FLAT_SOURCES else 'build_result.json')).write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
