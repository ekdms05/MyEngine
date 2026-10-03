"""Own-process native game UI clicks/keyboard checks; synthetic Win32 input, not IME/device QA."""
import argparse
import copy
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import subprocess
import time
import uuid
import importlib.util

spec = importlib.util.spec_from_file_location("saved_ui", Path(__file__).with_name("verify-game-ui.py"))
ui = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ui)
user = ctypes.WinDLL("user32", use_last_error=True)
user.GetForegroundWindow.restype = wintypes.HWND
user.GetWindowThreadProcessId.argtypes = (wintypes.HWND, ctypes.POINTER(wintypes.DWORD))
user.GetClientRect.argtypes = (wintypes.HWND, ctypes.POINTER(wintypes.RECT))
user.ClientToScreen.argtypes = (wintypes.HWND, ctypes.POINTER(wintypes.POINT))
user.SetForegroundWindow.argtypes = (wintypes.HWND,)
user.WindowFromPoint.argtypes = (wintypes.POINT,)
user.WindowFromPoint.restype = wintypes.HWND
user.GetAncestor.argtypes = (wintypes.HWND, wintypes.UINT)
user.GetAncestor.restype = wintypes.HWND
user.ShowWindow.argtypes = (wintypes.HWND, ctypes.c_int)
user.AttachThreadInput.argtypes = (wintypes.DWORD, wintypes.DWORD, wintypes.BOOL)
user.BringWindowToTop.argtypes = (wintypes.HWND,)
user.GetWindowTextW.argtypes = (wintypes.HWND, wintypes.LPWSTR, ctypes.c_int)
user.mouse_event.argtypes = (wintypes.DWORD, wintypes.DWORD, wintypes.DWORD, wintypes.DWORD, ctypes.c_size_t)
user.keybd_event.argtypes = (wintypes.BYTE, wintypes.BYTE, wintypes.DWORD, ctypes.c_size_t)
ENUM = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
user.EnumWindows.argtypes = (ENUM, wintypes.LPARAM)

def wait_for(predicate, process, message, seconds=8):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        result = predicate()
        if result:
            return result
        if process.poll() is not None:
            break
        time.sleep(.02)
    raise AssertionError(message)

def window(pid, editor):
    found = []
    @ENUM
    def visit(handle, _):
        owner = wintypes.DWORD()
        user.GetWindowThreadProcessId(handle, ctypes.byref(owner))
        title = ctypes.create_unicode_buffer(512)
        user.GetWindowTextW(handle, title, len(title))
        if owner.value == pid and (not editor or "Play" in title.value):
            found.append(handle)
        return True
    user.EnumWindows(visit, 0)
    return found[0] if found else None

def require_focus(handle):
    assert user.GetForegroundWindow() == handle, "Input refused: own game window is not foreground"

def activate(handle):
    if user.GetForegroundWindow() == handle: return
    # Attach only to our app thread, and always detach before injecting input.
    current = ctypes.WinDLL("kernel32").GetCurrentThreadId()
    target = user.GetWindowThreadProcessId(handle, None)
    attached = current != target and bool(user.AttachThreadInput(current,target,True))
    try:
        user.ShowWindow(handle,9)
        user.BringWindowToTop(handle)
        user.SetForegroundWindow(handle)
    finally:
        if attached: user.AttachThreadInput(current,target,False)

def click(handle, point):
    require_focus(handle)
    rect = wintypes.RECT()
    assert user.GetClientRect(handle, ctypes.byref(rect))
    width, height = rect.right, rect.bottom
    scale = max(1, min(width // 960, height // 540))
    position = wintypes.POINT((width - 960*scale)//2 + point[0]*scale,
                             (height - 540*scale)//2 + point[1]*scale)
    assert user.ClientToScreen(handle, ctypes.byref(position))
    user.SetCursorPos(position.x, position.y)
    time.sleep(.04)
    assert user.GetAncestor(user.WindowFromPoint(position),2)==handle, "Input refused: target point is covered or offscreen"
    activate(handle)
    deadline = time.monotonic()+.5
    while user.GetForegroundWindow()!=handle and time.monotonic()<deadline: time.sleep(.005)
    require_focus(handle)
    user.mouse_event(2, 0, 0, 0, 0)
    time.sleep(.04)
    user.mouse_event(4, 0, 0, 0, 0)

def key(handle, code, pressed):
    if pressed: require_focus(handle)
    user.keybd_event(code, user.MapVirtualKeyW(code, 0), 0 if pressed else 2, 0)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", choices=("Debug", "Release"), default="Release")
    parser.add_argument("--app", choices=("MyGame", "MyEditor", "all"), default="all")
    parser.add_argument("--case", choices=("clicks", "error", "all"), default="all")
    args = parser.parse_args()
    folder = ui.ROOT / "build/game-ui-input" / uuid.uuid4().hex
    folder.mkdir(parents=True)
    manifest, scene_path, scene, entity, document, source = ui.prepare(folder)
    button = copy.deepcopy(document["root"]["children"][0]["children"][-1])
    button.update(typeName="Button", name="test_open", children=[], properties=[])
    button["anchors"].update(anchorMinX=0,anchorMinY=0,anchorMaxX=0,anchorMaxY=0,pivotX=0,pivotY=0,
                             offMinX=400,offMinY=20,sizeX=120,sizeY=40)
    close = copy.deepcopy(button)
    close.update(name="test_close")
    close["anchors"].update(offMinX=20,offMinY=20)
    dialog = copy.deepcopy(button)
    dialog.update(typeName="Panel",name="test_dialog",children=[close],properties=[
        dict(__version=1,key=k,value=v) for k,v in [("modal","true"),("visible","false"),("background","true"),("tint","#203040")]])
    dialog["anchors"].update(offMinX=400,offMinY=120,sizeX=240,sizeY=120)
    document["root"]["children"] += [button,dialog]
    ui.write_json(source, document)
    editor = ui.ROOT / f"build/dev/apps/editor/{args.config}/MyEditor.exe"
    game = ui.ROOT / f"build/dev/apps/game/{args.config}/MyGame.exe"
    ui.run_app(editor,["--project",manifest,"--import-asset",source,"--asset-destination","hud.ui","--headless","--frames","1"],folder,"import")
    meta = manifest.parent/"assets/hud.ui.meta"
    metadata_hash = ui.digest(meta)
    entity["components"]["GameUi"] = dict(__version=1,enabled=True,document=dict(guid=json.loads(meta.read_text())["guid"],type="0"))
    original_foreground = user.GetForegroundWindow()
    cursor = wintypes.POINT(); user.GetCursorPos(ctypes.byref(cursor))
    records = []
    current_case = None
    failure_detail = None
    try:
        for is_editor, exe in [(False,game),(True,editor)]:
            if args.app != "all" and args.app != exe.stem: continue
            for failure in [False,True]:
                if args.case != "all" and failure != (args.case=="error"): continue
                name = ("MyEditor" if is_editor else "MyGame") + ("-error" if failure else "-clicks")
                current_case = name
                entity["components"]["ObjectBehavior"] = dict(__version=1,connections=[],luaSource='''
return {on_init=function(self)
    self.modal=false
    assert(mye.ui.on_click("test_open",function()
        FAILURE
        self.modal=true
        assert(mye.ui.set_visible("test_dialog",true))
        assert(mye.ui.set_progress("hp",75,100))
        mye.log("UI_INPUT_OPEN")
    end))
    assert(mye.ui.on_click("test_close",function()
        self.modal=false
        assert(mye.ui.set_visible("test_dialog",false))
        assert(mye.ui.set_progress("hp",0,100))
        mye.log("UI_INPUT_CLOSE")
    end))
    mye.log("UI_INPUT_READY")
end,on_update=function(self,dt)
    if self.modal then assert(not mye.input.is_action_pressed("move_right"),"MODAL_INPUT_LEAK") end
end}
'''.replace('FAILURE','error("UI_INPUT_FAILURE")' if failure else ''))
                ui.write_json(scene_path,scene)
                scene_hash = ui.digest(scene_path)
                capture = folder/(name+".bmp")
                command = [str(exe),"--project",str(manifest),"--dump",str(capture)] + (["--play","--frames","900"] if is_editor else ["--ticks","300"])
                log_path = folder/(name+".log")
                data_root = folder/"localappdata"/name
                with log_path.open("wb") as output:
                    process = subprocess.Popen(command,cwd=ui.ROOT,env=dict(os.environ,LOCALAPPDATA=str(data_root)),stdout=output,stderr=output)
                    try:
                        # Console redirection is buffered; the existing file sink is live.
                        def log():
                            path = next(data_root.rglob("engine.log"),None)
                            return path.read_text(encoding="utf-8",errors="replace") if path else ""
                        wait_for(lambda:"UI_INPUT_READY" in log(),process,name+": no fixed initialization")
                        handle = wait_for(lambda:window(process.pid,is_editor),process,name+": no own game window")
                        time.sleep(.2) # Let the editor's initial window/layout activation finish.
                        activate(handle)
                        wait_for(lambda:user.GetForegroundWindow()==handle,process,name+": no foreground",2)
                        click(handle,(450,40))
                        if failure:
                            wait_for(lambda:"UI_INPUT_FAILURE" in log(),process,name+": callback did not fail")
                        else:
                            wait_for(lambda:"UI_INPUT_OPEN" in log(),process,name+": click did not reach Lua")
                            key(handle,0x44,True)
                            try: time.sleep(.15)
                            finally: key(handle,0x44,False)
                            activate(handle)
                            wait_for(lambda:user.GetForegroundWindow()==handle,process,name+": focus after movement",2)
                            key(handle,0x0D,True)
                            try: time.sleep(.04)
                            finally: key(handle,0x0D,False)
                            wait_for(lambda:"UI_INPUT_CLOSE" in log(),process,name+": focused Enter did not reach Lua")
                        exit_code = process.wait(timeout=30)
                        assert exit_code == (1 if failure else 0), (name,exit_code)
                        if not failure:
                            assert log().count("UI_INPUT_OPEN")==1 and log().count("UI_INPUT_CLOSE")==1
                            assert "[ERROR]" not in log() and "MODAL_INPUT_LEAK" not in log()
                            assert capture.is_file()
                        assert ui.digest(meta)==metadata_hash and ui.digest(scene_path)==scene_hash
                        records.append(dict(name=name,command=command,binary=ui.digest(exe),exit=exit_code,
                                            capture=ui.digest(capture) if capture.exists() else None,scene=scene_hash,log=ui.digest(log_path)))
                    finally:
                        if process.poll() is None:
                            process.terminate(); process.wait(timeout=5)
    except Exception as error:
        failure_detail = dict(case=current_case, error=str(error))
        raise
    finally:
        user.SetCursorPos(cursor.x,cursor.y)
        if original_foreground: user.SetForegroundWindow(original_foreground)
        ui.write_json(folder/"report.json",dict(passed=failure_detail is None, records=records, failure=failure_detail,
            metadataPreserved=ui.digest(meta)==metadata_hash,
            limits="Requires foreground acquisition for this process. Synthetic OS mouse/key input; a focus loss can interrupt D, so no raw D delivery guarantee. No physical device, IME, online or monitor DPI certification."))
    print(f"PASS native UI input: {folder}")

if __name__ == "__main__":
    main()
