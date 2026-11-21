#!/usr/bin/env python3
"""N10-Q3E stage E part 3: four real clients and a dedicated server on one machine.

  net_test.py run  <package dir> <out dir> [--delay MS] [--loss PCT] [--map MAP]
  net_test.py soak <package dir> <out dir> --minutes N [--delay MS] [--loss PCT] [--map MAP] [--root REPO]

The package is what tools/q3e/package.py writes. Needs a display and a Vulkan driver for the
clients (lavapipe on Xvfb here); each client runs in a 320 x 240 window at 30 fps.

Network conditions use the engine's own simulation on every process:
  --delay MS  cl_packetdelay MS on each client and sv_packetdelay MS on the server, so a round trip
              takes 2 x MS more;
  --loss PCT  net_dropsim PCT on each client (snapshots lost) and on the server (client packets
              lost).

The server reads commands from stdin. The clients cannot (Quake3e reads stdin only on a dedicated
server), so each client runs a console loop that executes b<n>.cfg from its home folder every few
frames; a batch file ends by pointing the loop at the next one. The files exist before the client
starts (Quake3e indexes the folder at start-up) and are replaced whole (rename).

run: join, positions seen by everybody, a walk, down and revived by another client (hold F), a
wave fought by all four, the snapshot audit (cl_shownet 1 on one client: bytes of every server
message; ca_netstat on every client: entities per snapshot; ca_entcount on the server: every entity
a snapshot can carry), a client leaving and coming back, the squad wipe. Checks at the end; the
verdict is the last line of <out>/checks.txt.

soak: waves on with god mode, every client aiming at the nearest enemy and firing, moved between
spawn spots every minute; every minute ca_netstat on every client, ca_entcount / ca_coopstate on
the server and the memory of every process. Checks: everybody stayed connected, the waves went on,
no errors, memory did not keep growing.
"""
import argparse
import json
import math
import os
import re
import shutil
import subprocess
import sys
import time

NAMES = ["Alpha", "Bravo", "Charlie", "Delta"]
PORT = 27960
BATCHES = 400


class Log:
    def __init__(self, path):
        self.path = path

    def text(self):
        try:
            with open(self.path, errors="replace") as f:
                return f.read()
        except FileNotFoundError:
            return ""

    def wait(self, pattern, timeout, start=0):
        """the first match of pattern after character start, or None after timeout seconds"""
        end = time.time() + timeout
        rx = re.compile(pattern)
        while time.time() < end:
            m = rx.search(self.text(), start)
            if m:
                return m
            time.sleep(0.1)
        return None

    def size(self):
        return len(self.text())


class Server:
    def __init__(self, pkg, home, args):
        self.log = Log(os.path.join(home, "server.log"))
        self.proc = subprocess.Popen([os.path.join(pkg, "CatArena-server"), "+set", "fs_homepath", home, "+set", "dedicated", "1"] + args,
                                     stdin=subprocess.PIPE, stdout=open(self.log.path, "w"), stderr=subprocess.STDOUT, text=True, cwd=pkg)

    def cmd(self, *lines):
        for line in lines:
            self.proc.stdin.write(line + "\n")
        self.proc.stdin.flush()


class Client:
    def __init__(self, pkg, home, index, args):
        self.index = index
        self.name = NAMES[index]
        self.dir = os.path.join(home, "catarena")
        os.makedirs(self.dir, exist_ok=True)
        for n in range(1, BATCHES + 1):
            open(os.path.join(self.dir, "b%d.cfg" % n), "w").close()
        with open(os.path.join(self.dir, "drive.cfg"), "w", newline="\n") as f:
            f.write('set ca_next "exec b1.cfg"\nset ca_auto ""\nset ca_loop "vstr ca_next; vstr ca_auto; wait 6; vstr ca_loop"\nvstr ca_loop\n')
        self.n = 0
        self.log = Log(os.path.join(home, "client.log"))
        env = dict(os.environ, SDL_AUDIODRIVER="dummy")
        self.proc = subprocess.Popen([os.path.join(pkg, "CatArena"), "+set", "fs_homepath", home, "+set", "name", self.name,
                                      "+set", "r_mode", "-1", "+set", "r_customwidth", "320", "+set", "r_customheight", "240",
                                      "+set", "r_fullscreen", "0", "+set", "com_maxfps", "30", "+set", "ca_log", "1",
                                      "+set", "cl_drawFPS", "0"] + args + ["+connect", "127.0.0.1:%d" % PORT, "+exec", "drive.cfg"],
                                     stdout=open(self.log.path, "w"), stderr=subprocess.STDOUT, env=env, cwd=pkg)

    def cmd(self, *lines):
        """runs the lines in the client's console at its next loop pass (every 6 frames)"""
        self.n += 1
        if self.n >= BATCHES:
            raise RuntimeError("out of batch files")
        tmp = os.path.join(self.dir, "next.tmp")
        with open(tmp, "w", newline="\n") as f:
            f.write("\n".join(lines) + '\nset ca_next "exec b%d.cfg"\n' % (self.n + 1))
        os.replace(tmp, os.path.join(self.dir, "b%d.cfg" % self.n))


def rss_kb(pid):
    try:
        with open("/proc/%d/status" % pid) as f:
            for line in f:
                if line.startswith("VmRSS:"):
                    return int(line.split()[1])
    except FileNotFoundError:
        pass
    return 0


def parse_kv(line):
    d = {}
    for kv in line.split():
        if "=" in kv:
            k, v = kv.split("=", 1)
            try:
                d[k] = int(v)
            except ValueError:
                try:
                    d[k] = float(v)
                except ValueError:
                    d[k] = v
    return d


def players_of(net):
    """players=<client>:<x>,<y>,<z>:<health>:<downed>;... -> {client: (x, y, z, health, downed)}"""
    out = {}
    if net.get("players", "-") == "-":
        return out
    for p in str(net["players"]).split(";"):
        c, pos, h, dn = p.split(":")
        x, y, z = (float(v) for v in pos.split(","))
        out[int(c)] = (x, y, z, int(h), int(dn))
    return out


class Rig:
    def __init__(self, pkg, out, map_name, delay, loss, waves):
        self.out = out
        shutil.rmtree(out, ignore_errors=True)
        os.makedirs(out)
        net = []
        if loss:
            net += ["+set", "net_dropsim", str(loss)]
        # every player starts with the same two weapons (NAILGUN in hand), as the gameplay test
        sv = ["+set", "ca_log", "1", "+set", "ca_waves", "1" if waves else "0", "+set", "sv_fps", "20", "+set", "g_inactivity", "0",
              "+set", "ca_loadout", "0 1"] + net
        if delay:
            sv += ["+set", "sv_packetdelay", str(delay)]
        h = os.path.join(out, "server")
        os.makedirs(os.path.join(h, "catarena"))
        self.server = Server(pkg, h, sv + ["+set", "net_port", str(PORT), "+devmap", map_name])
        if not self.server.log.wait(r"items registered", 120):
            raise RuntimeError("server did not start")
        time.sleep(3)
        self.clients = []
        cl = net + (["+set", "cl_packetdelay", str(delay)] if delay else [])
        for i in range(4):
            self.clients.append(Client(pkg, os.path.join(out, "client%d" % i), i, cl))
            if not self.server.log.wait(r"ClientBegin: %d\b" % i, 240):
                raise RuntimeError("client %d did not join" % i)
            time.sleep(2)

    def netstat(self, timeout=20):
        """ca_netstat on every client; returns {client index: dict} of the first new answer"""
        starts = [c.log.size() for c in self.clients]
        for c in self.clients:
            c.cmd("ca_netstat")
        res = {}
        for i, c in enumerate(self.clients):
            m = c.log.wait(r"CA_NET (t=.*)", timeout, starts[i])
            res[i] = parse_kv(m.group(1)) if m else {}
        return res

    def coopstate(self, timeout=10):
        start = self.server.log.size()
        self.server.cmd("echo CASTATE_BEGIN", "ca_coopstate", "echo CASTATE_END")
        m = self.server.log.wait(r"(?s)CASTATE_BEGIN(.*?)CASTATE_END", timeout, start)
        rows = {}
        if m:
            for line in re.findall(r"CA_COOP (.*)", m.group(1)):
                d = parse_kv(line)
                rows[d["client"]] = d
        return rows

    def entcount(self, timeout=10):
        start = self.server.log.size()
        self.server.cmd("ca_entcount")
        m = self.server.log.wait(r"CA_ENTS (.*)", timeout, start)
        return parse_kv(m.group(1)) if m else {}

    def memory(self):
        return {"server": rss_kb(self.server.proc.pid), **{c.name: rss_kb(c.proc.pid) for c in self.clients}}

    def close(self):
        for c in self.clients:
            if c.proc.poll() is None:
                c.cmd("quit")
        self.server.cmd("quit")
        end = time.time() + 30
        for p in [c.proc for c in self.clients] + [self.server.proc]:
            try:
                p.wait(max(1, end - time.time()))
            except subprocess.TimeoutExpired:
                p.kill()


def shownet_sizes(text):
    """bytes of every server message between CA_SHOWNET_BEGIN and CA_SHOWNET_END: cl_shownet 1 prints
    "<bytes> " per message with no newline, so a run of numbers at the start of a line"""
    m = re.search(r"CA_SHOWNET_BEGIN(.*?)CA_SHOWNET_END", text, re.S)
    sizes = []
    for line in (m.group(1).splitlines() if m else []):
        run_ = re.match(r"((?:\d+ )+)", line)
        if run_:
            sizes += [int(v) for v in run_.group(1).split()]
    return sizes


def spawn_spots(root, map_name):
    """spawn spots of an N10 map in Quake III units (as run_stage_d.py views)"""
    mid = {"ca_reactor": "reactor-bastion", "ca_helios": "helios-crucible", "ca_bastion": "bastion-keep", "ca_arsenal": "q3-gallery"}.get(map_name)
    if not mid:
        return [(-700, -300, 40), (560, 0, 40)]
    c = json.load(open(os.path.join(root, "assets", "n1", "maps", mid, "map.json")))["compiled"]
    cx, cy = c["meta"]["center"]
    return [((s["x"] - cx) * 32, (s["y"] - cy) * 32, s["z"] * 32 + 28) for s in c["spawns"]]


def run(a):
    rig = Rig(a.pkg, a.out, a.map, a.delay, a.loss, waves=False)
    A, B, C, D = rig.clients
    results = []
    data = {"delay": a.delay, "loss": a.loss, "map": a.map}

    def ok(cond, what):
        results.append((bool(cond), what))
        print(("PASS " if cond else "FAIL ") + what, flush=True)

    try:
        joined = re.findall(r'broadcast: print "(\w+)\^7 entered the game', rig.server.log.text())
        ok(joined == NAMES, "four clients joined the dedicated server: %s" % ", ".join(joined))
        time.sleep(3)
        ns = rig.netstat()
        ok(all(len(players_of(ns[i])) == 3 for i in range(4)), "every client sees the three others: %s" % [sorted(players_of(ns[i])) for i in range(4)])
        ok(all(ns[i].get("client") == i for i in range(4)), "client numbers 0-3 as joined")

        # positions: the server's and every client's view of every other player
        spots = [(-700, -300), (-640, -300), (-700, -220), (-640, -220)]
        for c, (x, y) in zip(rig.clients, spots):
            c.cmd("ca_place %d %d 40 0" % (x, y))
        time.sleep(3)
        sv = rig.coopstate()
        ns = rig.netstat()
        worst = 0.0
        for i in range(4):
            for j, p in players_of(ns[i]).items():
                spos = [float(v) for v in str(sv[j]["pos"]).split(",")]
                worst = max(worst, math.dist(p[:3], spos))
        ok(sv and worst <= 2.0, "placed: every client sees every other player where the server has it (worst %.1f u)" % worst)

        # a walk: Bravo runs forward 1 s; everybody sees the same end point as the server
        B.cmd("+forward", "wait 30", "-forward")
        time.sleep(4)
        sv = rig.coopstate()
        ns = rig.netstat()
        bpos = [float(v) for v in str(sv[1]["pos"]).split(",")]
        seen = [players_of(ns[i]).get(1) for i in (0, 2, 3)]
        walked = math.dist(bpos[:2], spots[1])
        worst = max(math.dist(s[:3], bpos) for s in seen if s) if all(seen) else 999
        ok(walked > 100 and worst <= 2.0, "Bravo walked %.0f u; the other three see him where the server has him (worst %.1f u)" % (walked, worst))
        B.cmd("ca_place %d %d 40 180" % spots[1])
        time.sleep(2)

        # down and revived by another client holding F
        s0 = [c.log.size() for c in rig.clients]
        rig.server.cmd("ca_hurt 1 200")
        time.sleep(2)
        ns = rig.netstat()
        ok(ns[1].get("downed") == 1 and ns[1].get("health") == 0 and (ns[1].get("coop", 0) >> 12) == 4,
           "Bravo's own client: down at 0 health, YOU ARE DOWN (%s)" % {k: ns[1].get(k) for k in ("health", "downed", "coop")})
        ok(all(players_of(ns[i]).get(1, (0, 0, 0, 0, 0))[4] == 1 for i in (0, 2, 3)), "the other three see Bravo down")
        ok((ns[0].get("coop", 0) >> 12) == 1 and ((ns[0].get("coop", 0) >> 7) & 31) - 1 == 1, "Alpha's client: HOLD F TO REVIVE BRAVO")
        sv_start = rig.server.log.size()
        A.cmd("+button2")
        m = rig.server.log.wait(r"Cat Arena: Bravo revived by Alpha t=(\d+)", 20, sv_start)
        A.cmd("-button2")
        ok(m, "Alpha held F on his own client: Bravo revived by Alpha")
        time.sleep(2)
        st_a = A.log.text()[s0[0]:]
        st_b = B.log.text()[s0[1]:]
        ok("status REVIVING BRAVO" in st_a and "status BEING REVIVED" in st_b and "status YOU ARE DOWN" in st_b,
           "status lines on the two clients: Alpha REVIVING BRAVO; Bravo YOU ARE DOWN, BEING REVIVED")
        ns = rig.netstat()
        ok(ns[1].get("health") == 45 and ns[1].get("downed") == 0, "Bravo back with 45 health on his client (%s)" % ns[1].get("health"))

        # a wave fought by all four, with the snapshot audit
        for c in rig.clients:
            c.cmd("god")
        time.sleep(1)
        rig.server.cmd("set ca_waves 1")
        for c in rig.clients:
            c.cmd('set ca_auto "ca_aimnearest"', "+attack")
        audit_start = A.log.size()
        ents, nets = [], []
        t_end = time.time() + 150
        shownet = False
        sv_wave = rig.server.log.size()
        while time.time() < t_end:
            ents.append(rig.entcount())
            nets.append(rig.netstat())
            if not shownet and len(nets) == 3:
                A.cmd("echo CA_SHOWNET_BEGIN", "cl_shownet 1")
                shownet = True
                time.sleep(10)
                A.cmd("cl_shownet 0", "echo CA_SHOWNET_END")
            if re.search(r"Cat Arena: wave 2 cleared", rig.server.log.text()[sv_wave:]) and all(r.get("score", 0) > 0 for r in rig.coopstate().values()):
                break
            time.sleep(3)
        for c in rig.clients:
            c.cmd('set ca_auto ""', "-attack")
        time.sleep(1)
        sv = rig.coopstate()
        wave_text = rig.server.log.text()[sv_wave:]
        ok("wave 1 cleared" in wave_text, "wave 1 cleared by the four")
        ok(all(sv[i]["score"] > 0 for i in range(4)), "every client killed: scores %s" % [sv[i]["score"] for i in range(4)])
        ok(all(any(n[i].get("enemies", 0) > 0 for n in nets) for i in range(4)), "every client received the enemies")
        sizes = shownet_sizes(A.log.text()[audit_start:])
        maxents = max((n[i].get("maxents", 0) for n in nets for i in range(4)), default=0)
        maxsent = max((e.get("max", 0) for e in ents), default=0)   # the server's peak, counted every frame
        data["audit"] = {"messages": len(sizes), "bytes_max": max(sizes, default=0), "bytes_mean": sum(sizes) / len(sizes) if sizes else 0,
                         "snapshot_entities_max": maxents, "server_sendable_max": maxsent,
                         "server_sendable_peak": ents[-1] if ents else {}}
        ok(len(sizes) > 100 and max(sizes) < 16384, "snapshot audit: %d server messages in 10 s on Alpha, largest %d bytes, mean %.0f (MAX_MSGLEN 16384)"
           % (len(sizes), max(sizes, default=0), data["audit"]["bytes_mean"]))
        ok(0 < maxents < 256 and maxsent < 256, "snapshot audit: at most %d entities in a client's snapshot, at most %d the server could send (MAX_SNAPSHOT_ENTITIES 256)" % (maxents, maxsent))

        # a client leaves and comes back
        s_sv = rig.server.log.size()
        D.cmd("disconnect")
        left = rig.server.log.wait(r"ClientDisconnect: 3", 20, s_sv)
        time.sleep(3)
        ns = rig.netstat()
        ok(left and all(sorted(players_of(ns[i])) == sorted({0, 1, 2} - {i}) for i in range(3)), "Delta left: the three others see two players each")
        s_sv = rig.server.log.size()
        D.cmd("connect 127.0.0.1:%d" % PORT)
        back = rig.server.log.wait(r"ClientBegin: 3\b", 120, s_sv)
        time.sleep(4)
        ns = rig.netstat()
        ok(back and all(len(players_of(ns[i])) == 3 for i in range(4)), "Delta came back: everybody sees three others again")

        # squad wipe with four real clients (god off: Delta came back without it)
        for c in rig.clients[:3]:
            c.cmd("god")
        time.sleep(2)
        rig.server.cmd("set ca_waves 0")
        s_sv = rig.server.log.size()
        s0 = [c.log.size() for c in rig.clients]
        rig.server.cmd(*["ca_hurt %d 300" % i for i in range(4)])
        wiped = rig.server.log.wait(r"Cat Arena: squad wiped, round restarts", 20, s_sv)
        time.sleep(3)
        after = rig.server.log.text()[wiped.end():] if wiped else ""
        ok(wiped and not re.search(r"Cat Arena: \w+ down", after), "nobody down again right after the restart (spawn spots, 2.5 s grace)")
        ns = rig.netstat()
        ok(wiped and all("status SQUAD ELIMINATED" in rig.clients[i].log.text()[s0[i]:] for i in range(4)) and all(ns[i].get("health") == 100 for i in range(4)),
           "all four down: SQUAD ELIMINATED on every client, everybody back at 100")

        # network figures for the record
        ns = rig.netstat()
        data["clients"] = ns
        data["memory_kb"] = rig.memory()
        pings = [ns[i].get("ping", 0) for i in range(4)]
        gaps = [ns[i].get("gaps", 0) for i in range(4)]
        snaps = [ns[i].get("snaps", 0) for i in range(4)]
        print("pings %s, snapshots %s, gaps %s, prediction misses %s" % (pings, snaps, gaps, [ns[i].get("misses") for i in range(4)]), flush=True)
        if a.delay:
            ok(all(p >= 2 * a.delay - 10 for p in pings), "latency: pings %s ms with %d ms each way" % (pings, a.delay))
        if a.loss:
            ok(all(g > 0 for g in gaps), "loss: every client lost snapshots (gaps %s of %s)" % (gaps, snaps))
        text = rig.server.log.text() + "".join(c.log.text() for c in rig.clients)
        ok("Signal caught" not in text and "ERROR:" not in text and all(c.proc.poll() is None for c in rig.clients) and rig.server.proc.poll() is None,
           "no errors, all five processes still running")
    finally:
        rig.close()
    with open(os.path.join(a.out, "data.json"), "w") as f:
        json.dump(data, f, indent=1, default=str)
    fails = sum(1 for c, _ in results if not c)
    verdict = "ALL PASS" if fails == 0 else "%d FAILED" % fails
    with open(os.path.join(a.out, "checks.txt"), "w") as f:
        f.write("".join(("PASS " if c else "FAIL ") + w + "\n" for c, w in results) + verdict + "\n")
    print(verdict)
    return fails == 0


def soak(a):
    rig = Rig(a.pkg, a.out, a.map, a.delay, a.loss, waves=True)
    spots = spawn_spots(a.root, a.map)
    rows = []
    t0 = time.time()
    try:
        for c in rig.clients:
            c.cmd("god", 'set ca_auto "ca_aimnearest"', "+attack")
        minute = 0
        while time.time() - t0 < a.minutes * 60:
            time.sleep(max(0, t0 + (minute + 1) * 60 - time.time()))
            minute += 1
            spot = spots[(minute * 7) % len(spots)]
            for i, c in enumerate(rig.clients):
                c.cmd("ca_place %.0f %.0f %.0f %d" % (spot[0] + (i % 2) * 64, spot[1] + (i // 2) * 64, spot[2], (minute * 47) % 360))
            row = {"minute": minute, "ents": rig.entcount(), "coop": rig.coopstate(), "net": rig.netstat(), "memory_kb": rig.memory(),
                   "running": [c.proc.poll() is None for c in rig.clients] + [rig.server.proc.poll() is None]}
            if minute % 10 == 5:   # ten seconds of message sizes on Alpha
                start = rig.clients[0].log.size()
                rig.clients[0].cmd("echo CA_SHOWNET_BEGIN", "cl_shownet 1")
                time.sleep(10)
                rig.clients[0].cmd("cl_shownet 0", "echo CA_SHOWNET_END")
                time.sleep(2)
                sz = shownet_sizes(rig.clients[0].log.text()[start:])
                row["shownet"] = {"messages": len(sz), "max": max(sz, default=0), "mean": sum(sz) / len(sz) if sz else 0}
            waves = re.findall(r"Cat Arena: wave (\d+), (\d+) enemies", rig.server.log.text())
            row["wave"] = int(waves[-1][0]) if waves else 0
            rows.append(row)
            print("minute %d: wave %s, sendable %s, memory %s, pings %s" % (minute, row["wave"], row["ents"].get("sent"), row["memory_kb"],
                  [row["net"][i].get("ping") for i in range(4)]), flush=True)
            with open(os.path.join(a.out, "soak.json"), "w") as f:
                json.dump(rows, f, indent=1, default=str)
            if not all(row["running"]):
                break
    finally:
        rig.close()
    results = []

    def ok(cond, what):
        results.append((bool(cond), what))
        print(("PASS " if cond else "FAIL ") + what, flush=True)

    last = rows[-1] if rows else {}
    text = rig.server.log.text()
    ok(rows and len(rows) >= a.minutes and all(all(r["running"]) for r in rows), "five processes ran %d minutes" % len(rows))
    ok(rows and all(len(r["coop"]) == 4 for r in rows) and all(len(players_of(r["net"][i])) == 3 for r in rows for i in range(4)),
       "four players connected and seen by every client every minute")
    cleared = len(re.findall(r"Cat Arena: wave \d+ cleared", text))
    restarts = len(re.findall(r"round restarts at wave 1", text))
    ok(cleared >= a.minutes // 3, "waves went on: %d waves cleared, highest wave %s, %d round restarts" % (cleared, max((r["wave"] for r in rows), default=0), restarts))
    ok(max((r["ents"].get("sent", 0) for r in rows), default=0) < 256 and max((r["net"][i].get("maxents", 0) for r in rows for i in range(4)), default=0) < 256,
       "snapshots under MAX_SNAPSHOT_ENTITIES: at most %d sendable, %d in a client's snapshot" % (
           max((r["ents"].get("max", 0) for r in rows), default=0), max((r["net"][i].get("maxents", 0) for r in rows for i in range(4)), default=0)))
    if len(rows) >= 6:
        k = len(rows) // 3
        growth = {}
        for who in rows[0]["memory_kb"]:
            early = sum(r["memory_kb"][who] for r in rows[k:2 * k]) / k
            late = sum(r["memory_kb"][who] for r in rows[-k:]) / k
            growth[who] = (late - early) / early * 100 if early else 0
        ok(all(g < 5 for g in growth.values()), "memory steady: growth from the middle third to the last third %s %%" % {w: round(g, 1) for w, g in growth.items()})
    sn = [r["shownet"] for r in rows if "shownet" in r]
    ok(sn and all(x["messages"] > 100 and x["max"] < 16384 for x in sn), "server messages on Alpha (10 s every 10 minutes): largest %s bytes, mean %s (MAX_MSGLEN 16384)"
       % ([x["max"] for x in sn], [round(x["mean"]) for x in sn]))
    ok("Signal caught" not in text and "ERROR:" not in text, "no errors on the server")
    fails = sum(1 for c, _ in results if not c)
    verdict = "ALL PASS" if fails == 0 else "%d FAILED" % fails
    with open(os.path.join(a.out, "checks.txt"), "w") as f:
        f.write("".join(("PASS " if c else "FAIL ") + w + "\n" for c, w in results) + verdict + "\n")
    print(verdict)
    return fails == 0


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("mode", choices=["run", "soak"])
    ap.add_argument("pkg")
    ap.add_argument("out")
    ap.add_argument("--delay", type=int, default=0)
    ap.add_argument("--loss", type=float, default=0)
    ap.add_argument("--map", default="ca_test")
    ap.add_argument("--minutes", type=int, default=30)
    ap.add_argument("--root", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
    a = ap.parse_args()
    a.pkg, a.out = os.path.abspath(a.pkg), os.path.abspath(a.out)
    sys.exit(0 if (run(a) if a.mode == "run" else soak(a)) else 1)
