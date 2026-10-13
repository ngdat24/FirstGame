#!/usr/bin/env python3
"""End-to-end test of the HTTP server (server.cpp). Standard library only.

    g++ -std=c++17 -O2 server.cpp protocol.cpp GameLogic.cpp -o server -pthread
    python3 test_server.py ./server

It starts the server on a free port with --dev, then checks the protections, plays whole
games over HTTP, replays a seed, and hammers the server from many threads at once.
"""
import http.client
import json
import random
import socket
import subprocess
import sys
import threading
import time

failures = 0
checks = 0


def check(cond, what):
    global failures, checks
    checks += 1
    if not cond:
        failures += 1
        print("FAIL:", what)


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


class Client:
    def __init__(self, port):
        self.port = port

    def request(self, method, path, body=None, headers=None):
        conn = http.client.HTTPConnection("127.0.0.1", self.port, timeout=10)
        conn.request(method, path, body=body, headers=headers or {})
        r = conn.getresponse()
        text = r.read().decode("utf-8", "replace")
        out = (r.status, text, dict(r.getheaders()))
        conn.close()
        return out

    def api(self, obj, headers=None):
        body = obj if isinstance(obj, (str, bytes)) else json.dumps(obj)
        h = {"Content-Type": "application/json"} if headers is None else headers
        status, text, _ = self.request("POST", "/api", body, h)
        return status, text


def play_game(client, seed, chooser_seed):
    """Plays one whole game choosing random legal actions seen ONLY in the JSON snapshots."""
    rnd = random.Random(chooser_seed)
    status, text = client.api({"cmd": "new_game", "seed": seed})
    resp = json.loads(text)
    check(status == 200 and resp["ok"], "new_game over HTTP")
    for _ in range(60000):
        snap = resp["snapshot"]
        if snap["game_over"]:
            return snap
        acts = snap["legal"]["actions"]
        action = dict(rnd.choice(acts))
        if action["type"] == "attempt_steal":
            action["amount"] = rnd.randint(snap["legal"]["min_bribe"], snap["legal"]["max_bribe"])
        action["cmd"] = "action"
        status, text = client.api(action)
        resp = json.loads(text)
        if status != 200 or not resp["ok"]:
            check(False, "a legal action was refused: " + text[:120])
            return None
    check(False, "the game never ended")
    return None


def main():
    binary = sys.argv[1] if len(sys.argv) > 1 else "./server"
    port = free_port()
    proc = subprocess.Popen([binary, "--port", str(port), "--dev"], stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True)
    client = Client(port)
    for _ in range(50):  # wait until it answers
        try:
            if client.request("GET", "/health")[0] == 200:
                break
        except OSError:
            time.sleep(0.1)
    else:
        print("server did not start")
        proc.kill()
        return 1

    # --- protections -----------------------------------------------------------------
    status, text, _ = client.request("GET", "/health")
    check(status == 200 and json.loads(text)["ok"] is True, "health")

    status, text = client.api({"cmd": "get_rules"})
    check(status == 200 and json.loads(text)["ok"] is True, "get_rules")

    status, _ = client.api({"cmd": "new_game"}, headers={"Content-Type": "text/plain"})
    check(status == 415, "text/plain POST is refused with 415 (blocks blind cross-site posts)")
    status, _ = client.api({"cmd": "new_game"}, headers={})
    check(status == 415, "a POST without Content-Type is refused")
    status, _ = client.api({"cmd": "get_rules"}, headers={"Content-Type": "application/json; charset=utf-8"})
    check(status == 200, "application/json with a charset is accepted")

    status, _ = client.api({"cmd": "get_rules"}, headers={"Content-Type": "application/json", "Host": "evil.example.com"})
    check(status == 403, "foreign Host header is refused with 403 (DNS rebinding)")
    for host in ("localhost:%d" % port, "127.0.0.1:%d" % port, "localhost"):
        status, _ = client.api({"cmd": "get_rules"}, headers={"Content-Type": "application/json", "Host": host})
        check(status == 200, "Host %s is accepted" % host)

    status, text = client.api(" " * 6000)
    check(status == 413, "a 6000-byte body is refused with 413")
    status, text, headers = client.request("OPTIONS", "/api", None, {
        "Origin": "http://evil.example.com", "Access-Control-Request-Method": "POST"})
    check(not any(k.lower().startswith("access-control") for k in headers),
          "no CORS headers on a preflight (cross-site pages are blocked)")
    status, text, _ = client.request("GET", "/nope")
    check(status == 404 and json.loads(text)["error"] == "not_found", "unknown path -> JSON 404")
    status, text, _ = client.request("GET", "/api")
    check(status in (404, 405), "GET /api is not an endpoint")

    # --- protocol errors come back as HTTP 200 + ok:false ------------------------------
    status, text = client.api("this is not json")
    check(status == 200 and json.loads(text)["error"] == "bad_request", "garbage body -> ok:false")
    status, text = client.api({"cmd": "action", "type": "buy", "player_id": 0})
    check(status == 200 and json.loads(text)["ok"] is False, "action before new_game -> ok:false")

    # --- whole games over HTTP ---------------------------------------------------------
    final_a = play_game(client, 42, 1234)
    final_b = play_game(client, 42, 1234)
    final_c = play_game(client, 43, 1234)
    check(final_a is not None and final_a["winner_id"] in (0, 1, 2, 3), "a full game over HTTP has a winner")
    check(final_a == final_b, "same seed + same choices -> identical game over HTTP")
    check(final_a != final_c, "different seed -> different game")

    # --- many clients at once ----------------------------------------------------------
    errors = []

    def hammer(worker):
        rnd = random.Random(worker)
        c = Client(port)
        for i in range(60):
            try:
                if rnd.random() < 0.5:
                    status, text = c.api({"cmd": "get_state"})
                else:
                    status, text = c.api({"cmd": "action", "type": rnd.choice(["roll_dice", "buy", "decline", "pay_rent"]),
                                          "player_id": rnd.randint(0, 3)})
                body = json.loads(text)
                if status != 200 or "ok" not in body:
                    errors.append((worker, i, status, text[:80]))
            except Exception as e:  # noqa: BLE001
                errors.append((worker, i, repr(e)))

    check(client.api({"cmd": "new_game", "seed": 5})[0] == 200, "new_game before the stress test")
    threads = [threading.Thread(target=hammer, args=(w,)) for w in range(12)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    check(not errors, "12 concurrent clients got valid answers: %s" % errors[:3])
    check(proc.poll() is None, "the server survived the concurrent clients")
    snap = play_game(client, 9, 99)
    check(snap is not None and snap["game_over"], "a full game still works after the stress test")

    alive = proc.poll() is None
    check(alive, "the server is still running at the end")
    proc.terminate()
    try:
        _, err = proc.communicate(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        _, err = proc.communicate()
    check("ThreadSanitizer" not in err and "AddressSanitizer" not in err and "runtime error" not in err,
          "no sanitizer reports from the server: " + err[:200])

    print("%d/%d checks passed" % (checks - failures, checks))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
