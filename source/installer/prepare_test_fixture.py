"""Small non-executable fixtures with the real package layout for installer tests."""
from pathlib import Path
import hashlib,json,shutil
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
package=ROOT/'release/RENDER-EVIL-v0.2.0-preview.1-Smart-Installer'
fixture=HERE/'tests/fixture_package'
fixture.mkdir(parents=True,exist_ok=True)
catalog=json.loads((package/'catalog.json').read_text(encoding='utf-8-sig'))
for f in catalog['files']:
    out=fixture/f['source'];out.parent.mkdir(parents=True,exist_ok=True)
    data=('NONEXECUTABLE TEST PAYLOAD '+f['path']).encode()
    if f['path'].endswith('engine_command.json'):data=b'{"quality":1,"requested":false,"revision":0}'
    out.write_bytes(data);f['sha256']=hashlib.sha256(data).hexdigest().upper();f['bytes']=len(data)
catalog['supportedGameSha256']=hashlib.sha256(b'NONEXECUTABLE GAME HASH FIXTURE').hexdigest().upper()
for guard in catalog['guards']:
    guard['acceptedHashes']=[f['sha256'] for f in catalog['files'] if f['path']==guard['path']]
    if guard['path']=='dinput8.dll':guard['acceptedHashes'].append(hashlib.sha256(b'previous framework').hexdigest().upper())
(fixture/'catalog.json').write_text(json.dumps(catalog,indent=2)+'\n')
shutil.copytree(package/'defaults',fixture/'defaults',dirs_exist_ok=True)
print(fixture)
