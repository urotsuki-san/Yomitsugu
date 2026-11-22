"""Browser IME acceptance fixture for a disposable GitHub-hosted runner.

--fixture-smoke exercises the page in a fresh headless Chrome process.
--native uses real Windows input in the fresh test browser.
"""
import argparse
import contextlib
import ctypes
from ctypes import wintypes as wt
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import subprocess
import tempfile
import threading
import time
import winreg

HERE = Path(__file__).resolve().parent
CLSID = "{8F3A1C2E-4B5D-4E6F-8A9B-0C1D2E3F4A5B}"
FLAGS = ["--disable-extensions", "--disable-gpu", "--disable-background-networking",
         "--disable-component-update", "--no-first-run", "--no-default-browser-check",
         "--disable-sync", "--renderer-process-limit=2", "--disk-cache-size=1048576"]
CREATE = subprocess.CREATE_NO_WINDOW | subprocess.BELOW_NORMAL_PRIORITY_CLASS

def hosted_runner():
    return (os.environ.get("RUNNER_ENVIRONMENT")=="github-hosted"
            and os.environ.get("GITHUB_ACTIONS")=="true"
            and os.environ.get("GITHUB_REPOSITORY")=="urotsuki-san/Yomitsugu")

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def installed_tip():
    with winreg.OpenKey(winreg.HKEY_CLASSES_ROOT, "CLSID\\"+CLSID+"\\InProcServer32",
                        0, winreg.KEY_READ | winreg.KEY_WOW64_64KEY) as k:
        return Path(os.path.expandvars(winreg.QueryValueEx(k,None)[0])).resolve(strict=True)

@contextlib.contextmanager
def fixture_server():
    token = secrets.token_hex(16)
    state = {"sequence":-1}
    lock = threading.Lock()
    page = (HERE/"fields.html").read_bytes()
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *args): pass
        def do_GET(self):
            if self.path != "/"+token+"/?token="+token:
                self.send_error(404)
                return
            self.send_response(200)
            self.send_header("Content-Type","text/html; charset=utf-8")
            self.send_header("Content-Length",str(len(page)))
            self.end_headers()
            self.wfile.write(page)
        def do_POST(self):
            if self.path != "/"+token+"/events":
                self.send_error(404)
                return
            length=int(self.headers.get("Content-Length","0"))
            if not 0 < length < 150000:
                self.send_error(400)
                return
            payload=json.loads(self.rfile.read(length))
            with lock:
                if payload["sequence"]>=state["sequence"]:
                    state.clear()
                    state.update(payload)
            self.send_response(204)
            self.end_headers()
    local=ThreadingHTTPServer(("127.0.0.1",0),Handler)
    thread=threading.Thread(target=local.serve_forever,daemon=True)
    thread.start()
    def snapshot():
        with lock:
            return dict(state)
    try:
        yield "http://127.0.0.1:"+str(local.server_port)+"/"+token+"/?token="+token,snapshot
    finally:
        local.shutdown()
        thread.join()
        local.server_close()

def wait(predicate, message, timeout=12):
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        result=predicate()
        if result: return result
        time.sleep(.05)
    raise RuntimeError(message)

def foreground_pid():
    user=ctypes.windll.user32
    user.GetForegroundWindow.restype=wt.HWND
    hwnd=user.GetForegroundWindow()
    pid=wt.DWORD()
    user.GetWindowThreadProcessId.argtypes=[wt.HWND,ctypes.POINTER(wt.DWORD)]
    user.GetWindowThreadProcessId(hwnd,ctypes.byref(pid))
    return pid.value

class Keyboard(ctypes.Structure):
    _fields_=[("vk",wt.WORD),("scan",wt.WORD),("flags",wt.DWORD),("time",wt.DWORD),
              ("extra",ctypes.c_size_t)]
class Mouse(ctypes.Structure):
    _fields_=[("x",wt.LONG),("y",wt.LONG),("data",wt.DWORD),("flags",wt.DWORD),
              ("time",wt.DWORD),("extra",ctypes.c_size_t)]
class Hardware(ctypes.Structure):
    _fields_=[("msg",wt.DWORD),("low",wt.WORD),("high",wt.WORD)]
class InputData(ctypes.Union):
    _fields_=[("key",Keyboard),("mouse",Mouse),("hardware",Hardware)]
class Input(ctypes.Structure):
    _fields_=[("kind",wt.DWORD),("data",InputData)]

def key(pid,vk,scan,up):
    if foreground_pid()!=pid:
        raise RuntimeError("The isolated Chrome process lost foreground focus")
    item=Input()
    item.kind=1
    item.data.key=Keyboard(vk,scan,2 if up else 0,0,0)
    send=ctypes.windll.user32.SendInput
    send.argtypes=[wt.UINT,ctypes.POINTER(Input),ctypes.c_int]
    if send(1,ctypes.byref(item),ctypes.sizeof(Input))!=1:
        raise ctypes.WinError()
    time.sleep(.035)

def tap(pid,vk):
    key(pid,vk,0,False)
    key(pid,vk,0,True)

def mode_status():
    user=ctypes.windll.user32
    imm=ctypes.windll.imm32
    imm.ImmGetDefaultIMEWnd.argtypes=[wt.HWND]
    imm.ImmGetDefaultIMEWnd.restype=wt.HWND
    wnd=imm.ImmGetDefaultIMEWnd(user.GetForegroundWindow())
    if not wnd:return None
    result=ctypes.c_size_t()
    send=user.SendMessageTimeoutW
    send.argtypes=[wt.HWND,wt.UINT,wt.WPARAM,wt.LPARAM,wt.UINT,wt.UINT,ctypes.POINTER(ctypes.c_size_t)]
    if not send(wnd,0x283,5,0,2,300,ctypes.byref(result)):return None
    return result.value

def candidate_list(pid,include_hidden=False):
    # The production UI uses a standard LISTBOX. Read numeric state only.
    user=ctypes.windll.user32
    find=user.FindWindowExW
    find.argtypes=[wt.HWND,wt.HWND,wt.LPCWSTR,wt.LPCWSTR]
    find.restype=wt.HWND
    user.IsWindowVisible.argtypes=[wt.HWND]
    hwnd=0
    while hwnd:=find(None,hwnd,"ImeMixedCandidateWindow","変換候補"):
        owner=wt.DWORD()
        user.GetWindowThreadProcessId(hwnd,ctypes.byref(owner))
        visible=bool(user.IsWindowVisible(hwnd))
        if owner.value!=pid or (not visible and not include_hidden):continue
        box=find(hwnd,0,"LISTBOX",None)
        if not box:continue
        send=user.SendMessageTimeoutW
        send.argtypes=[wt.HWND,wt.UINT,wt.WPARAM,wt.LPARAM,wt.UINT,wt.UINT,ctypes.POINTER(ctypes.c_size_t)]
        count,idx=ctypes.c_size_t(),ctypes.c_size_t()
        if send(box,0x18b,0,0,2,200,ctypes.byref(count)) and send(box,0x188,0,0,2,200,ctypes.byref(idx)):
            return {"count":count.value,"selected":ctypes.c_ssize_t(idx.value).value,"visible":visible}
    return None

def observe_row(pid,index):
    try:
        return wait(lambda:(c:=candidate_list(pid)) and c["selected"]==index and c,"Candidate row not visible",2)
    except RuntimeError:
        return {"expected":index,"observed":candidate_list(pid,include_hidden=True),"visible":False}

def enter_commit(events,field,marker):
    # The only input sent in this interval is physical VK_RETURN via SendInput.
    # Blink queues compositionend; its isTrusted flag can be false even as the
    # surrounding native keydown, input and keyup have isTrusted=true.
    matching=[e for e in events if e["field"]==field]
    criteria=[lambda e:e["type"]=="keydown" and e["trusted"],
              lambda e:e["type"]=="input" and e["trusted"] and e["value"]==marker,
              lambda e:e["type"]=="compositionend" and e["data"]==marker and e["value"]==marker,
              lambda e:e["type"]=="keyup" and e["trusted"]]
    found=[]
    for event in matching:
        if criteria[len(found)](event):
            found.append(event)
            if len(found)==len(criteria):return found
    return None

def shift_key(pid,vk,scan):
    key(pid,0x10,0x2a,False)
    try:
        before=mode_status()
        key(pid,vk,scan,False)
        down=mode_status()
        key(pid,vk,scan,True)
        after=mode_status()
    finally:
        key(pid,0x10,0x2a,True)
    return [before,down,after,mode_status()]

def loaded_tip(pid,expected):
    ps = ("$ErrorActionPreference='Stop'; @(Get-Process -Id "+str(pid)+
          ").Modules | ForEach-Object { $_.FileName } | ConvertTo-Json -Compress")
    raw=subprocess.check_output(["powershell.exe","-NoProfile","-NonInteractive","-Command",ps],
                                creationflags=CREATE,timeout=10)
    modules=json.loads(raw)
    return any(os.path.normcase(str(Path(m).resolve()))==os.path.normcase(str(expected)) for m in modules)

def chrome_executable(specified):
    if specified:return Path(specified).resolve(strict=True)
    for base in (os.environ.get("ProgramFiles(x86)"),os.environ.get("ProgramFiles")):
        if base and (p:=Path(base)/"Google/Chrome/Application/chrome.exe").is_file():return p
    raise FileNotFoundError("Chrome executable")

def taskbar_audit(result_file,click=False):
    path=result_file.with_name("taskbar-click.json" if click else "taskbar.json")
    args=["powershell.exe","-NoProfile","-NonInteractive","-ExecutionPolicy","Bypass","-File",
          str(HERE/"taskbar.ps1"),"-Result",str(path)]
    if click:args.append("-Click")
    try:
        process=subprocess.run(args,creationflags=CREATE,capture_output=True,timeout=18)
        if process.returncode:
            path.with_suffix(".err").write_bytes(process.stderr+process.stdout)
            raise RuntimeError("taskbar helper exit "+str(process.returncode))
        return json.loads(path.read_text(encoding="utf-8-sig")).get("outcome")
    except (OSError,subprocess.SubprocessError,ValueError,RuntimeError) as error:
        print("Taskbar observation:",error,flush=True)
        return None

def test_taskbar(pid,snapshot,result_file):
    result={"action":"unavailable","opened_input":None}
    try:
        key(pid,0x10,0x2a,False)
        tap(pid,0x09)
        key(pid,0x10,0x2a,True)
        wait(lambda:snapshot()["active"]=="body","Textarea did not regain Tab focus",2)
        user=ctypes.windll.user32
        browser_window=user.GetForegroundWindow()
        action=taskbar_audit(result_file,click=True)
        result["action"]=action
        if action!="入力モードボタンをInvoke":return result
        if foreground_pid()!=pid:
            user.SetForegroundWindow.argtypes=[wt.HWND]
            user.SetForegroundWindow(browser_window)
        wait(lambda:foreground_pid()==pid and snapshot()["active"]=="body","Chrome did not regain focus",2)
        begin=len(snapshot()["events"])
        tap(pid,ord("K"))
        tap(pid,ord("A"))
        result["opened_input"]=bool(wait(lambda:any(e["type"]=="compositionstart" and e["trusted"]
            for e in snapshot()["events"][begin:]),"No native composition after clicking the mode button",2))
        tap(pid,0x1b)
    except Exception as error:
        result["error"]=str(error)
    return result

def smoke(chrome,profile):
    page=(HERE/"fields.html").resolve().as_uri()
    response=subprocess.run([chrome,"--headless=new","--dump-dom","--virtual-time-budget=1200",
                             "--user-data-dir="+str(profile),*FLAGS,page],capture_output=True,
                            cwd=HERE,timeout=35,creationflags=CREATE)
    rendered=response.stdout.decode("utf8","replace")
    ok=(response.returncode==0 and '<output id="status">fixture-ready</output>' in rendered)
    if not ok:raise RuntimeError("Chrome did not complete the fixture DOM startup")
    return {"mode":"headless fixture only","browser":str(chrome),"field_types":["search","textarea"],
            "focused":"query","native_keys":0,"ime_exercised":False}

def guest(chrome,profile,expected_hash,selector,result_file):
    tip=installed_tip()
    if sha(tip).lower()!=expected_hash.lower():
        raise RuntimeError("Registered TIP binary SHA-256 differs")
    marker="変換確認"+secrets.token_hex(4)
    custom=Path(os.environ["LOCALAPPDATA"])/"ImeMixed"/"user_dictionary.tsv"
    custom.parent.mkdir(parents=True,exist_ok=True)
    # Only a pristine guest dictionary may be used. Never replace existing entries.
    with custom.open("x",encoding="utf8") as f:
        f.write("よみつぐしけん\t"+marker+"\t名詞\n")
    try:
        with fixture_server() as (url,snapshot):
            p=subprocess.Popen([chrome,"--new-window","--start-maximized","--force-renderer-accessibility","--user-data-dir="+str(profile),*FLAGS,url],
                               cwd=HERE,creationflags=subprocess.BELOW_NORMAL_PRIORITY_CLASS)
            changes=[]
            selections=[]
            commits=[]
            stage="Chrome startup"
            try:
                wait(lambda:snapshot().get("active")=="query" and foreground_pid()==p.pid,
                     "The guest test browser did not gain focus")
                stage="profile selection"
                subprocess.run([str(selector),str(p.pid)],check=True,timeout=10)
                wait(lambda:loaded_tip(p.pid,tip),"The registered TIP is not loaded in this browser")
                taskbar_audit(result_file)
                for idx,field in enumerate(("query","body")):
                    stage=field+" direct after IME_OFF"
                    tap(p.pid,0x1A)  # VK_IME_OFF
                    start=len(snapshot().get("events",[]))
                    for v in "abc":tap(p.pid,ord(v.upper()))
                    direct=wait(lambda:(s:=snapshot())["values"][idx]=="abc" and s,"Direct input did not reach "+field)
                    if any(e["type"]=="compositionstart" for e in direct["events"][start:]):
                        raise RuntimeError("Unexpected composition during direct input in "+field)
                    for _ in "abc":tap(p.pid,0x08)
                    wait(lambda:snapshot()["values"][idx]=="","Direct text did not clear")
                    stage=field+" Shift+英数 OFF to ON"
                    changes.append({"field":field,"direction":"off-on","status":shift_key(p.pid,0xf0,0x3a)})
                    for v in "yomitsugushiken":tap(p.pid,ord(v.upper()))
                    wait(lambda:snapshot()["values"][idx]==marker,"Live marker did not reach "+field)
                    stage=field+" Tab candidate selection"
                    tap(p.pid,0x09)
                    if snapshot()["active"]!=field:raise RuntimeError("Candidate Tab moved focus")
                    tap(p.pid,0x09)
                    wait(lambda:(s:=snapshot())["active"]==field and s["values"][idx] not in ("",marker) and s,
                               "Second Tab did not choose another candidate")
                    chosen=observe_row(p.pid,1)
                    key(p.pid,0x10,0x2a,False)
                    tap(p.pid,0x09)
                    key(p.pid,0x10,0x2a,True)
                    wait(lambda:snapshot()["values"][idx]==marker,"Shift+Tab did not restore the first candidate")
                    restored=observe_row(p.pid,0)
                    selections.append({"field":field,"next":chosen,"previous":restored})
                    stage=field+" Enter"
                    start_enter=len(snapshot()["events"])
                    tap(p.pid,0x0D)
                    sequence=wait(lambda:enter_commit(snapshot()["events"][start_enter:],field,marker),
                                  "No complete native Enter commit sequence for "+field)
                    commits.append({"field":field,"events":sequence})
                    stage=field+" Shift+英数 ON to OFF"
                    changes.append({"field":field,"direction":"on-off","status":shift_key(p.pid,0xf0,0x3a)})
                    direct_start=len(snapshot()["events"])
                    tap(p.pid,ord("X"))
                    check=wait(lambda:(s:=snapshot())["values"][idx]==marker+"x" and s,"Direct x missing after toggle")
                    if any(e["type"].startswith("composition") for e in check["events"][direct_start:]):
                        raise RuntimeError("Unexpected composition after toggling OFF")
                    tap(p.pid,0x08)
                    stage=field+" Tab after commit"
                    tap(p.pid,0x09)
                    next_field="body" if idx==0 else "end"
                    wait(lambda:snapshot().get("active")==next_field,"Post-commit Tab did not move focus")
                last=snapshot()
                if last["values"]!=[marker,marker]:
                    raise RuntimeError("Both fields must contain the test dictionary result")
                for field in ("query","body"):
                    if not any(e["field"]==field and e["trusted"] and e["type"]=="compositionstart" for e in last["events"]):
                        raise RuntimeError("No trusted compositionstart for "+field)
                taskbar_result=test_taskbar(p.pid,snapshot,result_file)
                return {"mode":"hosted runner native","browser":str(chrome),"tip":str(tip),"tip_sha256":expected_hash,
                        "marker":marker,"verified_foreground_pid":p.pid,"shift_mode_transitions":changes,
                        "fixture_sha256":sha(HERE/"fields.html"),"enter_commits":commits,"keyboard_passed":True,
                        "candidate_list_passed":all(s["next"]["visible"] and s["previous"]["visible"] for s in selections),
                        "tab_candidates":selections,"taskbar":taskbar_result,"trace":last}
            except Exception as error:
                if result_file:
                    taskbar_audit(result_file)
                    result_file.write_text(json.dumps({"failed":stage,"error":str(error),"foreground":foreground_pid(),
                        "test_pid":p.pid,"mode_status":mode_status(),"candidates":candidate_list(p.pid),
                        "tab_candidates":selections,"enter_commits":commits,"shift_mode_transitions":changes,"trace":snapshot()},
                        ensure_ascii=False,indent=2),encoding="utf8")
                raise
            finally:
                # This is a separately launched browser with a fresh test profile. Gracefully
                # close that browser's own windows only; the guest may keep background services.
                user=ctypes.windll.user32
                cb_type=ctypes.WINFUNCTYPE(wt.BOOL,wt.HWND,wt.LPARAM)
                def close_owned(hwnd,_):
                    owner=wt.DWORD()
                    user.GetWindowThreadProcessId(hwnd,ctypes.byref(owner))
                    if owner.value==p.pid:
                        user.PostMessageW.argtypes=[wt.HWND,wt.UINT,wt.WPARAM,wt.LPARAM]
                        user.PostMessageW(hwnd,0x0010,0,0)
                    return True
                cb=cb_type(close_owned)
                user.EnumWindows(cb,0)
                try:p.wait(timeout=10)
                except subprocess.TimeoutExpired:p.terminate();p.wait(timeout=10)
    finally:
        if custom.read_text(encoding="utf8")=="よみつぐしけん\t"+marker+"\t名詞\n":custom.unlink()

def main():
    cli=argparse.ArgumentParser(description=__doc__)
    mode=cli.add_mutually_exclusive_group(required=True)
    mode.add_argument("--fixture-smoke",action="store_true")
    mode.add_argument("--native",action="store_true")
    cli.add_argument("--chrome");cli.add_argument("--expected-tip-sha256")
    cli.add_argument("--selector",type=Path)
    cli.add_argument("--result",type=Path)
    args=cli.parse_args()
    if args.native:
        if not hosted_runner():raise SystemExit("Native key test requires a GitHub-hosted test runner")
        if not args.result:cli.error("a result path is required")
        if not args.selector or not args.selector.is_file():cli.error("profile selector executable required")
        if not re.fullmatch("[0-9a-fA-F]{64}",args.expected_tip_sha256 or ""):
            cli.error("supply the SHA-256 of the intended candidate DLL")
    if args.result and args.result.exists():cli.error("result already exists")
    chrome=chrome_executable(args.chrome)
    with tempfile.TemporaryDirectory(prefix="chrome-fixture-",dir=None if args.native else HERE) as temp:
        profile=Path(temp)/"profile"
        result=smoke(chrome,profile) if args.fixture_smoke else guest(chrome,profile,args.expected_tip_sha256,args.selector.resolve(),args.result)
    output=json.dumps(result,indent=2,ensure_ascii=False)
    if args.result:args.result.write_text(output,encoding="utf8")
    else:print(output)
    if args.native and not result["candidate_list_passed"]:raise SystemExit("The expected candidate row was not visible")

if __name__=="__main__":main()
