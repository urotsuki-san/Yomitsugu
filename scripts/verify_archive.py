from pathlib import Path
import zipfile, json, hashlib
root=Path('dist/yomitsugu-0.2.10-preview')
archive=Path(str(root)+'.zip')
with zipfile.ZipFile(archive) as z:
    assert z.testzip() is None
    entries={x.filename:x for x in z.infolist() if not x.is_dir()}
    manifest=json.loads(z.read(root.name+'/manifest.json').decode('utf-8-sig'))
    assert len(entries)==len(manifest['files'])+1
    for item in manifest['files']:
        name=root.name+'/'+item['path']
        assert entries[name].file_size==item['bytes'],name
        assert hashlib.sha256(z.read(name)).hexdigest().upper()==item['sha256'],name
    status={'archive':str(archive),'files_verified':len(entries),'crc':'passed','manifest_hashes':'passed','public_release_ready':manifest.get('public_release_ready')}
audit_dir=Path('audit/2026-09-25/release')
audit_dir.mkdir(parents=True, exist_ok=True)
(audit_dir/'archive_validation.json').write_text(json.dumps(status,indent=2),encoding='utf-8')
print(json.dumps(status))
