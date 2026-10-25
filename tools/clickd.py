#!/usr/bin/env python3
"""clickd.py — watch a core's window and drive it, from a browser or curl.

Runs on the dev box beside a local RetroArch (`DISPLAY=:1` under Xvfb). Serves a
live view of the display and injects input back through uinput, so a human can
see the actual rendered frame and click/type into the running game — including
games that never advance on their own and whose title screens ignore synthetic
SDL events pushed straight into the engine.

Why uinput and not synthetic events into the engine
--------------------------------------------------
The core can push SDL key events into the engine's queue (see
`krkr_push_key`, and the `ONS_TEST_AUTODVANCE` injector). That path is real —
the events are drained by `krkr_pump_events` — but a KAG title screen sitting on
`@waitT`/`@waittrig` does not act on them, and no amount of re-pushing changes
that. uinput instead presents a *real* input device to the kernel: RetroArch's
udev joypad driver discovers it exactly as it would a plugged-in pad, and the
events travel the same route as a physical controller. That is the only path
that cannot be distinguished from a human.

Capture
-------
One long-lived ffmpeg does x11grab -> MJPEG. Spawn cost dominates a single grab,
so a persistent decoder is the only way to get live frames; the newest frame is
held in memory and served both as an MJPEG stream (/stream) and as a single PNG
(/grab.png, zero extra cost).

Endpoints
---------
    GET  /              live page (image + click-to-inject)
    GET  /stream        MJPEG stream (multipart/x-mixed-replace)
    GET  /grab.png      newest frame as PNG
    GET  /health        JSON: fps, frame age, uinput device state
    POST /key           {"key":"Return"|"Space"|"a"|...}      -> key tap
    POST /click         {"x":..,"y":..,"button":1|3}          -> abs pointer
    POST /pad           {"btn":"A"|"START"|"UP"|..., "action":"tap"|"down"|"up"}

Coordinates are in captured-image pixels; /click rescales to the pointer's
configured range, so the viewer can click the image directly at any size.

Run:  python3 tools/clickd.py [--display :1] [--port 8788] [--fps 15] [--size 1280x720]
"""
import argparse
import ctypes
import fcntl
import io
import json
import os
import selectors
import shutil
import signal
import struct
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

# uinput
# Kernel ABI, spelled out because stdlib has no uinput module. Layout and ioctl
# numbers are from linux/uinput.h and linux/input-event-codes.h.

EV_SYN, EV_KEY, EV_ABS = 0x00, 0x01, 0x03
SYN_REPORT = 0x00

UI_SET_EVBIT = 0x40045564
UI_SET_KEYBIT = 0x40045565
UI_SET_ABSBIT = 0x40045567
UI_DEV_CREATE = 0x5501
UI_DEV_DESTROY = 0x5502
UI_DEV_SETUP = 0x405c5503

# Identity of the virtual pad. Pinned so it is stable across restarts: a
# changing vendor/product makes RetroArch re-autodetect a different pad each
# time and forget the mapping.
PAD_NAME = "clickd virtual pad"
PAD_BUSTYPE, PAD_VENDOR, PAD_PRODUCT, PAD_VERSION = 0x03, 0x1234, 0x5678, 1


class UInputDevice:
    """Base for /dev/uinput virtual devices (stdlib: fcntl + struct)."""

    def __init__(self, name, bustype=PAD_BUSTYPE, vendor=PAD_VENDOR,
                 product=PAD_PRODUCT, version=PAD_VERSION):
        self.name, self.bustype = name, bustype
        self.vendor, self.product = vendor, product
        self.version = version
        self.fd = None
        self.ok = False
        self.error = None

    def _setup(self, keys, abs_caps):
        self.fd = os.open("/dev/uinput", os.O_WRONLY | os.O_NONBLOCK)
        fcntl.ioctl(self.fd, UI_SET_EVBIT, EV_KEY)
        fcntl.ioctl(self.fd, UI_SET_EVBIT, EV_SYN)
        for code in keys:
            fcntl.ioctl(self.fd, UI_SET_KEYBIT, code)
        if abs_caps:
            fcntl.ioctl(self.fd, UI_SET_EVBIT, EV_ABS)
            for code, lo, hi in abs_caps:
                fcntl.ioctl(self.fd, UI_SET_ABSBIT, code)
        try:
            class ID(ctypes.Structure):
                _fields_ = [("bustype", ctypes.c_uint16),
                            ("vendor", ctypes.c_uint16),
                            ("product", ctypes.c_uint16),
                            ("version", ctypes.c_uint16)]

            class SETUP(ctypes.Structure):
                _fields_ = [("id", ID),
                            ("name", ctypes.c_char * 80),
                            ("ff_effects_max", ctypes.c_uint32)]

            su = SETUP()
            su.id = ID(self.bustype, self.vendor, self.product, self.version)
            su.name = self.name.encode()[:79]
            fcntl.ioctl(self.fd, UI_DEV_SETUP, su)
        except Exception:
            # kernel < 4.5 has no UI_DEV_SETUP; the legacy write is equivalent.
            os.write(self.fd, struct.pack("80sHHHHi", self.name.encode()[:79],
                                          self.bustype, self.vendor,
                                          self.product, self.version, 0))
        fcntl.ioctl(self.fd, UI_DEV_CREATE)
        self.ok = True
        time.sleep(0.5)   # let udev settle before the first event

    def _emit(self, etype, code, value):
        os.write(self.fd, struct.pack("llHHi", 0, 0, etype, code, value))

    def _sync(self):
        self._emit(EV_SYN, SYN_REPORT, 0)

    def close(self):
        try:
            if self.fd is not None:
                fcntl.ioctl(self.fd, UI_DEV_DESTROY)
                os.close(self.fd)
                self.fd = None
        except Exception:
            pass


class VirtualKeyboard(UInputDevice):
    def __init__(self):
        super().__init__("clickd keyboard")

    def start(self, key_codes):
        try:
            self._setup(list(key_codes), None)
        except Exception as ex:
            self.error = str(ex)
            print("uinput keyboard unavailable (%s)" % ex)

    def tap(self, code, hold=0.05):
        if not self.ok:
            return False
        self._emit(EV_KEY, code, 1)
        self._sync()
        time.sleep(hold)
        self._emit(EV_KEY, code, 0)
        self._sync()
        return True


class VirtualGamepad(UInputDevice):
    ABS_X, ABS_Y, ABS_HAT0X, ABS_HAT0Y = 0x00, 0x01, 0x10, 0x11
    ABS_RX, ABS_RY = 0x03, 0x04
    STICK_MIN, STICK_MAX = -32768, 32767
    STICKS = ("LX", "LY", "RX", "RY")

    def __init__(self):
        super().__init__(PAD_NAME, PAD_BUSTYPE, PAD_VENDOR, PAD_PRODUCT,
                         PAD_VERSION)

    def start(self, btn_codes):
        caps = [(self.ABS_HAT0X, -1, 1), (self.ABS_HAT0Y, -1, 1),
                (self.ABS_X, self.STICK_MIN, self.STICK_MAX),
                (self.ABS_Y, self.STICK_MIN, self.STICK_MAX),
                (self.ABS_RX, self.STICK_MIN, self.STICK_MAX),
                (self.ABS_RY, self.STICK_MIN, self.STICK_MAX)]
        try:
            self._setup(list(btn_codes), caps)
        except Exception as ex:
            self.error = str(ex)
            print("uinput gamepad unavailable (%s)" % ex)

    def button(self, code, down):
        if not self.ok:
            return False
        self._emit(EV_KEY, code, 1 if down else 0)
        self._sync()
        return True

    def hat(self, axis, value):
        if not self.ok:
            return False
        self._emit(EV_ABS, axis, value)
        self._sync()
        return True

    def stick(self, which, axis, value=1.0):
        """Deflect an analog stick, for the core's pad->pointer synthesis.

        The paranoid path: a frontend with no mouse device still lets a game be
        clicked by moving the stick and pressing its button, because the core
        synthesizes RETRO_DEVICE_POINTER from the stick (see
        pointer_synth_input_state). up/left are negative; the kernel's ABS_*
        convention.
        """
        if not self.ok:
            return False
        code = {"LX": self.ABS_X, "LY": self.ABS_Y,
                "RX": self.ABS_RX, "RY": self.ABS_RY}.get((which or "LX").upper())
        if code is None:
            return False
        if axis in ("up", "left"):
            v = int(self.STICK_MIN * value)
        else:
            v = int(self.STICK_MAX * value)
        self._emit(EV_ABS, code, v)
        self._sync()
        return True

    def stick_centre(self):
        if not self.ok:
            return False
        for code in (self.ABS_X, self.ABS_Y, self.ABS_RX, self.ABS_RY):
            self._emit(EV_ABS, code, 0)
        self._sync()
        return True


class VirtualPointer(UInputDevice):
    """Absolute pointer, so a click on the served image maps 1:1.

    Absolute rather than relative: the viewer knows where on the captured frame
    the click landed, so there is no pointer-acceleration guessing.
    """

    BTN_LEFT, BTN_RIGHT = 0x110, 0x111
    ABS_X, ABS_Y = 0x00, 0x01

    def __init__(self, width, height):
        super().__init__("clickd pointer")
        self.w, self.h = width, height

    def start(self):
        try:
            self._setup([self.BTN_LEFT, self.BTN_RIGHT],
                        [(self.ABS_X, 0, self.w - 1), (self.ABS_Y, 0, self.h - 1)])
        except Exception as ex:
            self.error = str(ex)
            print("uinput pointer unavailable (%s)" % ex)

    def move(self, x, y):
        if not self.ok:
            return False
        self._emit(EV_ABS, self.ABS_X, max(0, min(self.w - 1, int(x))))
        self._emit(EV_ABS, self.ABS_Y, max(0, min(self.h - 1, int(y))))
        self._sync()
        return True

    def click(self, button, hold=0.06):
        if not self.ok:
            return False
        self._emit(EV_KEY, button, 1)
        self._sync()
        time.sleep(hold)
        self._emit(EV_KEY, button, 0)
        self._sync()
        return True


# input

KEY = {  # linux/input-event-codes.h, only what driving a VN needs
    "Return": 28, "Enter": 28, "Space": 57, "Escape": 1, "Backspace": 14,
    "Up": 103, "Down": 108, "Left": 105, "Right": 106,
    "a": 30, "b": 48, "f": 33, "q": 16, "s": 31, "o": 24, "v": 47,
    "h": 35, "H": 35, "n": 49, "p": 25, "r": 19, "l": 38, "g": 34, "c": 46,
    "0": 11, "1": 2, "2": 3, "3": 4, "Tab": 15,
    "PageUp": 104, "PageDown": 109, "LeftShift": 42, "LeftCtrl": 29,
}

# RetroPad buttons. Codes are BTN_* from input-event-codes.h.
BTN = {"A": 0x131,      # BTN_EAST
       "B": 0x130,      # BTN_SOUTH
       "X": 0x134,      # BTN_NORTH
       "Y": 0x133,      # BTN_WEST
       "L": 0x136, "R": 0x137,
       "SELECT": 0x13a, "START": 0x13b,
       "L2": 0x138, "R2": 0x139,
       "L3": 0x13d, "R3": 0x13e,
       "HOME": 0x13c, "HOTKEY": 0x13c}

HAT = {"UP": (VirtualGamepad.ABS_HAT0Y, -1),
       "DOWN": (VirtualGamepad.ABS_HAT0Y, 1),
       "LEFT": (VirtualGamepad.ABS_HAT0X, -1),
       "RIGHT": (VirtualGamepad.ABS_HAT0X, 1)}


class XInput:
    """Key/mouse injection through X (xdotool), for RetroArch's `x` input driver.

    Required when RetroArch runs the `x` input driver: it reads keys and pointer
    from X, not from /dev/uinput, so a uinput device is invisible to it (the
    uinput pad still works, because the joypad driver is udev and independent of
    the input driver). A uinput key delivered while the `x` driver is selected
    goes nowhere at all.

    There is no window manager on the Xvfb display, so nothing can be focused or
    activated — xdotool's --window path is unusable — but XTEST events
    (xdotool's default) go to whatever window is under the pointer and are
    received by a RetroArch window that has X input selected. No focus needed.
    """

    def __init__(self, display, width, height):
        self.display = display
        self.w, self.h = width, height
        self.ok = False
        self.error = None
        self.win = None

    def start(self):
        if not shutil.which("xdotool"):
            self.error = "xdotool not found"
            return
        try:
            r = subprocess.run(["xdotool", "search", "--class", "retroarch"],
                               env=dict(os.environ, DISPLAY=self.display),
                               stdout=subprocess.PIPE,
                               stderr=subprocess.DEVNULL, timeout=5)
            ids = r.stdout.split()
            self.win = ids[-1].decode() if ids else None
            self.ok = True
        except Exception as ex:
            self.error = str(ex)

    def _run(self, *args):
        if not self.ok:
            return False
        try:
            subprocess.run(["xdotool", *args],
                           env=dict(os.environ, DISPLAY=self.display),
                           stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL, timeout=5)
            return True
        except Exception:
            return False

    def key(self, name, hold=0.05):
        # xdotool takes X keysym names, not input-event-codes. The names needed
        # here are the same words, so pass through; 'a'..'z', 'Return', 'Space',
        # 'Escape', 'Up'.. all match.
        sym = {"Enter": "Return", "Esc": "Escape"}.get(name, name)
        ok = self._run("key", "--clearmodifiers", sym)
        if ok:
            time.sleep(hold)
        return ok

    def click(self, x, y, button=1):
        if not self.ok:
            return False
        self._run("mousemove", str(int(x)), str(int(y)))
        return self._run("click", str(int(button)))


class Input:
    """Facade over the virtual keyboard + gamepad + pointer.

    Two transports, chosen by what RetroArch is configured for:
      * X (xdotool) for keys and mouse — needed under the `x` input driver.
      * uinput for the pad — the joypad driver is udev and always reads it.
    Key/mouse prefer X when available, because that is the only transport the
    `x` input driver can see; uinput remains the fallback for a session running
    the udev input driver (where X events are not read).
    """

    def __init__(self, width, height, display=":1", prefer_x=True):
        self.kbd = VirtualKeyboard()
        self.pad = VirtualGamepad()
        self.ptr = VirtualPointer(width, height)
        self.x = XInput(display, width, height) if prefer_x else None

    def start(self):
        self.kbd.start(KEY.values())
        self.pad.start(BTN.values())
        self.ptr.start()
        if self.x is not None:
            self.x.start()

    def key(self, name, hold=0.05):
        if self.x is not None and self.x.ok:
            return self.x.key(name, hold)
        code = KEY.get(name)
        return self.kbd.tap(code, hold) if code is not None else False

    def button(self, name, action="tap", hold=0.05):
        name = name.upper()
        if name in HAT:
            axis, val = HAT[name]
            if action == "tap":
                self.pad.hat(axis, val)
                time.sleep(hold)
                # Neutralise BOTH axes: a pad latches the last direction and
                # will not report the same one twice until it sees a full
                # neutral report.
                self.pad.hat(VirtualGamepad.ABS_HAT0X, 0)
                self.pad.hat(VirtualGamepad.ABS_HAT0Y, 0)
                return True
            self.pad.hat(axis, val if action == "down" else 0)
            return True
        code = BTN.get(name)
        if code is None:
            return False
        if action == "tap":
            self.pad.button(code, True)
            time.sleep(hold)
            self.pad.button(code, False)
            return True
        return self.pad.button(code, action == "down")

    def click(self, x, y, button=1):
        """Click at an absolute position on the captured image.

        Under the `x` input driver the click must go through X, so prefer that
        transport when it is up; otherwise use the uinput pointer.
        """
        if self.x is not None and self.x.ok:
            return self.x.click(x, y, button)
        if self.ptr.ok and self.ptr.move(x, y):
            return self.ptr.click(VirtualPointer.BTN_RIGHT if button == 3
                                  else VirtualPointer.BTN_LEFT)
        return False

    def stick(self, which, axis, value=1.0, hold=0.25):
        """Deflect a stick then centre it — the pad-driven pointer route."""
        if not self.pad.ok:
            return False
        if not self.pad.stick(which, axis, value):
            return False
        time.sleep(hold)
        self.pad.stick_centre()
        return True

    def state(self):
        return {"keyboard": self.kbd.ok, "gamepad": self.pad.ok,
                "pointer": self.ptr.ok,
                "x": self.x.ok if self.x is not None else False,
                "errors": [e for e in (self.kbd.error, self.pad.error,
                                       self.ptr.error,
                                       self.x.error if self.x else None) if e]}

    def close(self):
        self.kbd.close()
        self.pad.close()
        self.ptr.close()


# capture

class Grabber:
    """Persistent ffmpeg x11grab -> MJPEG; newest frame held in memory.

    A single grab costs a process spawn (~hundreds of ms), so a live view is
    only possible with a long-lived decoder. Frames are split on the JPEG SOI
    marker; the newest complete one is what /grab.png and /stream serve.
    """

    def __init__(self, display, size, fps, auto_geometry=True):
        self.display, self.size, self.fps = display, size, fps
        self.auto_geometry = auto_geometry
        self.offset = "0,0"
        self.w, self.h = (int(x) for x in size.split("x"))
        self.frame = None
        self.frame_seq = 0
        self.frame_time = 0.0
        self.proc = None
        self.lock = threading.Lock()
        self._stop = threading.Event()

    def _detect_window(self):
        """Match the capture to the game window's geometry.

        Capturing the whole root when the game window is smaller puts empty X
        root around it — and on a black root that reads as solid black bars
        beside or below the picture, which looks exactly like a rendering bug.
        It cost a long detour chasing a 'text turns into black bars' report that
        was only ever x11grab grabbing past the window edge. So ask the window
        what it is and capture exactly that.

        Needs xdotool + xwininfo; if either is missing, fall back to the
        caller's geometry rather than failing.
        """
        if not self.auto_geometry:
            return
        if not (shutil.which("xdotool") and shutil.which("xwininfo")):
            return
        env = dict(os.environ, DISPLAY=self.display)
        try:
            ids = subprocess.run(["xdotool", "search", "--class", "retroarch"],
                                 env=env, stdout=subprocess.PIPE,
                                 stderr=subprocess.DEVNULL,
                                 timeout=5).stdout.split()
            if not ids:
                return
            win = ids[-1].decode()
            info = subprocess.run(["xwininfo", "-id", win],
                                  env=env, stdout=subprocess.PIPE,
                                  stderr=subprocess.DEVNULL,
                                  timeout=5).stdout.decode()
            def field(name):
                for line in info.splitlines():
                    if name in line:
                        return int(line.split(":")[1].strip())
                return None
            w = field("Width")
            h = field("Height")
            x = field("Absolute upper-left X")
            y = field("Absolute upper-left Y")
            if w and h and w > 0 and h > 0:
                self.w, self.h = w, h
                self.size = "%dx%d" % (w, h)
                self.offset = "%d,%d" % (x or 0, y or 0)
                print("clickd: captured window %s -> %s at %s"
                      % (win, self.size, self.offset))
        except Exception as ex:
            print("clickd: window geometry detection failed (%s); "
                  "using %s" % (ex, self.size))

    def start(self):
        self._detect_window()
        cmd = ["ffmpeg", "-hide_banner", "-loglevel", "error",
               "-f", "x11grab", "-draw_mouse", "0",
               "-video_size", self.size, "-framerate", str(self.fps),
               "-i", "%s+%s" % (self.display, self.offset),
               "-f", "mjpeg", "-q:v", "4", "-"]
        self.proc = subprocess.Popen(cmd, stdout=subprocess.PIPE,
                                     stderr=subprocess.DEVNULL, bufsize=0)
        threading.Thread(target=self._reader, daemon=True).start()

    def _reader(self):
        """Split the MJPEG byte stream into whole JPEG frames.

        ffmpeg emits concatenated JPEGs with no container framing, so frames
        are delimited by FFD8 (SOI) / FFD9 (EOI). A partial tail is kept until
        the next chunk completes it.
        """
        buf = b""
        while not self._stop.is_set():
            chunk = self.proc.stdout.read(65536)
            if not chunk:
                break
            buf += chunk
            while True:
                start = buf.find(b"\xff\xd8")
                if start < 0:
                    buf = b""
                    break
                end = buf.find(b"\xff\xd9", start + 2)
                if end < 0:
                    buf = buf[start:]   # keep the partial frame
                    break
                jpg = buf[start:end + 2]
                buf = buf[end + 2:]
                with self.lock:
                    self.frame = jpg
                    self.frame_seq += 1
                    self.frame_time = time.time()

    def stop(self):
        self._stop.set()
        if self.proc:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=3)
            except Exception:
                self.proc.kill()

    def snapshot(self):
        with self.lock:
            return self.frame, self.frame_seq, self.frame_time

    def wait_frame(self, prev_seq, timeout=5.0):
        """Block for a frame newer than prev_seq. Used by /stream so a viewer
        receives frames as they arrive instead of polling."""
        deadline = time.time() + timeout
        while time.time() < deadline and not self._stop.is_set():
            with self.lock:
                if self.frame_seq != prev_seq and self.frame is not None:
                    return self.frame, self.frame_seq
            time.sleep(0.01)
        return None, prev_seq


def to_png(jpg):
    """JPEG -> PNG via ImageMagick, so /grab.png serves what a VLM can read."""
    try:
        out = subprocess.run(["magick", "-", "png:-"], input=jpg,
                             stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                             timeout=5)
        if out.returncode == 0 and out.stdout:
            return out.stdout
    except Exception:
        pass
    return jpg   # fall back to the raw JPEG rather than 500


# page

PAGE = b"""<!doctype html><meta charset=utf-8>
<title>clickd</title>
<style>
 body{margin:0;background:#111;color:#ddd;font:13px system-ui}
 #bar{padding:6px 10px;display:flex;gap:14px;align-items:center;flex-wrap:wrap}
 #wrap{position:relative;display:inline-block}
 img{display:block;max-width:100vw;cursor:crosshair}
 .mark{position:absolute;width:14px;height:14px;margin:-7px 0 0 -7px;
       border:2px solid #0f0;border-radius:50%;pointer-events:none}
 button{background:#333;color:#ddd;border:1px solid #555;border-radius:4px;
        padding:3px 9px;cursor:pointer}
 button:hover{background:#444}
 #st{opacity:.7}
</style>
<div id=bar>
 <button onclick="send({key:'Space'})">Space</button>
 <button onclick="send({key:'Return'})">Return</button>
 <button onclick="send({key:'Escape'})">Esc</button>
 <button onclick="send({pad:'B'})">Advance (pad B)</button>
 <button onclick="send({pad:'A'})">Menu (pad A)</button>
 <button onclick="send({pad:'UP'})">Up</button>
 <button onclick="send({pad:'DOWN'})">Down</button>
 <button onclick="send({click:[320,240,1]})">Click centre</button>
 <button onclick="send({key:'h'})">h</button>
 <span id=st></span>
</div>
<div id=wrap><img id=v src="/grab.png"><div class=mark id=m style="display:none"></div></div>
<script>
const v=document.getElementById('v'), m=document.getElementById('m'), st=document.getElementById('st');
function send(o){fetch('/inject',{method:'POST',headers:{'Content-Type':'application/json'},
  body:JSON.stringify(o)}).then(r=>r.text()).then(t=>st.textContent=t).catch(e=>st.textContent=e)}
v.onclick=e=>{
  const r=v.getBoundingClientRect();
  const x=Math.round((e.clientX-r.left)*v.naturalWidth/r.width);
  const y=Math.round((e.clientY-r.top)*v.naturalHeight/r.height);
  m.style.display='block';
  m.style.left=(e.clientX-r.left)+'px'; m.style.top=(e.clientY-r.top)+'px';
  send({click:[x,y,e.button===2?3:1]});
};
v.oncontextmenu=e=>e.preventDefault();
// Poll the newest frame; the stream endpoint is available for a real viewer,
// but a browser tab behind others would otherwise queue frames.
setInterval(()=>{v.src='/grab.png?t='+Date.now()}, 200);
setInterval(()=>fetch('/health').then(r=>r.json()).then(h=>{
  st.textContent = h.fps+'fps  age='+h.frame_age_ms+'ms  pad='+h.uinput.gamepad
}).catch(()=>{}), 1000);
</script>
"""


# server

class Handler(BaseHTTPRequestHandler):
    grabber = None
    inp = None
    t0 = time.time()
    proto_version = "HTTP/1.1"

    def log_message(self, *a):
        pass   # quiet: the console is for the game's own output

    def _send(self, code, ctype, body):
        try:
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            # The viewer navigated away or the browser closed a prefetch. Not
            # an error: nothing to report and nothing to clean up.
            self.close_connection = True

    def _json(self, obj, code=200):
        self._send(code, "application/json", json.dumps(obj).encode())

    def do_GET(self):
        path = self.path.split("?")[0]
        if path in ("/", "/index.html"):
            self._send(200, "text/html; charset=utf-8", PAGE)
        elif path == "/grab.png":
            jpg, seq, _ = self.grabber.snapshot()
            if jpg is None:
                self._json({"error": "no frame yet"}, 503)
                return
            self._send(200, "image/png", to_png(jpg))
        elif path == "/stream":
            self.send_response(200)
            self.send_header("Content-Type",
                             "multipart/x-mixed-replace; boundary=frame")
            self.end_headers()
            seq = 0
            try:
                while True:
                    jpg, seq = self.grabber.wait_frame(seq, timeout=10)
                    if jpg is None:
                        break
                    self.wfile.write(b"--frame\r\nContent-Type: image/jpeg\r\n")
                    self.wfile.write(
                        ("Content-Length: %d\r\n\r\n" % len(jpg)).encode())
                    self.wfile.write(jpg)
                    self.wfile.write(b"\r\n")
                    self.wfile.flush()
            except (BrokenPipeError, ConnectionResetError):
                pass
        elif path == "/health":
            _, seq, ts = self.grabber.snapshot()
            age = int((time.time() - ts) * 1000) if ts else -1
            elapsed = time.time() - self.t0
            self._json({
                "fps": round(seq / elapsed, 1) if elapsed > 0 else 0,
                "frames": seq,
                "frame_age_ms": age,
                "uinput": self.inp.state(),
                "display": self.grabber.display,
                "size": self.grabber.size,
            })
        else:
            self._json({"error": "not found"}, 404)

    def do_POST(self):
        path = self.path.split("?")[0]
        n = int(self.headers.get("Content-Length") or 0)
        raw = self.rfile.read(n) if n else b"{}"
        try:
            body = json.loads(raw or b"{}")
        except Exception as ex:
            self._json({"error": "bad json: %s" % ex}, 400)
            return
        if path in ("/inject", "/key", "/click", "/pad"):
            self._json(self._dispatch(body))
        else:
            self._json({"error": "not found"}, 404)

    def _dispatch(self, body):
        """One handler for all input, so the page can post a mixed object and
        a script can post the same shape to /inject."""
        # explicit endpoint bodies
        if "key" in body and isinstance(body["key"], str) and "pad" not in body:
            ok = self.inp.key(body["key"], float(body.get("hold", 0.05)))
            return {"ok": ok, "did": "key %s" % body["key"]}
        if "btn" in body:
            ok = self.inp.button(str(body["btn"]),
                                 str(body.get("action", "tap")),
                                 float(body.get("hold", 0.05)))
            return {"ok": ok, "did": "pad %s" % body["btn"]}
        if "click" in body and isinstance(body["click"], list):
            x, y = int(body["click"][0]), int(body["click"][1])
            b = int(body["click"][2]) if len(body["click"]) > 2 else 1
            ok = self.inp.click(x, y, b)
            return {"ok": ok, "did": "click %d,%d b%d" % (x, y, b)}
        if "stick" in body:
            s = body["stick"]
            which, axis = (s[0], s[1]) if isinstance(s, list) else ("LX", "up")
            val = float(s[2]) if isinstance(s, list) and len(s) > 2 else 1.0
            ok = self.inp.stick(which, axis, val)
            return {"ok": ok, "did": "stick %s %s" % (which, axis)}
        if "pad" in body:
            ok = self.inp.button(str(body["pad"]),
                                 str(body.get("action", "tap")),
                                 float(body.get("hold", 0.05)))
            return {"ok": ok, "did": "pad %s" % body["pad"]}
        if "key" in body:
            ok = self.inp.key(str(body["key"]), float(body.get("hold", 0.05)))
            return {"ok": ok, "did": "key %s" % body["key"]}
        return {"ok": False, "error": "nothing to do"}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--display", default=":1")
    ap.add_argument("--port", type=int, default=8788)
    ap.add_argument("--fps", type=int, default=15)
    ap.add_argument("--size", default="1280x720")
    ap.add_argument("--host", default="0.0.0.0")
    args = ap.parse_args()

    grab = Grabber(args.display, args.size, args.fps)
    grab.start()
    print("clickd: capturing %s at %s (root %s)"
          % (args.display, grab.size, grab.offset))

    # The pointer's range must match what is captured, not the --size argument:
    # a click's coordinates arrive in captured-image pixels and are served to
    # RetroArch as an absolute position, so a mismatch scales every click.
    inp = Input(grab.w, grab.h, display=args.display)
    inp.start()
    print("clickd: uinput %s" % json.dumps(inp.state()))

    Handler.grabber, Handler.inp = grab, inp

    srv = ThreadingHTTPServer((args.host, args.port), Handler)
    srv.daemon_threads = True
    print("clickd: http://%s:%d/  (GET /grab.png, POST /inject)"
          % (args.host, args.port))

    def shutdown(*_):
        grab.stop()
        inp.close()
        srv.shutdown()

    signal.signal(signal.SIGINT, shutdown)
    signal.signal(signal.SIGTERM, shutdown)
    try:
        srv.serve_forever()
    finally:
        grab.stop()
        inp.close()


if __name__ == "__main__":
    sys.exit(main())
