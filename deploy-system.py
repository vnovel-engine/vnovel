#!/usr/bin/env python3
"""Deploy system definition and core to Recalbox live (no image flash needed).

Adds a new system to Recalbox's systemlist.xml and deploys the core .so.
This lets you test new systems without building a full Recalbox image.

Usage:
  deploy-system.py              Deploy core + system + database (no ES restart)
  deploy-system.py --restart    Also restart EmulationStation
  deploy-system.py --core-only  Only deploy the .so core
  deploy-system.py --system-only  Only deploy the system definition
  deploy-system.py --db-only    Only deploy the .rdb database

Requires: paramiko (in .venv)
"""
import paramiko
import re
import sys
import os
import subprocess
import time
import configparser

# Credentials are never committed. Provide them in a gitignored
# deploy-system.local.py (HOST/USER/PASS), or via the environment. See the
# deploy-system.local.py.example template.
try:
    from deploy_system_local import HOST, USER, PASS
except ImportError:
    HOST = os.environ.get("VNOVEL_DEPLOY_HOST", "recalbox.local")
    USER = os.environ.get("VNOVEL_DEPLOY_USER", "root")
    PASS = os.environ.get("VNOVEL_DEPLOY_PASS")

if not PASS:
    print("Error: no deploy password. Create deploy-system.local.py "
          "(copy deploy-system.local.py.example) or set VNOVEL_DEPLOY_PASS.")
    sys.exit(1)

# Artifacts come from this script's own repository, so a checkout anywhere
# works. Override with VNOVEL_LOCAL_DIR for a build staged elsewhere.
REPO_DIR = os.environ.get(
    "VNOVEL_LOCAL_DIR", os.path.dirname(os.path.abspath(__file__)))

LOCAL_SO = os.path.join(REPO_DIR, "vnovel_libretro.so")
LOCAL_INFO = os.path.join(REPO_DIR, "vnovel_libretro.info")
LOCAL_RDB = os.path.join(REPO_DIR, "vnovel.rdb")
REMOTE_DIR = "/usr/lib/libretro"
REMOTE_CORE = REMOTE_DIR + "/vnovel_libretro.so"
REMOTE_INFO = REMOTE_DIR + "/vnovel_libretro.info"

# Recalbox's RetroArch sets libretro_directory to ~/.config/retroarch/cores, and
# that is the directory it loads cores from — /usr/lib/libretro is only where the
# image ships them. Deploying to just the system directory left the core the
# frontend actually ran untouched, so a fix could land without ever taking
# effect (it did: an old onscripter-en_libretro.so from the image shadowed
# everything for days). Both are written; the frontend's copy is the one that
# matters and the system copy keeps an installed-tree deployment consistent.
REMOTE_CORES_DIR = "/recalbox/share/system/.config/retroarch/cores"
REMOTE_CORES_CORE = REMOTE_CORES_DIR + "/vnovel_libretro.so"
REMOTE_CORES_INFO = REMOTE_CORES_DIR + "/vnovel_libretro.info"
REMOTE_RDB = "/recalbox/share/system/.config/retroarch/database/rdb/vnovel.rdb"

SYSTEMLIST = "/recalbox/share/system/.emulationstation/systemlist.xml"
# The image ships the same file read-only; EmulationStation seeds the writable
# copy from it on a settings reset, so it must agree with SYSTEMLIST or the old
# core name comes back.
SYSTEMLIST_INIT = "/recalbox/share_init/system/.emulationstation/systemlist.xml"

# EmulationStation's EmulatorManager reads "<system>.core"/"<system>.emulator"
# from recalbox.conf and uses them in preference to the systemlist default. The
# core name it yields is fed to configgen, which builds the core path as
# /usr/lib/libretro/<core>_libretro.so (libretroCores.py / recalboxFiles.py) —
# a hardcoded directory, not libretro_directory. Both facts bit us: a stale
# "vnovel.core=onscripter-en" sent the frontend looking for a core that no
# longer existed, and the path it looked in was /usr/lib/libretro regardless of
# where the core was deployed.
RECALBOX_CONF = "/recalbox/share/system/recalbox.conf"
CORE_NAME = "vnovel"

# The vnovel system XML to inject.
#
# DERIVED FROM system.ini, not duplicated here. A hardcoded copy had TWO wrong
# extension lists (both missing data.xp3), and a deploy therefore replaced a
# correct systemlist.xml entry with one that hid every KiriKiri game from
# EmulationStation. The .ini is the source of truth; read it at deploy time.
#
# The extensions matter more than they look: a system definition is the only
# place that can name a FULL FILENAME, which is what tells nscript.dat apart
# from, say, a MAME `.dat`. The core's .info cannot do that -- RetroArch matches
# only the text after the last dot (core_info.c, core_info_does_support_file:
# `string_list_find_elem(list, ext + 1)`), so a `nscript.dat` entry there would
# reduce to `dat`.
SYSTEM_INI_CANDIDATES = [
    os.environ.get("VNOVEL_SYSTEM_INI", ""),
    os.path.expanduser(
        "~/dev/recalbox/package/recalbox-romfs2/systems/vnovel/system.ini"),
]


def _system_xml_from_ini(path):
    """Build the <system> element the way Recalbox's systembuilder.py does.

    Field mapping copied from package/recalbox-romfs2/scripts/{systemholder,
    systembuilder}.py so a deploy and an image agree. Only the fields this
    system actually uses are emitted; a new one must be added to both.
    """
    # RawConfigParser, not ConfigParser: the values contain `%ROOT%` (a Recalbox
    # placeholder), which ConfigParser's interpolation rejects.
    ini = configparser.RawConfigParser()
    ini.read(path)

    def get(section, key, default=""):
        return ini.get(section, key, fallback=default).strip().strip('"')

    def flag(section, key):
        return "1" if get(section, key, "0").lower() in ("1", "true", "yes", "on") else "0"

    def esc(s):
        return (s.replace("&", "&amp;").replace('"', "&quot;")
                 .replace("<", "&lt;").replace(">", "&gt;"))

    core_sections = sorted(
        (s for s in ini.sections() if s.startswith("core.")),
        key=lambda s: int(s.split(".", 1)[1]) if s.split(".", 1)[1].isdigit() else 0)

    # The descriptor's extension list is the UNION across cores, with `files:`
    # hoisted to the front (systemholder.py:410-417).
    exts = []
    for sec in core_sections:
        for e in get(sec, "extensions").split():
            if e not in exts:
                exts.append(e)
    ext_str = " ".join(exts)
    if "files:" in ext_str:
        ext_str = "files:" + ext_str.replace("files:", "")

    lines = []
    lines.append(
        f'  <system uuid="{esc(get("system","uuid"))}" '
        f'name="{esc(get("system","name"))}" '
        f'fullname="{esc(get("system","fullname"))}">')
    # port/readonly are emitted ONLY for a port system (systembuilder.py:104-106);
    # a normal system omits them, and emitting them made a deploy's XML differ
    # from the image's for no reason.
    descriptor = (
        f'    <descriptor path="{esc(get("system","roms.folder"))}" '
        f'theme="{esc(get("system","theme.folder"))}" '
        f'extensions="{esc(ext_str)}" '
        f'extension.types="{esc(get("system","extension.types"))}" '
        f'icon="{esc(get("system","icon.unicode"))}" downloader="0"')
    if flag("system", "port") == "1":
        descriptor += f' port="1" readonly="{flag("system","readonly")}"'
    descriptor += "/>"
    lines.append(descriptor)
    lines.append(
        f'    <scraper screenscraper="{esc(get("system","screenscraper.id","0"))}"/>')
    lines.append(
        f'    <properties type="{esc(get("properties","type"))}" '
        f'pad="{esc(get("properties","device.pad"))}" '
        f'keyboard="{esc(get("properties","device.keyboard"))}" '
        f'mouse="{esc(get("properties","device.mouse"))}" '
        f'lightgun="{esc(get("properties","device.lightgun"))}" '
        f'releasedate="{esc(get("properties","release.date"))}" '
        f'manufacturer="{esc(get("properties","manufacturer"))}" '
        f'retroachievements="{flag("properties","retroachievements")}" '
        f'crt.multiresolution="{flag("properties","crt.multiresolution")}" '
        f'crt.multiregion="{flag("properties","crt.multiregion")}" '
        f'ignoredfiles="{esc(get("properties","ignoredfiles"))}"/>')

    by_emulator = {}
    for sec in core_sections:
        by_emulator.setdefault(get(sec, "emulator", "libretro"), []).append(sec)

    lines.append('    <emulatorList>')
    for emulator, sections in by_emulator.items():
        lines.append(f'      <emulator name="{esc(emulator)}">')
        for sec in sections:
            lines.append(
                f'        <core name="{esc(get(sec,"core"))}" '
                f'priority="{esc(get(sec,"priority"))}" '
                f'extensions="{esc(get(sec,"extensions"))}" '
                f'netplay="{flag(sec,"netplay")}" '
                f'softpatching="{flag(sec,"softpatching")}" '
                f'compatibility="{esc(get(sec,"compatibility"))}" '
                f'speed="{esc(get(sec,"speed"))}" '
                f'crt.available="{flag(sec,"crt.available")}" '
                f'video.backend="{esc(get(sec,"video.backend","default"))}"/>')
        lines.append('      </emulator>')
    lines.append('    </emulatorList>')
    lines.append('  </system>')
    return "\n".join(lines), ext_str


def load_system_xml():
    """(xml, extensions) from the first system.ini that exists, or (None, None)."""
    for path in SYSTEM_INI_CANDIDATES:
        if path and os.path.exists(path):
            xml, exts = _system_xml_from_ini(path)
            print(f"System definition read from {path}")
            print(f"  extensions: {exts}")
            return xml, exts
    return None, None


def _remote_arch(ssh):
    """'aarch64', 'x86_64', ... — or None if uname is unavailable."""
    try:
        _, out, _ = ssh.exec_command("uname -m")
        return out.read().decode().strip() or None
    except Exception:
        return None


def _local_arch(path):
    """The ELF machine of a local file, via readelf; None if unknown."""
    try:
        out = subprocess.run(["readelf", "-h", path],
                             capture_output=True, text=True, timeout=10).stdout
        for line in out.splitlines():
            if "Machine:" in line:
                return line.split(":", 1)[1].strip()
    except Exception:
        pass
    return None


def _norm_arch(machine):
    """Normalise readelf's 'Machine:' / uname's -m to a comparable form."""
    m = (machine or "").lower()
    if "aarch64" in m or "arm64" in m:
        return "aarch64"
    if "x86-64" in m or "x86_64" in m:
        return "x86_64"
    return m


def deploy_core(ssh):
    """Deploy the single self-contained core.

    The core is one file: the runtime and both engines are linked in, so
    nothing else has to land beside it.

    The core is architecture-checked before it is uploaded: uploading a core
    built for another platform silently replaces a good one and fails at
    load time with no obvious cause.
    """
    required = (LOCAL_SO,)
    missing = [p for p in required if not os.path.exists(p)]
    if missing:
        print("Error: build artifacts missing:")
        for p in missing:
            print(f"  {p}")
        print("Run 'make' first.")
        return False

    remote_arch = _norm_arch(_remote_arch(ssh))
    if remote_arch:
        local_arch = _norm_arch(_local_arch(LOCAL_SO))
        if local_arch and local_arch != remote_arch:
            print(f"Error: architecture mismatch — target is {remote_arch}, "
                  f"core is {local_arch}")
            print("Rebuild for the target (make platform="
                  f"{'aarch64' if remote_arch == 'aarch64' else 'unix'}).")
            return False
        print(f"Architecture check: core is {remote_arch} (ok)")

    print("Remounting root read-write...")
    ssh.exec_command("mount -o remount,rw /")
    time.sleep(0.5)

    sftp = ssh.open_sftp()
    to_send = [
        (LOCAL_SO, REMOTE_CORE, True),
        (LOCAL_INFO, REMOTE_INFO, False),
        (LOCAL_SO, REMOTE_CORES_CORE, True),
        (LOCAL_INFO, REMOTE_CORES_INFO, False),
    ]
    for local_path, remote_path, required in to_send:
        if not os.path.exists(local_path):
            if required:
                print(f"Error: {local_path} missing")
                sftp.close()
                return False
            print(f"Skipping {os.path.basename(local_path)} (not built)")
            continue
        name = os.path.basename(local_path)
        # Upload beside the target and rename, so a running frontend never
        # loads a half-written core.
        tmp = remote_path + ".new"
        print(f"Uploading {name}...")
        sftp.put(local_path, tmp)
        # The SD-card share is vfat/exfat: it carries no POSIX mode bits, so
        # chmod there fails with EACCES even though the write succeeded. The
        # mode only matters where it is meaningful, so a failure here is not
        # fatal — the rename below still has to happen.
        try:
            sftp.chmod(tmp, 0o755 if name.endswith(".so") else 0o644)
        except IOError:
            pass
        ssh.exec_command(f"mv -f {tmp} {remote_path}")
        time.sleep(0.2)

    sftp.close()
    print("Remounting root read-only...")
    ssh.exec_command("mount -o remount,ro /")
    time.sleep(0.5)
    return True

def deploy_emulator_selection(ssh):
    """Pin <system>.core/<system>.emulator in recalbox.conf.

    Without this the frontend falls back to whatever the user last selected (or
    to the image default, which named a core that no longer exists). Set both
    keys idempotently; leave every other line untouched.
    """
    print("Remounting root read-write...")
    ssh.exec_command("mount -o remount,rw /")
    time.sleep(0.5)

    sftp = ssh.open_sftp()
    try:
        with sftp.open(RECALBOX_CONF, 'r') as f:
            lines = f.read().decode().splitlines()
    except IOError:
        print(f"Error: {RECALBOX_CONF} not found")
        sftp.close()
        return False

    wanted = {f"{CORE_NAME}.core": CORE_NAME, f"{CORE_NAME}.emulator": "libretro"}
    seen = set()
    out = []
    for line in lines:
        key = line.split('=', 1)[0] if '=' in line else None
        if key in wanted:
            new = f"{key}={wanted[key]}"
            if line != new:
                print(f"Rewriting: {line!r} -> {new!r}")
            out.append(new)
            seen.add(key)
        else:
            out.append(line)
    for key, value in wanted.items():
        if key not in seen:
            print(f"Appending: {key}={value}")
            out.append(f"{key}={value}")

    content = "\n".join(out)
    if not content.endswith("\n"):
        content += "\n"
    with sftp.open(RECALBOX_CONF, 'w') as f:
        f.write(content)
    sftp.close()

    ssh.exec_command("mount -o remount,ro /")
    time.sleep(0.5)
    print(f"Updated {RECALBOX_CONF}")
    return True


def deploy_system(ssh):
    """Add the vnovel system to systemlist.xml."""
    system_xml, _exts = load_system_xml()
    if not system_xml:
        print("ERROR: no system.ini found; tried:\n  "
              + "\n  ".join(p for p in SYSTEM_INI_CANDIDATES if p)
              + "\nSet VNOVEL_SYSTEM_INI to override.", file=sys.stderr)
        return False

    sftp = ssh.open_sftp()

    with sftp.open(SYSTEMLIST, 'r') as f:
        content = f.read().decode()

    if 'name="vnovel"' in content:
        print("vnovel system already exists, replacing...")
        content = re.sub(
            r'\s*<system uuid="[^"]*" name="vnovel".*?</system>',
            '', content, flags=re.DOTALL
        )

    content = content.replace('</systemList>', system_xml + '\n</systemList>')

    with sftp.open(SYSTEMLIST, 'w') as f:
        f.write(content)

    # The database the core's .info points at (database = "vnovel"). RetroArch
    # looks it up in <database dir>/rdb/vnovel.rdb; without it there is no
    # name lookup and the .info's database field resolves to nothing.
    sftp.close()
    print(f"Updated {SYSTEMLIST}")

    deploy_systemlist_init(ssh)

    ssh.exec_command("mkdir -p /recalbox/share/roms/vnovel")
    time.sleep(0.5)
    return True

def deploy_systemlist_init(ssh):
    """Re-point the read-only systemlist template at the current core name.

    EmulationStation copies this over SYSTEMLIST when settings are reset, so a
    stale core name here silently reintroduces the bug. Rewritten in place only
    when it still mentions the old name.
    """
    print("Remounting root read-write...")
    ssh.exec_command("mount -o remount,rw /")
    time.sleep(0.5)

    sftp = ssh.open_sftp()
    try:
        with sftp.open(SYSTEMLIST_INIT, 'r') as f:
            content = f.read().decode()
    except IOError:
        print(f"Skipping {SYSTEMLIST_INIT} (not present)")
        sftp.close()
        ssh.exec_command("mount -o remount,ro /")
        return True

    if f'<core name="{CORE_NAME}"' in content and 'onscripter-en' not in content:
        sftp.close()
        ssh.exec_command("mount -o remount,ro /")
        return True

    content = re.sub(
        r'<core name="onscripter-en"', f'<core name="{CORE_NAME}"', content)
    with sftp.open(SYSTEMLIST_INIT, 'w') as f:
        f.write(content)
    sftp.close()

    ssh.exec_command("mount -o remount,ro /")
    time.sleep(0.5)
    print(f"Updated {SYSTEMLIST_INIT}")
    return True

def deploy_database(ssh):
    """Upload the vnovel .rdb so the scanner can name games by fingerprint."""
    if not os.path.exists(LOCAL_RDB):
        print(f"WARNING: {LOCAL_RDB} not found; build it with "
              f"'python3 tools/make-rdb.py dat/vnovel.dat vnovel.rdb'")
        return False

    print("Remounting root read-write...")
    ssh.exec_command("mount -o remount,rw /")
    time.sleep(0.5)

    sftp = ssh.open_sftp()
    dbdir = os.path.dirname(REMOTE_RDB)
    ssh.exec_command(f"mkdir -p {dbdir}")
    time.sleep(0.5)
    print(f"Uploading database to {REMOTE_RDB}...")
    sftp.put(LOCAL_RDB, REMOTE_RDB)
    sftp.chmod(REMOTE_RDB, 0o644)
    sftp.close()

    print("Remounting root read-only...")
    ssh.exec_command("mount -o remount,ro /")
    time.sleep(0.5)
    return True

def restart_es(ssh):
    """Restart EmulationStation to pick up changes.

    Recalbox has no systemd; EmulationStation is supervised by the S31 init
    script. killall alone leaves it down, so start it explicitly.
    """
    print("Restarting EmulationStation...")
    ssh.exec_command(
        "killall -TERM emulationstation 2>/dev/null; sleep 3; "
        "/etc/init.d/S31emulationstation start >/dev/null 2>&1 &")

def main():
    do_core = "--system-only" not in sys.argv and "--db-only" not in sys.argv
    do_system = "--core-only" not in sys.argv and "--db-only" not in sys.argv
    do_db = "--core-only" not in sys.argv and "--system-only" not in sys.argv
    do_restart = "--restart" in sys.argv

    ssh = paramiko.SSHClient()
    ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    print(f"Connecting to {USER}@{HOST}...")
    ssh.connect(HOST, username=USER, password=PASS)

    failed = False

    if do_core:
        failed |= not deploy_core(ssh)

    if do_system:
        failed |= not deploy_system(ssh)

    if do_system or do_core:
        failed |= not deploy_emulator_selection(ssh)

    if do_db:
        failed |= not deploy_database(ssh)

    if failed:
        print("One or more steps failed; nothing else was attempted.")
        ssh.close()
        return 1

    if do_restart:
        restart_es(ssh)

    ssh.close()

    if not do_restart:
        print("Done. Restart EmulationStation to apply changes (--restart to do it automatically).")
    else:
        print("Done!")

if __name__ == "__main__":
    sys.exit(main())
