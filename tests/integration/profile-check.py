"""Локальный HTTPS через реальные ядра; DNS, TUN и маршруты хоста не меняются."""

import argparse
import http.server
import json
import pathlib
import socket
import ssl
import subprocess
import tempfile
import threading
import time


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        self.server.paths.append(self.path)
        if self.path == "/redirect":
            self.send_response(302)
            self.send_header("Location", "/ok")
        else:
            self.send_response(204 if self.path == "/ok" else 404)
        self.end_headers()

    def log_message(self, *_):
        pass


def fixture_config(name, proxy_port, tls_port):
    if name == "sing-box":
        return {
            "log": {"disabled": True},
            "inbounds": [{
                "type": "shadowsocks", "listen": "127.0.0.1", "listen_port": proxy_port,
                "method": "aes-128-gcm", "password": "fixture-password",
            }],
            "outbounds": [{"type": "direct", "tag": "fixture"}],
            "route": {
                "rules": [{
                    "domain": ["probe.invalid"], "action": "route",
                    "outbound": "fixture", "override_address": "127.0.0.1",
                    "override_port": tls_port,
                }],
                "final": "fixture",
            },
        }
    return {
        "log": {"loglevel": "none"},
        "inbounds": [{
            "protocol": "shadowsocks", "listen": "127.0.0.1", "port": proxy_port,
            "settings": {"method": "aes-128-gcm", "password": "fixture-password",
                         "network": "tcp"},
        }],
        "outbounds": [{
            "protocol": "freedom",
            "settings": {
                "redirect": f"127.0.0.1:{tls_port}",
                "finalRules": [{"action": "allow", "ip": ["127.0.0.1/32"], "port": tls_port}],
            },
        }],
    }


def wait_ready(child, port):
    until = time.monotonic() + 4
    while child.poll() is None and time.monotonic() < until:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.2):
                return
        except OSError:
            time.sleep(0.025)
    raise RuntimeError("Ядро стенда не открыло порт.")


def verify(args, directory, server, certificate):
    with socket.socket() as port_holder:
        port_holder.bind(("127.0.0.1", 0))
        proxy_port = port_holder.getsockname()[1]
    config = directory / "fixture.json"
    config.write_text(json.dumps(fixture_config(args.name, proxy_port, server.server_port)),
                      encoding="utf-8")
    with subprocess.Popen([args.binary, "run", "-c", str(config)],
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) as child:
        try:
            wait_ready(child, proxy_port)
            for path, trust, credential, result in (
                ("/ok", certificate, "valid", "ok"),
                ("/ok", certificate, "invalid", "fail"),
                ("/ok", directory / "unknown.pem", "valid", "fail"),
                ("/redirect", certificate, "valid", "fail"),
                ("/missing", certificate, "valid", "fail"),
            ):
                subprocess.run([
                    args.driver, args.name, args.binary, str(proxy_port),
                    f"https://probe.invalid:{server.server_port}{path}",
                    str(trust), credential, result,
                ], check=True, timeout=15)
            if server.paths != ["/ok", "/redirect", "/missing"]:
                raise RuntimeError(f"Лишние HTTPS-запросы: {server.paths!r}")
        finally:
            child.terminate()
            try:
                child.wait(timeout=3)
            except subprocess.TimeoutExpired:
                child.kill()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--name", choices=("sing-box", "xray"), required=True)
    parser.add_argument("--binary", required=True)
    parser.add_argument("--driver", required=True)
    parser.add_argument("--openssl", required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as temporary:
        directory = pathlib.Path(temporary)
        certificate = directory / "server.pem"
        key = directory / "server-key.pem"
        subprocess.run([
            args.openssl, "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
            "-subj", "/CN=probe.invalid", "-addext", "subjectAltName=DNS:probe.invalid",
            "-out", str(certificate), "-keyout", str(key),
        ], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=10)
        with http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler) as server:
            server.paths = []
            tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            tls.load_cert_chain(certificate, key)
            server.socket = tls.wrap_socket(server.socket, server_side=True)
            thread = threading.Thread(target=server.serve_forever)
            thread.start()
            try:
                verify(args, directory, server, certificate)
            finally:
                server.shutdown()
                thread.join()


if __name__ == "__main__":
    main()
