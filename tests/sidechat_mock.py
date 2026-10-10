"""Exercise the local script with a fake hyprctl and isolated state. Never real IPC."""
import json, os, pathlib, subprocess, tempfile, sys
source=pathlib.Path(sys.argv[1]).read_text()
with tempfile.TemporaryDirectory() as d:
    root=pathlib.Path(d)
    source=source.replace('/dev/shm/sidechat',str(root/'sidechat')).replace('/tmp/sidechat',str(root/'sidechat'))
    script=root/'sidechat.sh';script.write_text(source)
    fake=root/'hyprctl'
    fake.write_text('''#!/usr/bin/env python3
import json, os, sys
from pathlib import Path
r=Path(os.environ['MOCK_ROOT']);a=sys.argv[1:]
with (r/'calls').open('a') as f:f.write(json.dumps(a)+'\\n')
if a[0]=='clients': print((r/'clients').read_text())
elif a[0]=='monitors': print(json.dumps([dict(id=0,name='A',x=0,y=0,width=2000,height=1200,scale=2,transform=0,focused=True,activeWorkspace=dict(id=1)),dict(id=1,name='B',x=1000,y=0,width=2000,height=1200,scale=2,transform=0,focused=False,activeWorkspace=dict(id=2))]))
elif a[0]=='activeworkspace': print('{"id":1,"monitor":"A"}')
elif a[0]=='activewindow': print('{"address":"0xother"}')
elif a[0]=='dispatch': print('ok')
else: sys.exit(2)
''');fake.chmod(0o755)
    env=dict(os.environ,PATH=str(root)+':'+os.environ['PATH'],MOCK_ROOT=str(root))
    def run(action,clients,monitor=None):
        (root/'clients').write_text(json.dumps(clients));(root/'calls').write_text('')
        p=subprocess.run(['bash',str(script),action]+([monitor] if monitor else []),env=env,capture_output=True,text=True,timeout=10)
        assert p.returncode==0,(p.stdout,p.stderr)
        return [json.loads(s) for s in (root/'calls').read_text().splitlines()]
    c=dict(address='0xabc',class_='unused',workspace=dict(name='1'),monitor=0,pinned=False,floating=True)
    c['class']='ai-hub-firefox'
    for action,clients in [('hide',[]),('show',[c]),('hide',[dict(c,workspace=dict(name='special:sidechat_hidden'))])]:
        calls=run(action,clients);assert not any(a[0]=='dispatch' for a in calls),(action,calls)
    calls=run('hide',[c]);assert any(a[0]=='dispatch' and any('special:sidechat_hidden' in x and '0xabc' in x for x in a) for a in calls)
    calls=run('show',[dict(c,workspace=dict(name='special:sidechat_hidden'))],'B')
    assert any(a[0]=='dispatch' and any("workspace = '2'" in x and '0xabc' in x for x in a) for a in calls)
    calls=run('show',[c],'B');assert any(a[0]=='dispatch' and any("workspace = '2'" in x and '0xabc' in x for x in a) for a in calls)
    print('6 isolated sidechat idempotence, hide and target-monitor checks passed')
