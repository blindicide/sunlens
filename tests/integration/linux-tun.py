"""Живой TUN/VLESS тест только в изолированном user/network namespace."""

import argparse
import hashlib
import fcntl
import http.server
import ipaddress
import json
import os
import pathlib
import selectors
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


def run(*arguments):
    return subprocess.check_output(arguments, stderr=subprocess.STDOUT, text=True)


def ip(*arguments):
    return run("/usr/sbin/ip", *arguments)


def require_namespace():
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


def http_probe(address):
    host = f"[{address}]" if ":" in address else address
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    with opener.open(f"http://{host}:18080/fixture", timeout=5) as response:
        if response.read() != BODY:
            raise RuntimeError("HTTP через TUN вернул неверные данные.")


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


def dns_probe():
    name = b"\x07fixture\x07invalid\x00"
    query = struct.pack("!HHHHHH", 0x1234, 0x0100, 1, 0, 0, 0) + name + b"\x00\x01\x00\x01"
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as client:
        client.settimeout(4)
        client.sendto(query, ("8.8.8.8", 53))
        answer, _ = client.recvfrom(4096)
        if answer[:2] != query[:2] or socket.inet_aton(TARGET4) not in answer:
            raise RuntimeError("DNS через TUN вернул неверный ответ.")


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200)
        self.send_header("Content-Length", str(len(BODY)))
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


def server(core):
    require_namespace()
    print(json.dumps({"state": "namespace-ready"}), flush=True)
    sys.stdin.buffer.readline()
    ip("link", "set", "lo", "up")
    ip("link", "set", "dev", "veth-server", "addrgenmode", "none")
    ip("link", "set", "veth-server", "up")
    ip("address", "add", "10.200.0.1/24", "dev", "veth-server")
    ip("-6", "address", "add", "2001:db8:1::1/64", "dev", "veth-server", "nodad")
    ip("address", "add", TARGET4 + "/32", "dev", "lo")
    ip("address", "add", DNS + "/32", "dev", "lo")
    ip("-6", "address", "add", TARGET6 + "/128", "dev", "lo", "nodad")
    services = [
        http.server.ThreadingHTTPServer((TARGET4, 18080), Handler),
        HTTP6((TARGET6, 18080), Handler),
    ]
    for service in services:
        threading.Thread(target=service.serve_forever, daemon=True).start()
    for family, address, port, dns in (
        (socket.AF_INET, TARGET4, 18081, False),
        (socket.AF_INET6, TARGET6, 18081, False),
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


def request(path, ipv6=True, invalid=False):
    node = {
        "protocol": "vless", "label": "Fixture", "host": "10.200.0.1", "port": 18443,
        "credential": WRONG_UUID if invalid else FIXTURE_UUID,
        "method": "", "transport": "tcp", "tls": False, "serverName": "",
        "wsHost": "", "wsPath": "", "flow": "",
    }
    path.write_text(json.dumps({
        "schema": 1, "core": "sing-box",
        "serverId": "b6d532a4-82a6-4390-ab6d-17cfe7f468e7",
        "subscriptionId": "e5e0cb97-cf16-4d13-9bc9-ad6da8182c61",
        "node": node,
        "options": {
            "routing": 0, "bypassLan": True, "ipv6": ipv6, "rules": [],
            "dns": {"address": DNS, "port": 53, "https": False, "tlsName": ""},
        },
    }))


def client(args):
    require_namespace()
    report = pathlib.Path(args.report).resolve()
    report.mkdir(parents=True, exist_ok=True)
    processes = []
    try:
        remote = subprocess.Popen(
            ["unshare", "--net", sys.executable, __file__, "--server", "--core", args.core],
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
        ip("route", "add", "default", "via", "10.200.0.1")
        ip("-6", "route", "add", "default", "via", "2001:db8:1::1")
        send(remote, "configure")
        line(remote)
        http_probe(TARGET4)
        http_probe(TARGET6)
        ip("-4", "rule", "add", "priority", "7000", "from", "198.18.0.0/15", "lookup", "main")
        ip("-4", "route", "add", "198.19.0.0/24", "dev", "veth-client", "table", "500")
        before = network()
        (report / "before.json").write_text(json.dumps(before, indent=2))
        with tempfile.TemporaryDirectory(prefix="sunlens-client-") as temporary:
            directory = pathlib.Path(temporary)
            runtime = directory / "runtime"
            state = directory / "state"
            runtime.mkdir()
            state.mkdir()
            profile = directory / "request.json"
            request(profile)

            def driver():
                process = subprocess.Popen(
                    [args.driver, args.core, str(runtime), str(state), str(profile)],
                    stdin=subprocess.PIPE, stdout=subprocess.PIPE, bufsize=0,
                )
                processes.append(process)
                until_state(process, "idle")
                return process

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
                send(helper, "start")
                def during():
                    snapshot = network()
                    snapshot["addresses"] = json.loads(ip("-j", "address", "show"))
                    (report / "during.json").write_text(json.dumps(snapshot, indent=2))
                ready = until_state(helper, "ready")
                during()
                http_probe(TARGET4)
                http_probe(TARGET6)
                udp(TARGET4)
                udp(TARGET6)
                dns_probe()
                sampled = until_state(helper, "ready")
                deadline = time.monotonic() + 5
                while int(sampled["upload"]) <= 512 or int(sampled["download"]) <= 512:
                    if time.monotonic() > deadline:
                        raise RuntimeError("Фактические счётчики TUN недоступны.")
                    sampled = line(helper)
                send(helper, "stop")
                until_state(helper, "idle")
                if network() != before:
                    (report / "failed-restoration.json").write_text(json.dumps(network(), indent=2))
                    raise RuntimeError("Сеть не восстановлена после отключения.")
                request(profile, ipv6=False)
                send(helper, "start")
                until_state(helper, "ready")
                http_probe(TARGET4)
                udp(TARGET6, should_work=False)
                send(helper, "stop")
                until_state(helper, "idle")
                if network() != before:
                    (report / "failed-restoration.json").write_text(json.dumps(network(), indent=2))
                    raise RuntimeError("Сеть не восстановлена после IPv6 disable.")
                request(profile, invalid=True)
                send(helper, "start")
                until_state(helper, "ready")
                try:
                    http_probe(TARGET4)
                except (OSError, TimeoutError):
                    pass
                else:
                    raise RuntimeError("Неверный UUID не должен давать рабочее подключение.")
                send(helper, "stop")
                until_state(helper, "idle")
                request(profile)
                send(helper, "start")
                until_state(helper, "ready")
                send(helper, "kill-core")
                until_state(helper, "error")
                if network() != before:
                    raise RuntimeError("Авария ядра оставила сетевые изменения.")
                send(helper, "start")
                until_state(helper, "ready")
                helper.kill()
                helper.wait(timeout=5)
                helper = driver()
                if network() != before:
                    raise RuntimeError("Crash recovery оставил сетевые изменения.")
                if (state / "recovery.json").exists() or list(runtime.iterdir()):
                    raise RuntimeError("Crash recovery оставил журнал или секретные файлы.")
                send(helper, "quit")
                helper.wait(timeout=5)
                capture.send_signal(signal.SIGINT)
                capture.wait(timeout=5)
            leaked = run("tcpdump", "-Z", "root", "-nn", "-r", str(report / "leaks.pcap"))
            packets = [value for value in leaked.splitlines() if value and not value.startswith("reading ")]
            if packets:
                raise RuntimeError("Обнаружены открытые DNS/target пакеты на физическом интерфейсе.")
            result = {
                "scope": "isolated-linux-user-network-namespace",
                "core": "sing-box", "protocol": "VLESS/TCP fixture",
                "coreSha256": hashlib.sha256(pathlib.Path(args.core).read_bytes()).hexdigest(),
                "tcp4": True, "tcp6": True, "udp4": True, "udp6": True,
                "explicitDnsThroughTun": True, "ipv6DisabledBlocksTraffic": True,
                "invalidCredentialsNoDirectFallback": True,
                "disconnectRestored": True, "coreCrashRestored": True,
                "helperCrashRecovered": True, "externalTargetDnsLeakPackets": 0,
                "actualTunUpload": sampled["upload"], "actualTunDownload": sampled["download"],
                "systemResolverVerified": False, "polkitInstallationVerified": False,
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
    parser.add_argument("--core", required=True)
    parser.add_argument("--driver")
    parser.add_argument("--report")
    parser.add_argument("--server", action="store_true")
    args = parser.parse_args()
    if args.server:
        server(args.core)
    else:
        if not args.driver or not args.report:
            parser.error("Требуются --driver и --report.")
        client(args)


if __name__ == "__main__":
    main()
