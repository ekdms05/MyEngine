"""Actual local Play/MyGame saved-UI checks. No physical input, IME or online claim."""
import argparse
import copy
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import uuid
import zlib

ROOT = Path(__file__).resolve().parents[1]


def write_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run_app(exe, arguments, folder, name, expected=0):
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    environment = dict(os.environ, LOCALAPPDATA=str(folder / "localappdata"))
    result = subprocess.run([str(exe), *map(str, arguments)], cwd=ROOT, env=environment,
                            startupinfo=startup, capture_output=True, timeout=30)
    log = (result.stdout + result.stderr).decode("utf-8", errors="replace")
    (folder / (name + ".log")).write_text(log, encoding="utf-8")
    if result.returncode != expected or (expected == 0 and "[ERROR]" in log):
        raise AssertionError(f"{name}: exit={result.returncode}, expected={expected}; {folder}")
    return log


def bitmap(path):
    data = path.read_bytes()
    assert data[:2] == b"BM"
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bits = struct.unpack_from("<H", data, 28)[0]
    assert bits in (24, 32) and width > 0 and height != 0
    stride = (width * bits // 8 + 3) & ~3

    def pixel(x, y):
        row = abs(height) - 1 - y if height > 0 else y
        i = offset + row * stride + x * (bits // 8)
        b, g, r = data[i:i + 3]
        return r, g, b

    return width, abs(height), pixel


def prepare(folder):
    project = folder / "project"
    shutil.copytree(ROOT / "game/starter/meadow_village", project)
    manifest = project / "project.myeproj"
    scene_path = project / json.loads(manifest.read_text(encoding="utf-8-sig"))["mainScene"]
    scene = json.loads(scene_path.read_text(encoding="utf-8-sig"))
    # Suppress unrelated starter messages/controllers in this isolated display fixture.
    for entity in scene["entities"]:
        entity["components"].pop("ObjectBehavior", None)
        entity["components"].pop("InteractionTarget", None)
        entity["components"].pop("ScenePortal", None)
    entity = dict(id=max(e["id"] for e in scene["entities"]) + 1,
                  components={"ObjectName": dict(__version=1, value="HUD")})
    scene["entities"].append(entity)
    document = json.loads((ROOT / "docs/examples/hud.ui").read_text(encoding="utf-8"))
    icon = copy.deepcopy(document["root"]["children"][0]["children"][0])
    icon.update(typeName="Image", name="atlas_icon", children=[])
    icon["anchors"].update(offMinX=800, offMinY=20, sizeX=32, sizeY=32)
    guid = str(uuid.uuid4())
    icon["properties"] = [dict(__version=1,key="texture",value=guid),dict(__version=1,key="source",value="2,0,2,2")]
    document["root"]["children"].append(icon)
    # Four opaque 2x2 regions. No game art or external image library needed.
    rows = b"".join(b"\0" + b"".join(bytes((0,204,102,255)) if x >= 2 and y < 2 else bytes((204,0,0,255)) for x in range(4)) for y in range(4))
    def chunk(tag, data):
        return struct.pack(">I",len(data)) + tag + data + struct.pack(">I",zlib.crc32(tag+data))
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR",struct.pack(">IIBBBBB",4,4,8,6,0,0,0)) + chunk(b"IDAT",zlib.compress(rows)) + chunk(b"IEND",b"")
    (project / "assets/ui-fixture.png").write_bytes(png)
    write_json(project / "assets/ui-fixture.png.meta",dict(version=1,guid=guid,importer="TextureImporter",importerVersion=1,settings={}))
    source = folder / "hud.ui"
    write_json(source,document)
    return manifest, scene_path, scene, entity, document, source


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", choices=("Debug","Release"), default="Release")
    args = parser.parse_args()
    folder = ROOT / "build/game-ui" / uuid.uuid4().hex
    folder.mkdir(parents=True)
    manifest, scene_path, scene, entity, document, source = prepare(folder)
    editor = ROOT / f"build/dev/apps/editor/{args.config}/MyEditor.exe"
    game = ROOT / f"build/dev/apps/game/{args.config}/MyGame.exe"
    run_app(editor,["--project",manifest,"--import-asset",source,"--asset-destination","hud.ui","--headless","--frames","1"],folder,"import")
    metadata = manifest.parent / "assets/hud.ui.meta"
    guid = json.loads(metadata.read_text(encoding="utf-8"))["guid"]
    entity["components"]["GameUi"] = dict(__version=1,enabled=True,document=dict(guid=guid,type="0"))
    metadata_hash = digest(metadata)
    records = []
    for hp in (100,75,0):
        entity["components"]["ObjectBehavior"] = dict(__version=1,connections=[],luaSource=f'''return {{on_init=function(self)
assert(mye.ui.set_progress("hp",{hp},100))
assert(mye.ui.set_text("stats","한글 HP {hp} / 100"))
assert(mye.ui.set_enabled("attack",{str(hp > 0).lower()}))
assert(mye.ui.set_enabled("potion",{str(0 < hp < 100).lower()}))
mye.log("HUD_BOUND",{hp}) end}}''')
        write_json(scene_path,scene)
        scene_hash = digest(scene_path)
        for app, exe in (("MyGame",game),("MyEditor",editor)):
            for headless in (True,False):
                name=f"{app}-{hp}-{'headless' if headless else 'native'}"
                capture=folder / (name+".bmp")
                arguments=["--project",manifest,"--dump",capture]
                if headless: arguments.append("--headless")
                arguments += ["--play","--frames","180"] if app=="MyEditor" else ["--ticks","4"]
                log=run_app(exe,arguments,folder,name)
                assert "HUD_BOUND" in log and capture.is_file()
                width,height,pixel=bitmap(capture)
                scale=width//960
                assert scale>=1 and height==540*scale
                filled=round(296*hp/100)
                for x in range(296):
                    expected=(204,48,64) if x<filled else (52,24,32)
                    assert pixel((32+x)*scale,72*scale)==expected,(name,x,pixel((32+x)*scale,72*scale))
                assert pixel(808*scale,28*scale)==(0,204,102),name
                assert pixel(35*scale,207*scale)==((76,76,87) if hp>0 else (36,36,41)),name
                records.append(dict(name=name,command=[str(exe),*map(str,arguments)],binary=digest(exe),capture=digest(capture),hp=hp,size=[width,height]))
                assert digest(scene_path)==scene_hash and digest(metadata)==metadata_hash
    # Error cases use exactly the same app init path as successful saved UI.
    target=manifest.parent / "assets/hud.ui"
    valid=target.read_bytes()
    for case in ("corrupt","missing-png","bad-region","missing-ui"):
        if case=="corrupt": target.write_text("{}",encoding="utf-8")
        if case=="missing-png": (manifest.parent / "assets/ui-fixture.png").rename(manifest.parent / "assets/ui-fixture.png.saved")
        if case=="bad-region":
            bad=copy.deepcopy(document);bad["root"]["children"][-1]["properties"][1]["value"]="3,0,2,2";write_json(target,bad)
        if case=="missing-ui": target.rename(target.with_suffix(".ui.saved"))
        for app,exe in (("MyGame",game),("MyEditor",editor)):
            arguments=["--project",manifest,"--headless"]+(["--play","--frames","180"] if app=="MyEditor" else ["--ticks","4"])
            log=run_app(exe,arguments,folder,app+"-"+case,1)
            assert "UI" in log or "GameUi" in log,case
        if case=="missing-png": (manifest.parent / "assets/ui-fixture.png.saved").rename(manifest.parent / "assets/ui-fixture.png")
        if case=="missing-ui": target.with_suffix(".ui.saved").rename(target)
        target.write_bytes(valid)
    write_json(folder / "report.json",dict(records=records,negativeApps=8,metadataPreserved=digest(metadata)==metadata_hash,
        limits="Synthetic local state; native MyEditor dump is Play render target. No physical clicks/focus/IME/online/monitor-DPI claim."))
    print(f"PASS saved UI: {folder}")


if __name__ == "__main__":
    main()
