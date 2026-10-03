"""Own-process game UI input checks; --case ime uses an installed Korean IME, not physical devices."""
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
user.GetClassNameW.argtypes = (wintypes.HWND, wintypes.LPWSTR, ctypes.c_int)
user.IsWindowVisible.argtypes = (wintypes.HWND,)
user.SendMessageW.argtypes = (wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM)
user.SendMessageW.restype = ctypes.c_ssize_t
user.GetKeyboardLayout.argtypes = (wintypes.DWORD,)
user.GetKeyboardLayout.restype = wintypes.HANDLE
user.GetKeyboardLayoutList.argtypes = (ctypes.c_int, ctypes.POINTER(wintypes.HANDLE))
user.PostMessageW.argtypes = (wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM)
user.CreateWindowExW.argtypes = (wintypes.DWORD, wintypes.LPCWSTR, wintypes.LPCWSTR, wintypes.DWORD,
    ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, wintypes.HWND, wintypes.HMENU, wintypes.HINSTANCE, wintypes.LPVOID)
user.CreateWindowExW.restype = wintypes.HWND
user.DestroyWindow.argtypes = (wintypes.HWND,)
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
        kind = ctypes.create_unicode_buffer(128)
        user.GetClassNameW(handle, kind, len(kind))
        # A process may also own hidden IME/console windows; they are never input targets.
        if owner.value == pid and kind.value == "MyEngineWindowClass" and user.IsWindowVisible(handle) and (not editor or "Play" in title.value):
            found.append(handle)
        return True
    user.EnumWindows(visit, 0)
    return found[0] if found else None

def require_focus(handle):
    foreground=user.GetForegroundWindow()
    if foreground != handle:
        owner=wintypes.DWORD(); user.GetWindowThreadProcessId(foreground,ctypes.byref(owner))
        kind=ctypes.create_unicode_buffer(128); user.GetClassNameW(foreground,kind,len(kind))
        raise AssertionError(f"Input refused: own window {handle} is not foreground (handle={foreground}, pid={owner.value}, class={kind.value})")

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

def tap(handle, code):
    key(handle, code, True)
    try: time.sleep(.08)
    finally: key(handle, code, False)
    time.sleep(.08)

def korean_layout(handle, process):
    count = user.GetKeyboardLayoutList(0, None)
    layouts = (wintypes.HANDLE * count)()
    count = user.GetKeyboardLayoutList(count, layouts)
    korean = next((layouts[i] for i in range(count) if layouts[i] & 0xffff == 0x0412), None)
    assert korean, "Korean keyboard/IME is not installed; no system language package will be added"
    thread = user.GetWindowThreadProcessId(handle, None)
    original = user.GetKeyboardLayout(thread)
    if original != korean:
        assert user.PostMessageW(handle, 0x0050, 0, korean)
        wait_for(lambda:user.GetKeyboardLayout(thread) == korean, process, "Own app rejected Korean input layout", 2)
    return original, korean

def committed_values(log):
    return [line.split("UI_IME_VALUE:",1)[1] for line in log.splitlines() if "[Lua] UI_IME_VALUE:" in line]

def last_committed(log):
    values=committed_values(log)
    return values[-1] if values else None

def check_auxiliary_window():
    auxiliary=user.CreateWindowExW(0,"STATIC","Play - Default IME",0x80000000,0,0,1,1,None,None,None,None)
    assert auxiliary, "Could not create the hidden native-window regression fixture"
    try:
        assert window(os.getpid(),False) is None and window(os.getpid(),True) is None, "Auxiliary window selected as a game input target"
    finally:
        assert user.DestroyWindow(auxiliary), "Could not destroy the owned auxiliary-window fixture"

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", choices=("Debug", "Release"), default="Release")
    parser.add_argument("--app", choices=("MyGame", "MyEditor", "all"), default="all")
    parser.add_argument("--case", choices=("clicks", "error", "text", "ime", "all"), default="all")
    args = parser.parse_args()
    check_auxiliary_window()
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
    entry=copy.deepcopy(button)
    entry.update(typeName="TextInput",name="test_entry")
    entry["anchors"].update(offMinX=400,offMinY=300,sizeX=240,sizeY=30)
    document["root"]["children"].append(entry)
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
    cleanup_errors = []
    try:
        for is_editor, exe in [(False,game),(True,editor)]:
            if args.app != "all" and args.app != exe.stem: continue
            # The normal gate stays independent of installed language packs.
            for mode in (["ime"] if args.case == "ime" else ["clicks","error","text"]):
                if args.case != "all" and mode != args.case: continue
                failure=mode=="error"
                name = exe.stem+"-"+mode
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
    assert(mye.ui.on_submit("test_entry",function(value)
        assert(value==EXPECTED_TEXT and mye.ui.get_text("test_entry")==value)
        assert(mye.ui.set_text("stats","입력: "..value))
        mye.log("UI_TEXT_SUBMIT")
    end))
    mye.log("UI_INPUT_READY")
end,on_update=function(self,dt)
    if self.modal then assert(not mye.input.is_action_pressed("move_right"),"MODAL_INPUT_LEAK") end
    if IS_TEXT and not self.received and mye.ui.get_text("test_entry")==EXPECTED_TEXT then
        self.received=true
        mye.log("UI_TEXT_RECEIVED")
    end
    if IS_IME then
        local value=assert(mye.ui.get_text("test_entry"))
        if value~=self.imeValue then self.imeValue=value; mye.log("UI_IME_VALUE:"..value) end
    end
end}
'''.replace('FAILURE','error("UI_INPUT_FAILURE")' if failure else '')
    .replace('EXPECTED_TEXT','"한글"' if mode=="ime" else '"한글😀"')
    .replace('IS_TEXT','true' if mode in ("text","ime") else 'false')
    .replace('IS_IME','true' if mode=="ime" else 'false'))
                ui.write_json(scene_path,scene)
                scene_hash = ui.digest(scene_path)
                capture = folder/(name+".bmp")
                command = [str(exe),"--project",str(manifest),"--dump",str(capture)] + (["--play","--frames","900"] if is_editor else ["--ticks","900" if mode=="ime" else "300"])
                log_path = folder/(name+".log")
                data_root = folder/"localappdata"/name
                with log_path.open("wb") as output:
                    process = subprocess.Popen(command,cwd=ui.ROOT,env=dict(os.environ,LOCALAPPDATA=str(data_root)),stdout=output,stderr=output)
                    handle = None
                    original_layout = None
                    ime_toggle_changed = False
                    ime_evidence = None
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
                        if mode=="ime":
                            original_layout, layout = korean_layout(handle, process)
                            click(handle,(450,310)); time.sleep(.1)
                            for letter in "gksrmf": tap(handle, ord(letter.upper()))
                            wait_for(lambda:last_committed(log()) in ("gksrmf","한"), process, name+": no Latin or Korean typing")
                            if last_committed(log()) == "gksrmf":
                                for _ in range(6): tap(handle, 0x08)
                                wait_for(lambda:last_committed(log()) == "", process, name+": probe text was not cleared")
                                tap(handle, 0x15) # Hangul mode toggle in the installed Korean layout.
                                ime_toggle_changed = True
                                for letter in "gksrmf": tap(handle, ord(letter.upper()))
                            wait_for(lambda:last_committed(log()) == "한", process, name+": preedit entered committed value")
                            assert "UI_TEXT_SUBMIT" not in log() and "UI_TEXT_RECEIVED" not in log()
                            tap(handle, 0x0D)
                            wait_for(lambda:"UI_TEXT_RECEIVED" in log(), process, name+": IME did not commit 한글")
                            assert "UI_TEXT_SUBMIT" not in log(), name+": composition Enter submitted text"
                            tap(handle, 0x0D)
                            wait_for(lambda:"UI_TEXT_SUBMIT" in log(), process, name+": committed Enter did not submit")
                            ime_evidence = dict(layout=hex(layout), preCommit="한", committed="한글", compositionEnterSubmitted=False)
                            if ime_toggle_changed:
                                tap(handle, 0x15)
                                ime_toggle_changed = False
                                tap(handle, 0x58)
                                wait_for(lambda:last_committed(log())=="한글x",process,name+": Latin mode not restored",2)
                                tap(handle,0x08)
                                wait_for(lambda:last_committed(log())=="한글",process,name+": restore probe not removed",2)
                            if original_layout != layout:
                                assert user.PostMessageW(handle, 0x0050, 0, original_layout)
                                thread=user.GetWindowThreadProcessId(handle,None)
                                wait_for(lambda:user.GetKeyboardLayout(thread)==original_layout,process,name+": layout not restored",2)
                            ime_evidence["modeAndLayoutRestored"] = True
                        elif mode=="text":
                            click(handle,(450,310)); time.sleep(.08)
                            require_focus(handle)
                            user.SendMessageW(handle,0x010D,0,0) # Start preedit; Enter must not submit it.
                            key(handle,0x0D,True)
                            try: time.sleep(.04)
                            finally: key(handle,0x0D,False)
                            require_focus(handle)
                            user.SendMessageW(handle,0x010E,0,0)
                            assert "UI_TEXT_SUBMIT" not in log(), "IME Enter submitted preedit"
                            encoded="한글😀".encode("utf-16-le")
                            for index in range(0,len(encoded),2):
                                require_focus(handle)
                                user.SendMessageW(handle,0x0102,int.from_bytes(encoded[index:index+2],"little"),0)
                            wait_for(lambda:"UI_TEXT_RECEIVED" in log(),process,name+": text did not reach UI")
                            key(handle,0x0D,True)
                            try: time.sleep(.04)
                            finally: key(handle,0x0D,False)
                            wait_for(lambda:"UI_TEXT_SUBMIT" in log(),process,name+": submit did not reach fixed Lua")
                        else: click(handle,(450,40))
                        if failure:
                            wait_for(lambda:"UI_INPUT_FAILURE" in log(),process,name+": callback did not fail")
                        elif mode=="clicks":
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
                            if mode in ("text","ime"): assert log().count("UI_TEXT_RECEIVED")==1 and log().count("UI_TEXT_SUBMIT")==1
                            else: assert log().count("UI_INPUT_OPEN")==1 and log().count("UI_INPUT_CLOSE")==1
                            assert "[ERROR]" not in log() and "MODAL_INPUT_LEAK" not in log()
                            assert capture.is_file()
                        assert ui.digest(meta)==metadata_hash and ui.digest(scene_path)==scene_hash
                        records.append(dict(name=name,command=command,binary=ui.digest(exe),exit=exit_code,
                                            capture=ui.digest(capture) if capture.exists() else None,scene=scene_hash,log=ui.digest(log_path),
                                            ime=ime_evidence))
                    finally:
                        try:
                            if handle and process.poll() is None and original_layout is not None:
                                if ime_toggle_changed:
                                    activate(handle)
                                    wait_for(lambda:user.GetForegroundWindow()==handle,process,name+": cannot restore own IME mode",2)
                                    tap(handle,0x15)
                                user.PostMessageW(handle,0x0050,0,original_layout)
                        except Exception as error:
                            cleanup_errors.append(dict(case=name,error=str(error)))
                        finally:
                            if process.poll() is None:
                                process.terminate(); process.wait(timeout=5)
        if cleanup_errors:
            raise AssertionError("IME cleanup failed; see cleanupErrors in report.json")
    except Exception as error:
        failure_detail = dict(case=current_case, error=str(error))
        raise
    finally:
        user.SetCursorPos(cursor.x,cursor.y)
        if original_foreground: user.SetForegroundWindow(original_foreground)
        ui.write_json(folder/"report.json",dict(passed=failure_detail is None, records=records, failure=failure_detail,
            cleanupErrors=cleanup_errors, requestedCase=args.case, requestedApp=args.app,
            metadataPreserved=ui.digest(meta)==metadata_hash,
            limits="Requires own foreground. Default cases use synthetic mouse/key and WM_CHAR/start/end. Optional ime case types via an installed Korean IME; no candidate-selection/cancel, physical-device/DPI or online certification. Focus loss may interrupt D; no raw D delivery guarantee."))
    print(f"PASS native UI input: {folder}")

if __name__ == "__main__":
    main()
