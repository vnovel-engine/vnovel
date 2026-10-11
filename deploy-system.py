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
import time

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

LOCAL_SO = os.path.expanduser("~/dev/vnovel/vnovel_libretro.so")
LOCAL_ENGINE_ONS = os.path.expanduser("~/dev/vnovel/vnovel_engine_ons.so")
LOCAL_ENGINE_PONS = os.path.expanduser("~/dev/vnovel/vnovel_engine_pons.so")
LOCAL_INFO = os.path.expanduser("~/dev/vnovel/vnovel_libretro.info")
LOCAL_RDB = os.path.expanduser("~/dev/vnovel/vnovel.rdb")
REMOTE_CORE = "/overlay/upper/usr/lib/libretro/vnovel_libretro.so"
REMOTE_ENGINE_ONS = "/overlay/upper/usr/lib/libretro/vnovel_engine_ons.so"
REMOTE_ENGINE_PONS = "/overlay/upper/usr/lib/libretro/vnovel_engine_pons.so"
REMOTE_INFO = "/overlay/upper/usr/lib/libretro/vnovel_libretro.info"
REMOTE_RDB = "/recalbox/share/system/.config/retroarch/database/rdb/vnovel.rdb"

SYSTEMLIST = "/recalbox/share/system/.emulationstation/systemlist.xml"

# The vnovel system XML to inject
SYSTEM_XML = '''  <system uuid="c2e3b2c6-2a11-49b4-9d90-1903b7a55488" name="vnovel" fullname="Visual Novels">
    <descriptor path="%ROOT%/vnovel" theme="vnovel" extensions="files:nscript.dat pscript.dat nscript.___" extension.types="*=file" icon="$F02D" downloader="0" port="0" readonly="0"/>
    <scraper screenscraper="0"/>
    <properties type="engine" pad="mandatory" keyboard="optional" mouse="optional" lightgun="no" releasedate="1999-01" manufacturer="07th Expansion / TYPE-MOON / various" retroachievements="0" crt.multiresolution="0" crt.multiregion="0" ignoredfiles=""/>
    <emulatorList>
      <emulator name="libretro">
        <core name="vnovel" priority="1" extensions="files:nscript.dat pscript.dat nscript.___" netplay="0" softpatching="0" compatibility="high" speed="high" crt.available="1" video.backend="default"/>
      </emulator>
    </emulatorList>
  </system>'''

def deploy_core(ssh):
    """Deploy the .so core to Recalbox."""
    if not os.path.exists(LOCAL_SO):
        print(f"Error: {LOCAL_SO} not found")
        return False

    print("Remounting root read-write...")
    ssh.exec_command("mount -o remount,rw /")
    time.sleep(0.5)

    sftp = ssh.open_sftp()
    print(f"Uploading core to {REMOTE_CORE}...")
    sftp.put(LOCAL_SO, REMOTE_CORE)
    sftp.chmod(REMOTE_CORE, 0o755)

    for local_engine, remote_engine, name in [
        (LOCAL_ENGINE_ONS, REMOTE_ENGINE_ONS, "ons"),
        (LOCAL_ENGINE_PONS, REMOTE_ENGINE_PONS, "pons"),
    ]:
        if os.path.exists(local_engine):
            print(f"Uploading {name} engine to {remote_engine}...")
            sftp.put(local_engine, remote_engine)
            sftp.chmod(remote_engine, 0o755)

    if os.path.exists(LOCAL_INFO):
        print(f"Uploading core info to {REMOTE_INFO}...")
        sftp.put(LOCAL_INFO, REMOTE_INFO)

    sftp.close()
    print("Remounting root read-only...")
    ssh.exec_command("mount -o remount,ro /")
    time.sleep(0.5)
    return True

def deploy_system(ssh):
    """Add the vnovel system to systemlist.xml."""
    sftp = ssh.open_sftp()

    with sftp.open(SYSTEMLIST, 'r') as f:
        content = f.read().decode()

    if 'name="vnovel"' in content:
        print("vnovel system already exists, replacing...")
        content = re.sub(
            r'\s*<system uuid="[^"]*" name="vnovel".*?</system>',
            '', content, flags=re.DOTALL
        )

    content = content.replace('</systemList>', SYSTEM_XML + '\n</systemList>')

    with sftp.open(SYSTEMLIST, 'w') as f:
        f.write(content)

    # The database the core's .info points at (database = "vnovel"). RetroArch
    # looks it up in <database dir>/rdb/vnovel.rdb; without it there is no
    # name lookup and the .info's database field resolves to nothing.
    sftp.close()
    print(f"Updated {SYSTEMLIST}")

    ssh.exec_command("mkdir -p /recalbox/share/roms/vnovel")
    time.sleep(0.5)
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
    """Restart EmulationStation to pick up changes."""
    print("Restarting EmulationStation...")
    ssh.exec_command("systemctl restart emulationstation.service 2>/dev/null || killall -9 emulationstation 2>/dev/null")

def main():
    do_core = "--system-only" not in sys.argv and "--db-only" not in sys.argv
    do_system = "--core-only" not in sys.argv and "--db-only" not in sys.argv
    do_db = "--core-only" not in sys.argv and "--system-only" not in sys.argv
    do_restart = "--restart" in sys.argv

    ssh = paramiko.SSHClient()
    ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    print(f"Connecting to {USER}@{HOST}...")
    ssh.connect(HOST, username=USER, password=PASS)

    if do_core:
        deploy_core(ssh)

    if do_system:
        deploy_system(ssh)

    if do_db:
        deploy_database(ssh)

    if do_restart:
        restart_es(ssh)

    ssh.close()

    if not do_restart:
        print("Done. Restart EmulationStation to apply changes (--restart to do it automatically).")
    else:
        print("Done!")

if __name__ == "__main__":
    main()
