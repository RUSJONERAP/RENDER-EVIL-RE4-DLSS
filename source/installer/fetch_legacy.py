"""Fetch the pinned upstream release for the optional, untested older-GPU profile."""
from pathlib import Path,PurePosixPath
import hashlib,json,urllib.request,zipfile
root=Path(__file__).resolve().parents[2]
dest=root/'third_party/dlssg-for-sm86-0.4.1'
dest.mkdir(parents=True,exist_ok=True)
url='https://github.com/sdli1995/dlssg_for_sm86/releases/download/0.4.1/dlssg-release-0.4.1-x64.zip'
expected='7230131637617ede9dc45c552244ddb814ae70098406e102fc254d52bcdb6e14'
archive=dest/'dlssg-release-0.4.1-x64.zip'
if not archive.exists():
    temp=archive.with_suffix('.download')
    with urllib.request.urlopen(url,timeout=60) as src,temp.open('wb') as out:
        while chunk:=src.read(1024*1024):out.write(chunk)
    temp.replace(archive)
with archive.open('rb') as f:digest=hashlib.file_digest(f,'sha256').hexdigest()
if digest!=expected:raise RuntimeError('Upstream release checksum mismatch')
with zipfile.ZipFile(archive) as z:
    names=[n.replace('\\','/') for n in z.namelist() if not n.endswith('/')]
    records=[]
    for actual in z.namelist():
        normalized=actual.replace('\\','/')
        if normalized.endswith('/'):continue
        path=PurePosixPath(normalized)
        if path.is_absolute() or '..' in path.parts:raise RuntimeError('Unsafe archive path')
        output=dest/'original'/path;output.parent.mkdir(parents=True,exist_ok=True)
        data=z.read(actual);output.write_bytes(data)
        records.append(dict(path=normalized,bytes=len(data),sha256=hashlib.sha256(data).hexdigest()))
    if not any(n.endswith('alternatives/winmm.dll') for n in names):raise RuntimeError('Required real winmm proxy absent')
(dest/'provenance.json').write_text(json.dumps(dict(url=url,sha256=digest,version='0.4.1',commit='44f40064aed100ff1104a555e4d0bc3122086d64',files=records),indent=2)+'\n')
print(json.dumps(dict(archiveSha256=digest,fileCount=len(records),files=records),indent=2))
