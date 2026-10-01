#!/usr/bin/env /home/matcha/dev/onscripter-en-libretro/.venv/bin/python3
"""Deploy system definition and core to Recalbox live (no image flash needed).

Adds a new system to Recalbox's systemlist.xml and deploys the core .so.
This lets you test new systems without building a full Recalbox image.

Usage:
  deploy-system.py              Deploy core + system (no ES restart)
  deploy-system.py --restart    Also restart EmulationStation
  deploy-system.py --core-only  Only deploy the .so core
  deploy-system.py --system-only  Only deploy the system definition

Requires: paramiko (in .venv)
"""
import paramiko
import re
import sys
import os
import time

HOST = "recalbox.local"
USER = "root"
PASS = "recalboxroot"

LOCAL_SO = os.path.expanduser("~/dev/onscripter-en-libretro/onscripter-en_libretro.so")
LOCAL_ENGINE_ONS = os.path.expanduser("~/dev/onscripter-en-libretro/onscripter-en_engine_ons.so")
LOCAL_ENGINE_PONS = os.path.expanduser("~/dev/onscripter-en-libretro/onscripter-en_engine_pons.so")
LOCAL_INFO = os.path.expanduser("~/dev/onscripter-en-libretro/onscripter-en_libretro.info")
REMOTE_CORE = "/overlay/upper/usr/lib/libretro/onscripter-en_libretro.so"
REMOTE_ENGINE_ONS = "/overlay/upper/usr/lib/libretro/onscripter-en_engine_ons.so"
REMOTE_ENGINE_PONS = "/overlay/upper/usr/lib/libretro/onscripter-en_engine_pons.so"
REMOTE_INFO = "/overlay/upper/usr/lib/libretro/onscripter-en_libretro.info"

SYSTEMLIST = "/recalbox/share/system/.emulationstation/systemlist.xml"

# The vnovel system XML to inject
SYSTEM_XML = '''  <system uuid="c2e3b2c6-2a11-49b4-9d90-1903b7a55488" name="vnovel" fullname="Visual Novels">
    <descriptor path="%ROOT%/vnovel" theme="vnovel" extensions="files:nscript.dat pscript.dat nscript.___" extension.types="*=file" icon="$F02D" downloader="0" port="0" readonly="0"/>
    <scraper screenscraper="0"/>
    <properties type="engine" pad="mandatory" keyboard="optional" mouse="optional" lightgun="no" releasedate="1999-01" manufacturer="07th Expansion / TYPE-MOON / various" retroachievements="0" crt.multiresolution="0" crt.multiregion="0" ignoredfiles=""/>
    <emulatorList>
      <emulator name="libretro">
        <core name="onscripter-en" priority="1" extensions="files:nscript.dat pscript.dat nscript.___" netplay="0" softpatching="0" compatibility="high" speed="high" crt.available="1" video.backend="default"/>
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

    sftp.close()
    print(f"Updated {SYSTEMLIST}")

    ssh.exec_command("mkdir -p /recalbox/share/roms/vnovel")
    time.sleep(0.5)
    return True

def restart_es(ssh):
    """Restart EmulationStation to pick up changes."""
    print("Restarting EmulationStation...")
    ssh.exec_command("systemctl restart emulationstation.service 2>/dev/null || killall -9 emulationstation 2>/dev/null")

def main():
    do_core = "--system-only" not in sys.argv
    do_system = "--core-only" not in sys.argv
    do_restart = "--restart" in sys.argv

    ssh = paramiko.SSHClient()
    ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    print(f"Connecting to {USER}@{HOST}...")
    ssh.connect(HOST, username=USER, password=PASS)

    if do_core:
        deploy_core(ssh)

    if do_system:
        deploy_system(ssh)

    if do_restart:
        restart_es(ssh)

    ssh.close()

    if not do_restart:
        print("Done. Restart EmulationStation to apply changes (--restart to do it automatically).")
    else:
        print("Done!")

if __name__ == "__main__":
    main()
