from __future__ import annotations
import json,os,subprocess,sys,shutil,time
from pathlib import Path

root=Path(__file__).resolve().parents[1]
source=json.loads((root/"runtime"/"generation_config.json").read_text(encoding="utf-8"))
work=root/"Tests"/".persistent-generation-test";shutil.rmtree(work,ignore_errors=True);requests=work/"requests";outputs=work/"outputs";requests.mkdir(parents=True)
try:
    log_path=work/"worker.log";log_stream=log_path.open("w",encoding="utf-8")
    process=subprocess.Popen([str(root/"runtime/python/Scripts/python.exe"),"-u",str(root/"backend/style_backend.py"),"serve",str(requests)],cwd=root/"backend",stdout=log_stream,stderr=subprocess.STDOUT,text=True,encoding="utf-8",errors="replace")
    try:
        for index,count in enumerate((2,1,1)):
            config=dict(source);config.update({"command":"generate","style_name":"PersistentTest","image_count":count,"width":256,"height":256,"steps":2,"seed":7000+index*10,"enable_safety_checker":index==2,"output_dir":str(outputs),"adapters":[{"type":"style","path":source["lora_path"],"strength":source["lora_strength"]}]})
            pending=requests/f".{index}.tmp";pending.write_text(json.dumps(config),encoding="utf-8");os.replace(pending,requests/f"{index}.json")
            expected=(2,3,4)[index];deadline=time.time()+180
            while len(list(outputs.rglob("*.png")))<expected and time.time()<deadline:
                if process.poll() is not None:break
                time.sleep(.2)
        comparison=dict(source);comparison.update({"command":"compare","image_count":1,"width":256,"height":256,"steps":2,"seed":7030,"enable_safety_checker":False,"output_dir":str(outputs),"comparisons":[{"name":"MyStyle","path":str(root/"Styles/MyStyle/MyStyle.safetensors"),"strength":1.0},{"name":"MyStyle_v2","path":str(root/"Styles/MyStyle_v2/MyStyle_v2.safetensors"),"strength":1.2}]})
        pending=requests/".comparison.tmp";pending.write_text(json.dumps(comparison),encoding="utf-8");os.replace(pending,requests/"comparison.json");deadline=time.time()+180
        while len(list(outputs.rglob("*.png")))<6 and time.time()<deadline:
            if process.poll() is not None:break
            time.sleep(.2)
        process.terminate();process.wait(timeout=15);log_stream.close();text=log_path.read_text(encoding="utf-8",errors="replace")
    finally:
        if process.poll() is None:process.kill()
        if not log_stream.closed:log_stream.close()
    images=list(outputs.rglob("*.png"));metadata=list(outputs.rglob("*.json"))
    if len(images)!=6 or len(metadata)!=6:print(text);raise SystemExit(f"expected 6 results, got {len(images)}")
    if "Reusing loaded base model." not in text:print(text);raise SystemExit("base model was not reused")
    seeds=sorted(json.loads(p.read_text(encoding="utf-8"))["seed"] for p in metadata)
    if seeds!=[7000,7001,7010,7020,7030,7030]:raise SystemExit(f"unexpected seeds: {seeds}")
    safety=sorted(json.loads(p.read_text(encoding="utf-8"))["enable_safety_checker"] for p in metadata)
    if safety!=[False,False,False,False,False,True]:raise SystemExit(f"unexpected safety flags: {safety}")
    styles=sorted(json.loads(p.read_text(encoding="utf-8"))["style_name"] for p in metadata)
    if "MyStyle" not in styles or "MyStyle_v2" not in styles:raise SystemExit(f"comparison styles missing: {styles}")
    print("Persistent backend, batch, safety ON/OFF, and Style comparison passed.")
finally:
    shutil.rmtree(work,ignore_errors=True)
