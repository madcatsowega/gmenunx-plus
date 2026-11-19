#!/usr/bin/env python3
"""
retrofw-usb - Fast USB Ethernet (FTP + Telnet) file transfer & deploy CLI for RetroFW 2.3 (RS-97 Plus).

Usage on RS-97 Plus:
  Plug USB cable into PC and select "Charger" (enables USB networking on 169.254.1.1).

Path shortcuts for push/pull/ls:
  home:  -> /home/retrofw/
  roms:  -> /home/retrofw/ROMS/
  apps:  -> /home/retrofw/apps/
  emu:   -> /home/retrofw/emu/
  sd2:   -> /media/mmcblk1p1/
"""

import argparse
import ftplib
import os
import select
import socket
import subprocess
import sys
import time

DEFAULT_HOST = os.environ.get("RETROFW_HOST", "169.254.1.1")
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SHORTCUTS = {
    "home:": "/home/retrofw/",
    "roms:": "/home/retrofw/ROMS/",
    "apps:": "/home/retrofw/apps/",
    "emu:": "/home/retrofw/emu/",
    "sd2:": "/media/mmcblk1p1/",
}


def expand_remote(path: str) -> str:
    if not path:
        return "/home/retrofw"
    for prefix, target in SHORTCUTS.items():
        if path.startswith(prefix):
            rest = path[len(prefix):].lstrip("/")
            return target.rstrip("/") + ("/" + rest if rest else "")
    if not path.startswith("/"):
        return "/home/retrofw/" + path
    return path


def human_size(n: int) -> str:
    for unit in ("B", "KB", "MB", "GB"):
        if n < 1024 or unit == "GB":
            return f"{n:.1f} {unit}" if unit != "B" else f"{n} B"
        n /= 1024.0
    return f"{n} B"


def check_port(host: str, port: int, timeout: float = 1.5) -> bool:
    try:
        with socket.create_connection((host, port), timeout=timeout):
            return True
    except OSError:
        return False


def print_usb_setup_hint(host: str) -> None:
    print(f"\nCannot reach RetroFW at {host}.")
    print("Checklist:")
    print("  1. On the RS-97 Plus, plug in USB and pick 'Charger' (or enable USB network).")
    print("     (You should see the small network icon appear in GMenuNX's status bar.)")
    try:
        out = subprocess.check_output(["ip", "-o", "link", "show"], text=True)
        usb_ifaces = []
        for line in out.splitlines():
            parts = line.split(":")
            if len(parts) >= 2:
                name = parts[1].strip()
                if name.startswith(("usb", "enx", "enp0s")) or "usb" in name.lower():
                    usb_ifaces.append(name)
        if usb_ifaces:
            print(f"  2. Detected candidate USB interface(s): {', '.join(usb_ifaces)}")
            iface = usb_ifaces[0]
            print(f"     If your PC didn't auto-assign 169.254.1.x, run:")
            print(f"       sudo ip addr replace 169.254.1.2/24 dev {iface} && sudo ip link set {iface} up")
        else:
            print("  2. No USB network interface (usb0 / enx*) detected yet on this PC.")
    except Exception:
        pass


def ftp_connect(host: str, timeout: float = 8.0) -> ftplib.FTP:
    ftp = ftplib.FTP()
    ftp.connect(host, 21, timeout=timeout)
    try:
        ftp.login()  # anonymous (BusyBox ftpd default)
    except ftplib.error_perm:
        ftp.login("root", "")
    ftp.set_pasv(True)
    return ftp


def ftp_mkdirs(ftp: ftplib.FTP, remote_dir: str) -> None:
    parts = [p for p in remote_dir.strip("/").split("/") if p]
    cur = ""
    for p in parts:
        cur += "/" + p
        try:
            ftp.mkd(cur)
        except ftplib.error_perm:
            pass


def ftp_upload_file(ftp: ftplib.FTP, local_file: str, remote_file: str) -> None:
    size = os.path.getsize(local_file)
    sent = [0]
    t0 = time.time()

    def cb(chunk: bytes) -> None:
        sent[0] += len(chunk)
        pct = int(sent[0] * 100 / size) if size > 0 else 100
        sys.stdout.write(f"\r  -> {remote_file}  {pct:3d}% ({human_size(sent[0])}/{human_size(size)})")
        sys.stdout.flush()

    ftp_mkdirs(ftp, os.path.dirname(remote_file))
    with open(local_file, "rb") as f:
        ftp.storbinary(f"STOR {remote_file}", f, blocksize=16384, callback=cb)
    dt = max(time.time() - t0, 0.001)
    rate = size / dt
    sys.stdout.write(f"\r  OK {remote_file}  ({human_size(size)}, {human_size(int(rate))}/s)      \n")
    sys.stdout.flush()


def ftp_download_file(ftp: ftplib.FTP, remote_file: str, local_file: str) -> None:
    os.makedirs(os.path.dirname(os.path.abspath(local_file)), exist_ok=True)
    got = [0]
    t0 = time.time()

    with open(local_file, "wb") as f:
        def cb(chunk: bytes) -> None:
            f.write(chunk)
            got[0] += len(chunk)
            sys.stdout.write(f"\r  <- {remote_file}  ({human_size(got[0])})")
            sys.stdout.flush()

        ftp.retrbinary(f"RETR {remote_file}", cb, blocksize=16384)
    dt = max(time.time() - t0, 0.001)
    sys.stdout.write(f"\r  OK {local_file}  ({human_size(got[0])}, {human_size(int(got[0] / dt))}/s)      \n")
    sys.stdout.flush()


def strip_telnet_iac(data: bytes) -> bytes:
    out = bytearray()
    i = 0
    n = len(data)
    while i < n:
        b = data[i]
        if b == 255 and i + 1 < n:  # IAC
            cmd = data[i + 1]
            if cmd in (251, 252, 253, 254) and i + 2 < n:  # WILL/WONT/DO/DONT
                i += 3
                continue
            elif cmd == 250:  # SB ... SE
                i += 2
                while i + 1 < n and not (data[i] == 255 and data[i + 1] == 240):
                    i += 1
                i += 2
                continue
            else:
                i += 2
                continue
        out.append(b)
        i += 1
    return bytes(out)


def telnet_exec(host: str, command: str, timeout: float = 4.0) -> str:
    """Run a single command on RetroFW's BusyBox telnetd (port 23) and return stdout."""
    sock = socket.create_connection((host, 23), timeout=timeout)
    sock.setblocking(False)
    marker = "__RFW_DONE_991__"

    def drain(wait: float) -> str:
        buf = bytearray()
        end = time.time() + wait
        while time.time() < end:
            r, _, _ = select.select([sock], [], [], max(0.05, end - time.time()))
            if not r:
                if buf:
                    break
                continue
            chunk = sock.recv(4096)
            if not chunk:
                break
            # Reply WONT/DONT to any telnet option negotiations
            i = 0
            while i + 2 < len(chunk):
                if chunk[i] == 255 and chunk[i + 1] in (251, 253):
                    resp = 254 if chunk[i + 1] == 251 else 252
                    try:
                        sock.sendall(bytes([255, resp, chunk[i + 2]]))
                    except OSError:
                        pass
                    i += 3
                else:
                    i += 1
            buf.extend(strip_telnet_iac(chunk))
            if marker.encode() in buf:
                break
        return buf.decode("utf-8", errors="replace")

    try:
        banner = drain(0.8)
        if "login:" in banner.lower():
            sock.sendall(b"root\n")
            banner = drain(0.8)
            if "password:" in banner.lower():
                sock.sendall(b"\n")
                drain(0.6)

        payload = f"{command}; echo {marker}\n".encode("utf-8")
        sock.sendall(payload)
        raw = drain(timeout)
        lines = []
        for line in raw.replace("\r", "").splitlines():
            if marker in line:
                break
            if line.strip() == f"{command}; echo {marker}".strip():
                continue
            lines.append(line)
        return "\n".join(lines).strip()
    finally:
        try:
            sock.sendall(b"exit\n")
        except OSError:
            pass
        sock.close()


def cmd_status(args) -> int:
    host = args.host
    ftp_ok = check_port(host, 21)
    tel_ok = check_port(host, 23)
    print(f"RetroFW host: {host}")
    print(f"  FTP    (port 21): {'ONLINE' if ftp_ok else 'OFFLINE'}")
    print(f"  Telnet (port 23): {'ONLINE' if tel_ok else 'OFFLINE'}")
    if not ftp_ok and not tel_ok:
        print_usb_setup_hint(host)
        return 1
    if tel_ok:
        info = telnet_exec(
            host,
            "echo \"Kernel: $(uname -r)\"; "
            "echo \"Sched:  $(cat /sys/block/mmcblk0/queue/scheduler 2>/dev/null)\"; "
            "echo \"RAM:    $(free -m | awk 'NR==2{print $3\"/\"$2\" MB used\"}')\"; "
            "df -h /home/retrofw /media/mmcblk1p1 2>/dev/null | awk 'NR==1||/mmcblk/'",
        )
        if info:
            print("\nDevice Info:")
            for line in info.splitlines():
                print("  " + line)
    return 0


def cmd_ls(args) -> int:
    remote = expand_remote(args.path)
    if not check_port(args.host, 21):
        print_usb_setup_hint(args.host)
        return 1
    ftp = ftp_connect(args.host)
    try:
        ftp.cwd(remote)
        ftp.retrlines("LIST")
    finally:
        ftp.quit()
    return 0


def cmd_push(args) -> int:
    local = args.local
    if not os.path.exists(local):
        print(f"Error: local path not found: {local}", file=sys.stderr)
        return 1
    remote = expand_remote(args.remote)
    if not check_port(args.host, 21):
        print_usb_setup_hint(args.host)
        return 1

    ftp = ftp_connect(args.host)
    try:
        if os.path.isfile(local):
            # If remote looks like a directory (ends with / or exists as dir), append filename
            dest = remote
            if dest.endswith("/") or dest in ("/home/retrofw", "/media/mmcblk1p1"):
                dest = dest.rstrip("/") + "/" + os.path.basename(local)
            else:
                try:
                    ftp.cwd(dest)
                    dest = dest.rstrip("/") + "/" + os.path.basename(local)
                except ftplib.error_perm:
                    pass
            ftp_upload_file(ftp, local, dest)
        else:
            base = remote.rstrip("/") + "/" + os.path.basename(os.path.abspath(local))
            for root, _, files in os.walk(local):
                rel = os.path.relpath(root, local)
                rdir = base if rel == "." else (base + "/" + rel.replace(os.sep, "/"))
                for fname in sorted(files):
                    ftp_upload_file(ftp, os.path.join(root, fname), rdir + "/" + fname)
    finally:
        ftp.quit()

    if check_port(args.host, 23):
        telnet_exec(args.host, "sync", timeout=3.0)
        print("Synced handheld filesystem.")
    return 0


def cmd_pull(args) -> int:
    remote = expand_remote(args.remote)
    local_dir = args.local or "."
    if not check_port(args.host, 21):
        print_usb_setup_hint(args.host)
        return 1

    ftp = ftp_connect(args.host)
    try:
        if os.path.isdir(local_dir):
            local_file = os.path.join(local_dir, os.path.basename(remote.rstrip("/")))
        else:
            local_file = local_dir
        ftp_download_file(ftp, remote, local_file)
    finally:
        ftp.quit()
    return 0


def cmd_rm(args) -> int:
    remote = expand_remote(args.remote)
    if not check_port(args.host, 21):
        print_usb_setup_hint(args.host)
        return 1
    ftp = ftp_connect(args.host)
    try:
        ftp.delete(remote)
        print(f"Deleted {remote}")
    finally:
        ftp.quit()
    return 0


def cmd_screenshots(args) -> int:
    out_dir = args.out or os.path.join(REPO_ROOT, "screenshots")
    os.makedirs(out_dir, exist_ok=True)
    if not check_port(args.host, 21):
        print_usb_setup_hint(args.host)
        return 1
    ftp = ftp_connect(args.host)
    count = 0
    try:
        ftp.cwd("/home/retrofw")
        names = ftp.nlst()
        for n in sorted(names):
            low = n.lower()
            if low.startswith("screenshot") and low.endswith((".png", ".bmp")):
                ftp_download_file(ftp, "/home/retrofw/" + n, os.path.join(out_dir, n))
                count += 1
    finally:
        ftp.quit()
    if count == 0:
        print("No screenshots found in /home/retrofw/ (hold START on handheld to capture one!).")
    else:
        print(f"Pulled {count} screenshot(s) into {out_dir}")
    return 0


def cmd_deploy(args) -> int:
    dist_dir = os.path.join(REPO_ROOT, "dist", "retrofw")
    files = ["gmenunx", "libperfhud.so", "about.txt"]
    for fn in files:
        if not os.path.isfile(os.path.join(dist_dir, fn)):
            print(f"Missing build artifact: {os.path.join(dist_dir, fn)}", file=sys.stderr)
            print("Run `source ~/retrofw-dev/env.sh && make PLATFORM=retrofw CC=mipsel-linux-gcc CXX=mipsel-linux-g++ STRIP=mipsel-linux-strip dist` first.", file=sys.stderr)
            return 1

    if not check_port(args.host, 21):
        print_usb_setup_hint(args.host)
        return 1

    print(f"Deploying GMenuNX+ Modern to {args.host} ...")
    ftp = ftp_connect(args.host)
    try:
        for fn in files:
            ftp_upload_file(ftp, os.path.join(dist_dir, fn), f"/home/retrofw/apps/gmenu2x-plus/{fn}")
    finally:
        ftp.quit()

    if check_port(args.host, 23):
        print("Syncing & restarting GMenuNX+ on handheld...")
        telnet_exec(args.host, "rm -f /tmp/libperfhud.so; sync; killall gmenunx-plus gmenunx 2>/dev/null || true", timeout=3.0)
        print("Done! GMenuNX+ should respawn immediately on your RS-97 Plus.")
    else:
        print("Uploaded! (Telnet unavailable; reboot handheld to load the new binary.)")
    return 0


def cmd_shell(args) -> int:
    if not check_port(args.host, 23):
        print_usb_setup_hint(args.host)
        return 1
    if args.command:
        cmd = " ".join(args.command)
        out = telnet_exec(args.host, cmd, timeout=8.0)
        if out:
            print(out)
        return 0
    # Interactive fallback via system telnet if installed
    if subprocess.call(["which", "telnet"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) == 0:
        return subprocess.call(["telnet", args.host])
    print("Tip: pass a command directly, e.g.: retrofw-usb shell 'free -m'")
    return 0


def main() -> int:
    p = argparse.ArgumentParser(
        prog="retrofw-usb",
        description="USB Ethernet (FTP + Telnet) file transfer & instant deploy tool for RetroFW 2.3 (RS-97 Plus).",
    )
    p.add_argument("--host", default=DEFAULT_HOST, help=f"Handheld IP (default: {DEFAULT_HOST})")
    sub = p.add_subparsers(dest="cmd", required=True)

    sub.add_parser("status", help="Check USB connection, FTP/Telnet ports, and device stats")
    sub.add_parser("deploy", help="Push latest gmenunx + libperfhud.so and restart GMenuNX on handheld")

    p_ls = sub.add_parser("ls", help="List files on handheld (supports home:, roms:, apps:, emu:, sd2:)")
    p_ls.add_argument("path", nargs="?", default="/home/retrofw")

    p_push = sub.add_parser("push", help="Upload a file or folder to handheld")
    p_push.add_argument("local", help="Local file or directory")
    p_push.add_argument("remote", nargs="?", default="/home/retrofw", help="Remote path (e.g. roms:SFC/, sd2:apps/, home:)")

    p_pull = sub.add_parser("pull", help="Download a file from handheld")
    p_pull.add_argument("remote", help="Remote file path")
    p_pull.add_argument("local", nargs="?", default=".", help="Local destination path or folder")

    p_rm = sub.add_parser("rm", help="Delete a file on handheld")
    p_rm.add_argument("remote", help="Remote file path")

    p_ss = sub.add_parser("screenshots", help="Download screenshots from /home/retrofw/ into ./screenshots/")
    p_ss.add_argument("out", nargs="?", help="Output folder")

    p_sh = sub.add_parser("shell", help="Run a shell command on handheld via Telnet")
    p_sh.add_argument("command", nargs="*", help="Command to execute")

    args = p.parse_args()
    dispatch = {
        "status": cmd_status,
        "deploy": cmd_deploy,
        "ls": cmd_ls,
        "push": cmd_push,
        "pull": cmd_pull,
        "rm": cmd_rm,
        "screenshots": cmd_screenshots,
        "shell": cmd_shell,
    }
    return dispatch[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
