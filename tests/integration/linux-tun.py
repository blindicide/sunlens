"""Живой TUN/VLESS тест в изолированном network namespace."""

import argparse
import hashlib
import fcntl
import http.server
import ipaddress
import json
import os
import pathlib
import selectors
import shutil
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import urllib.request

FIXTURE_UUID = "11223344-5566-4788-99aa-bbccddeeff00"
WRONG_UUID = "11223344-5566-4788-99aa-bbccddeeff11"
BODY = b"SUNLENS-TUN-FIXTURE"
TARGET4 = "203.0.113.8"
TARGET6 = "2001:db8::8"
DNS = "203.0.113.53"
SERVER_LAN = "10.200.0.1"
CLIENT_LAN = "10.200.0.2"
RULE_LAN = "10.200.0.3"
SERVER_LAN6 = "fd12:3456::1"
CLIENT_LAN6 = "fd12:3456::2"
HELPER = "/usr/libexec/sunlens/sunlens-tunnel-helper"
INSTALLED_CORES = pathlib.Path("/usr/libexec/sunlens/cores")
STATE = pathlib.Path("/var/lib/sunlens")
RUNTIME = pathlib.Path("/run/sunlens")
RESOLVECTL = "/usr/bin/resolvectl"


def run(*arguments):
    return subprocess.check_output(arguments, stderr=subprocess.STDOUT, text=True, timeout=15)


def ip(*arguments):
    return run("/usr/bin/ip", *arguments)


def require_namespace(installed=False):
    if installed:
        if os.geteuid() != 0 or os.environ.get("SUNLENS_ISOLATED_CI") != "1" or any(
            os.path.samefile(f"/proc/self/ns/{kind}", f"/proc/1/ns/{kind}")
            for kind in ("net", "mnt")
        ):
            raise RuntimeError("Установленный тест требует отдельные mount/network namespaces CI.")
        os.umask(0o077)
        return
    mapping = pathlib.Path("/proc/self/uid_map").read_text().split()
    descriptor = os.open("/proc/self/ns/net", os.O_RDONLY)
    try:
        owner = fcntl.ioctl(descriptor, 0xB701)
    finally:
        os.close(descriptor)
    try:
        isolated = os.fstat(owner).st_ino == os.stat("/proc/self/ns/user").st_ino
    finally:
        os.close(owner)
    if (os.geteuid() != 0 or len(mapping) != 3 or mapping[0] != "0"
            or mapping[1] == "0" or mapping[2] != "1"
            or not isolated):
        raise RuntimeError("Требуется отдельный user/network namespace непривилегированного UID.")
    os.umask(0o077)


def private_state():
    run("mount", "--make-rprivate", "/")
    run("mount", "-t", "tmpfs", "-o", "mode=0755,nosuid,nodev,noexec", "tmpfs", "/run")
    run("mount", "-t", "tmpfs", "-o", "mode=0700,nosuid,nodev,noexec", "tmpfs", str(STATE))
    for path in ("/run", str(STATE)):
        if os.path.samefile(path, "/proc/1/root" + path):
            raise RuntimeError("Каталоги состояния должны быть изолированы от runner.")


def start_resolver(processes):
    pathlib.Path("/run/dbus").mkdir(mode=0o755)
    pathlib.Path("/run/dbus").chmod(0o755)
    directory = pathlib.Path("/run/systemd/resolve")
    directory.parent.mkdir(mode=0o755)
    directory.parent.chmod(0o755)
    directory.mkdir(mode=0o755)
    directory.chmod(0o755)
    shutil.chown(directory, user="systemd-resolve", group="systemd-resolve")
    config = pathlib.Path("/run/sunlens-resolved.conf")
    config.write_text("[Resolve]\nDNS=\nFallbackDNS=\nDNSSEC=no\nDNSOverTLS=no\n"
                      "LLMNR=no\nMulticastDNS=no\nCache=no\nDNSStubListener=yes\n")
    config.chmod(0o644)
    run("mount", "--bind", str(config), "/etc/systemd/resolved.conf")
    os.environ.pop("DBUS_SYSTEM_BUS_ADDRESS", None)
    bus = subprocess.Popen(["dbus-daemon", "--system", "--nofork", "--nopidfile",
                            "--address=unix:path=/run/dbus/system_bus_socket"])
    processes.append(bus)
    deadline = time.monotonic() + 5
    while not pathlib.Path("/run/dbus/system_bus_socket").is_socket():
        if bus.poll() is not None or time.monotonic() >= deadline:
            raise RuntimeError("Не запустилась изолированная системная шина D-Bus.")
        time.sleep(0.05)
    capabilities = "+setpcap,+net_bind_service,+net_raw"
    resolver = subprocess.Popen([
        "setpriv", "--reuid=systemd-resolve", "--regid=systemd-resolve", "--init-groups",
        f"--inh-caps={capabilities}", f"--ambient-caps={capabilities}",
        "/usr/lib/systemd/systemd-resolved",
    ])
    processes.append(resolver)
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline and bus.poll() is None and resolver.poll() is None:
        try:
            subprocess.run([RESOLVECTL, "status"], stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL, timeout=1, check=True)
            return
        except (subprocess.CalledProcessError, subprocess.TimeoutExpired):
            time.sleep(0.1)
    raise RuntimeError("Не запустился изолированный системный resolver.")


def line(process, timeout=20):
    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ)
    try:
        if not selector.select(timeout):
            raise RuntimeError("Процесс fixture не ответил вовремя.")
        value = process.stdout.readline()
        if not value:
            raise RuntimeError(f"Процесс fixture завершился: {process.poll()}")
        return json.loads(value)
    finally:
        selector.close()


def until_state(process, expected, timeout=25):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        status = line(process, max(0.1, deadline - time.monotonic()))
        if status["state"] == expected:
            return status
        if status["state"] in ("recovery-required", "error") and expected != status["state"]:
            raise RuntimeError(f"Помощник: {status['message']}")
    raise RuntimeError("Помощник не достиг ожидаемого состояния.")


def send(process, operation):
    process.stdin.write((operation + "\n").encode())
    process.stdin.flush()


def network():
    return {
        family + kind: json.loads(ip("-j", family, kind, "show", *(
            ("table", "all") if kind == "route" else ()
        )))
        for family in ("-4", "-6") for kind in ("route", "rule")
    }


def http_probe(address, *, host_header=None, expected_peer=None):
    host = f"[{address}]" if ":" in address else address
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    query = urllib.request.Request(f"http://{host}:18080/fixture")
    if host_header:
        query.add_header("Host", host_header)
    with opener.open(query, timeout=5) as response:
        if response.read() != BODY:
            raise RuntimeError("HTTP через TUN вернул неверные данные.")
        peer = response.headers.get("X-Fixture-Peer")
        if expected_peer and peer != expected_peer:
            raise RuntimeError(f"Маршрут HTTP к {host_header or address}: {peer}, ожидался {expected_peer}.")


def udp(address, should_work=True):
    family = socket.AF_INET6 if ":" in address else socket.AF_INET
    with socket.socket(family, socket.SOCK_DGRAM) as client:
        client.settimeout(2)
        client.sendto(b"fixture-udp", (address, 18081))
        try:
            received, _ = client.recvfrom(1024)
        except socket.timeout:
            if not should_work:
                return
            raise
        if not should_work or received != b"sunlens:fixture-udp":
            raise RuntimeError("Неверный результат UDP/IPv6 disable.")


def dns_probe(address="8.8.8.8", hostname="fixture.invalid"):
    name = b"".join(bytes([len(label)]) + label.encode("ascii") for label in hostname.split(".")) + b"\0"
    query = struct.pack("!HHHHHH", 0x1234, 0x0100, 1, 0, 0, 0) + name + b"\x00\x01\x00\x01"
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as client:
        client.settimeout(4)
        client.sendto(query, (address, 53))
        answer, _ = client.recvfrom(4096)
        if answer[:2] != query[:2] or socket.inet_aton(TARGET4) not in answer:
            raise RuntimeError("DNS через TUN вернул неверный ответ.")


def system_dns_probe():
    output = run(RESOLVECTL, "--protocol=dns", "--type=A", "query", "system-resolver.example")
    if TARGET4 not in output:
        raise RuntimeError("Системный resolver вернул неверный IP.")
    dns_probe("127.0.0.53", "system-stub.example")
    if TARGET4 not in run("getent", "ahostsv4", "system-nss.example"):
        raise RuntimeError("Разрешение имени через системный NSS вернуло неверный IP.")


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200)
        self.send_header("Content-Length", str(len(BODY)))
        self.send_header("X-Fixture-Peer", self.client_address[0])
        self.end_headers()
        self.wfile.write(BODY)

    def log_message(self, *_arguments):
        pass


class HTTP6(http.server.ThreadingHTTPServer):
    address_family = socket.AF_INET6


def udp_server(family, address, port, dns=False):
    server = socket.socket(family, socket.SOCK_DGRAM)
    server.bind((address, port))
    while True:
        packet, peer = server.recvfrom(4096)
        if dns:
            end = 12
            while end < len(packet) and packet[end]:
                end += packet[end] + 1
            end += 5
            if end > len(packet):
                continue
            qtype = struct.unpack("!H", packet[end - 4:end - 2])[0]
            value = ipaddress.ip_address(TARGET4 if qtype == 1 else TARGET6).packed
            answer = (packet[:2] + b"\x81\x80\x00\x01\x00\x01\x00\x00\x00\x00"
                      + packet[12:end] + b"\xc0\x0c"
                      + struct.pack("!HHIH", qtype, 1, 60, len(value)) + value)
        else:
            answer = b"sunlens:" + packet
        server.sendto(answer, peer)


def server(core, installed=False):
    require_namespace(installed)
    print(json.dumps({"state": "namespace-ready"}), flush=True)
    sys.stdin.buffer.readline()
    ip("link", "set", "lo", "up")
    ip("link", "set", "dev", "veth-server", "addrgenmode", "none")
    ip("link", "set", "veth-server", "up")
    ip("address", "add", "10.200.0.1/24", "dev", "veth-server")
    ip("address", "add", RULE_LAN + "/24", "dev", "veth-server")
    ip("-6", "address", "add", "2001:db8:1::1/64", "dev", "veth-server", "nodad")
    ip("-6", "address", "add", SERVER_LAN6 + "/64", "dev", "veth-server", "nodad")
    ip("address", "add", TARGET4 + "/32", "dev", "lo")
    ip("address", "add", DNS + "/32", "dev", "lo")
    ip("-6", "address", "add", TARGET6 + "/128", "dev", "lo", "nodad")
    services = [
        http.server.ThreadingHTTPServer((TARGET4, 18080), Handler),
        HTTP6((TARGET6, 18080), Handler),
        http.server.ThreadingHTTPServer((SERVER_LAN, 18080), Handler),
        http.server.ThreadingHTTPServer((RULE_LAN, 18080), Handler),
    ]
    for service in services:
        threading.Thread(target=service.serve_forever, daemon=True).start()
    for family, address, port, dns in (
        (socket.AF_INET, TARGET4, 18081, False),
        (socket.AF_INET6, TARGET6, 18081, False),
        (socket.AF_INET6, SERVER_LAN6, 18081, False),
        (socket.AF_INET, DNS, 53, True),
    ):
        threading.Thread(target=udp_server, args=(family, address, port, dns), daemon=True).start()
    with tempfile.TemporaryDirectory(prefix="sunlens-server-") as temporary:
        config = pathlib.Path(temporary) / "server.json"
        config.write_text(json.dumps({
            "log": {"disabled": True},
            "inbounds": [{
                "type": "vless", "listen": "10.200.0.1", "listen_port": 18443,
                "users": [{"uuid": FIXTURE_UUID}],
            }],
            "outbounds": [{"type": "direct"}],
        }))
        process = subprocess.Popen(
            [core, "run", "-c", str(config)],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )
        try:
            deadline = time.monotonic() + 10
            while True:
                try:
                    with socket.create_connection(("10.200.0.1", 18443), timeout=0.2):
                        break
                except OSError:
                    if process.poll() is not None or time.monotonic() > deadline:
                        raise RuntimeError("VLESS сервер не запустился.")
                    time.sleep(0.05)
            print(json.dumps({"state": "server-ready"}), flush=True)
            sys.stdin.buffer.readline()
        finally:
            process.terminate()
            process.wait(timeout=5)


def request(path, ipv6=True, invalid=False, core="sing-box", *, routing=0, bypass_lan=True,
            rules=()):
    node = {
        "protocol": "vless", "label": "Fixture", "host": SERVER_LAN, "port": 18443,
        "credential": WRONG_UUID if invalid else FIXTURE_UUID,
        "method": "", "transport": "tcp", "tls": False, "serverName": "",
        "wsHost": "", "wsPath": "", "flow": "",
    }
    path.write_text(json.dumps({
        "schema": 1, "core": core,
        "serverId": "b6d532a4-82a6-4390-ab6d-17cfe7f468e7",
        "subscriptionId": "e5e0cb97-cf16-4d13-9bc9-ad6da8182c61",
        "node": node,
        "options": {
            "routing": routing, "bypassLan": bypass_lan, "ipv6": ipv6, "rules": rules,
            "dns": {"address": DNS, "port": 53, "https": False, "tlsName": ""},
        },
    }))


def client(args):
    require_namespace(args.installed)
    if args.installed:
        private_state()
    report = pathlib.Path(args.report).resolve()
    report.mkdir(parents=True, exist_ok=True)
    processes = []
    try:
        remote = subprocess.Popen(
            ["unshare", "--net", sys.executable, __file__, "--server", "--core", args.core]
            + (["--installed"] if args.installed else []),
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, bufsize=0,
        )
        processes.append(remote)
        line(remote)
        ip("link", "set", "lo", "up")
        ip("link", "add", "veth-client", "type", "veth", "peer", "name", "veth-server")
        ip("link", "set", "veth-server", "netns", str(remote.pid))
        ip("link", "set", "dev", "veth-client", "addrgenmode", "none")
        ip("link", "set", "veth-client", "up")
        ip("address", "add", "10.200.0.2/24", "dev", "veth-client")
        ip("-6", "address", "add", "2001:db8:1::2/64", "dev", "veth-client", "nodad")
        ip("-6", "address", "add", CLIENT_LAN6 + "/64", "dev", "veth-client", "nodad")
        ip("route", "add", "default", "via", "10.200.0.1")
        ip("-6", "route", "add", "default", "via", "2001:db8:1::1")
        send(remote, "configure")
        line(remote)
        http_probe(TARGET4)
        http_probe(TARGET6)
        ip("-4", "rule", "add", "priority", "7000", "from", "198.18.0.0/15", "lookup", "main")
        ip("-4", "route", "add", "198.19.0.0/24", "dev", "veth-client", "table", "500")
        dns_before = ""
        if args.installed:
            start_resolver(processes)
            run(RESOLVECTL, "dns", "veth-client", "10.200.0.1")
            run(RESOLVECTL, "default-route", "veth-client", "no")
            run(RESOLVECTL, "domain", "veth-client", "~baseline.example")
            dns_before = run(RESOLVECTL, "status")
        before = network()
        (report / "before.json").write_text(json.dumps(before, indent=2))

        def assert_restored():
            if network() != before:
                (report / "failed-restoration.json").write_text(json.dumps(network(), indent=2))
                raise RuntimeError("Сеть не восстановила исходные маршруты и правила.")
            if args.installed and run(RESOLVECTL, "status") != dns_before:
                raise RuntimeError("Системный resolver не восстановил исходные настройки.")

        with tempfile.TemporaryDirectory(prefix="sunlens-client-") as temporary:
            directory = pathlib.Path(temporary)
            runtime = RUNTIME if args.installed else directory / "runtime"
            state = STATE if args.installed else directory / "state"
            if not args.installed:
                runtime.mkdir()
                state.mkdir()
            profile = directory / "request.json"
            core_name = "xray" if args.xray else "sing-box"
            request(profile, core=core_name)
            journal = state / "recovery.json"

            def driver():
                command = [HELPER] if args.installed else [
                    args.driver, args.core, str(runtime), str(state), str(profile)
                ] + ([args.xray] if args.xray else [])
                process = subprocess.Popen(
                    command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, bufsize=0,
                )
                processes.append(process)
                until_state(process, "idle")
                return process

            def operate(process, operation):
                if args.installed:
                    frame = {"schema": 1, "operation": operation}
                    if operation == "start":
                        frame["request"] = json.loads(profile.read_text())
                    send(process, json.dumps(frame))
                else:
                    send(process, operation)

            helper = driver()
            with (report / "capture.log").open("wb") as capture_log:
                capture = subprocess.Popen([
                    "tcpdump", "-Z", "root", "-U", "-n", "-i", "veth-client", "-w", str(report / "leaks.pcap"),
                    f"host {TARGET4} or host {TARGET6} or host {DNS} or port 53",
                ], stdout=subprocess.DEVNULL, stderr=capture_log)
                processes.append(capture)
                time.sleep(0.25)
                if capture.poll() is not None:
                    raise RuntimeError("Внешний захват пакетов не запустился.")
                operate(helper, "start")
                def during(filename="during.json"):
                    snapshot = network()
                    snapshot["addresses"] = json.loads(ip("-j", "address", "show"))
                    (report / filename).write_text(json.dumps(snapshot, indent=2))
                until_state(helper, "ready")
                during()
                http_probe(TARGET4)
                http_probe(TARGET6)
                udp(TARGET4)
                udp(TARGET6)
                udp(SERVER_LAN6)
                dns_probe()
                http_probe(SERVER_LAN, expected_peer=CLIENT_LAN)
                if args.installed:
                    system_dns_probe()
                    attempt = subprocess.run([HELPER, "--prepare-uninstall"], capture_output=True,
                                             text=True, timeout=15)
                    if attempt.returncode == 0 or "Другой сетевой помощник" not in attempt.stderr:
                        raise RuntimeError("Активный TUN не блокирует подготовку удаления.")
                sampled = until_state(helper, "ready")
                deadline = time.monotonic() + 5
                while int(sampled["upload"]) <= 512 or int(sampled["download"]) <= 512:
                    if time.monotonic() > deadline:
                        raise RuntimeError("Фактические счётчики TUN недоступны.")
                    sampled = line(helper)
                operate(helper, "stop")
                until_state(helper, "idle")
                assert_restored()
                request(profile, core=core_name, routing=2, bypass_lan=False, rules=[
                    {"kind": 0, "value": "direct-rule.example", "direct": True},
                    {"kind": 2, "value": RULE_LAN + "/32", "direct": True},
                ])
                operate(helper, "start")
                until_state(helper, "ready")
                during("during-rules.json")
                http_probe(SERVER_LAN, expected_peer=SERVER_LAN)
                http_probe(SERVER_LAN, host_header="direct-rule.example", expected_peer=CLIENT_LAN)
                http_probe(RULE_LAN, expected_peer=CLIENT_LAN)
                operate(helper, "stop")
                until_state(helper, "idle")
                assert_restored()
                request(profile, core=core_name, routing=1, bypass_lan=False)
                operate(helper, "start")
                until_state(helper, "ready")
                http_probe(SERVER_LAN, expected_peer=CLIENT_LAN)
                operate(helper, "stop")
                until_state(helper, "idle")
                assert_restored()
                request(profile, ipv6=False, core=core_name)
                operate(helper, "start")
                until_state(helper, "ready")
                http_probe(TARGET4)
                udp(TARGET6, should_work=False)
                udp(SERVER_LAN6, should_work=False)
                operate(helper, "stop")
                until_state(helper, "idle")
                assert_restored()
                request(profile, invalid=True, core=core_name)
                operate(helper, "start")
                until_state(helper, "ready")
                try:
                    http_probe(TARGET4)
                except (OSError, TimeoutError):
                    pass
                else:
                    raise RuntimeError("Неверный UUID не должен давать рабочее подключение.")
                operate(helper, "stop")
                until_state(helper, "idle")
                request(profile, core=core_name)
                operate(helper, "start")
                until_state(helper, "ready")
                if args.installed:
                    pid = int(json.loads(journal.read_text())["lease"]["processId"])
                    if pid <= 1:
                        raise RuntimeError("В журнале нет PID ядра.")
                    descriptor = os.pidfd_open(pid)
                    try:
                        if not os.path.samefile(f"/proc/{pid}/ns/net", "/proc/self/ns/net"):
                            raise RuntimeError("Ядро находится вне изолированного namespace.")
                        if not os.path.samefile(f"/proc/{pid}/exe", args.xray or args.core):
                            raise RuntimeError("Журнал не указывает на установленное ядро.")
                        signal.pidfd_send_signal(descriptor, signal.SIGKILL)
                    finally:
                        os.close(descriptor)
                else:
                    operate(helper, "kill-core")
                until_state(helper, "error")
                assert_restored()
                operate(helper, "start")
                until_state(helper, "ready")
                helper.kill()
                helper.wait(timeout=5)
                if args.installed:
                    if not journal.is_file():
                        raise RuntimeError("Отсутствует журнал после аварии помощника.")
                    run(HELPER, "--prepare-uninstall")
                    if not (runtime / "uninstalling").is_file():
                        raise RuntimeError("Подготовка удаления не закрыла новые подключения.")
                    blocked = subprocess.run([HELPER], stdin=subprocess.DEVNULL, capture_output=True,
                                             text=True, timeout=15)
                    if blocked.returncode == 0 or "обслуживается пакетным менеджером" not in blocked.stderr:
                        raise RuntimeError("Во время обслуживания принимаются новые подключения.")
                    run(HELPER, "--finish-install")
                helper = driver()
                assert_restored()
                if journal.exists() or list(runtime.iterdir()):
                    raise RuntimeError("Crash recovery оставил журнал или секретные файлы.")
                helper.stdin.close()
                if helper.wait(timeout=10):
                    raise RuntimeError("Не удалось завершить IPC.")
                capture.send_signal(signal.SIGINT)
                capture.wait(timeout=5)
            leaked = run("tcpdump", "-Z", "root", "-nn", "-r", str(report / "leaks.pcap"))
            packets = [value for value in leaked.splitlines() if value and not value.startswith("reading ")]
            if packets:
                raise RuntimeError("Обнаружены открытые DNS/target пакеты на физическом интерфейсе.")
            result = {
                "scope": ("installed-helper-isolated-linux-network-and-mount-namespaces"
                          if args.installed else "isolated-linux-user-network-namespace"),
                "core": core_name, "protocol": "VLESS/TCP fixture",
                "coreSha256": hashlib.sha256(pathlib.Path(args.xray or args.core).read_bytes()).hexdigest(),
                "tcp4": True, "tcp6": True, "udp4": True, "udp6": True,
                "lanBypassVerified": True, "domainAndCidrRulesVerified": True, "directRoutingVerified": True,
                "explicitDnsThroughTun": True, "ipv6DisabledBlocksTraffic": True,
                "ipv6DisabledBlocksLan": True,
                "invalidCredentialsNoDirectFallback": True,
                "disconnectRestored": True, "coreCrashRestored": True,
                "helperCrashRecovered": True, "externalTargetDnsLeakPackets": 0,
                "actualTunUpload": sampled["upload"], "actualTunDownload": sampled["download"],
                "systemResolverVerified": args.installed,
                "activeTunnelBlocksPreRemoval": args.installed,
                "preRemovalRecoversHelperCrash": args.installed,
                "polkitInstallationVerified": False,
                "windowsVerified": False,
            }
            (report / "result.json").write_text(json.dumps(result, indent=2) + "\n")
            (report / "after.json").write_text(json.dumps(network(), indent=2))
            print(json.dumps(result, indent=2))
    finally:
        for process in reversed(processes):
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)


def main():
    def terminate(_signal, _frame):
        raise SystemExit(1)
    signal.signal(signal.SIGTERM, terminate)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core")
    parser.add_argument("--driver")
    parser.add_argument("--xray")
    parser.add_argument("--report")
    parser.add_argument("--server", action="store_true")
    parser.add_argument("--installed", action="store_true")
    args = parser.parse_args()
    if args.installed:
        if args.core and args.core != str(INSTALLED_CORES / "sing-box"):
            parser.error("Установленный тест использует только системный sing-box.")
        if args.xray and args.xray != str(INSTALLED_CORES / "xray"):
            parser.error("Установленный тест использует только системный Xray.")
        args.core = str(INSTALLED_CORES / "sing-box")
    if not args.core:
        parser.error("Требуется --core.")
    if args.server:
        server(args.core, args.installed)
    else:
        if not args.report or (not args.installed and not args.driver):
            parser.error("Требуются --report и, вне установленного режима, --driver.")
        client(args)


if __name__ == "__main__":
    main()
