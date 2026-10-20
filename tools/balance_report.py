#!/usr/bin/env python3
"""Balance harness, phase 1 (measure only): runs tests/balance_bot in parallel and writes build/balance/REPORT.md + CSV tables.

  tools/balance_report.py [--seeds 5000] [--jobs N] [--out build/balance] [--bot build/balance_bot]
                          [--skip-run] [--quick]

--skip-run   reuse the CSVs already in <out>/raw (re-render the report only)
--quick      500 seeds and fewer scenarios (a smoke test)

Pure standard library (no pandas). Never changes data/*.json: what-if knobs go through a tuning override file in <out>/tune.
"""
import argparse
import collections
import csv
import json
import math
import os
import re
import statistics
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

POLICIES = ["balanced", "reckless", "cautious", "grinder", "scroll-heavy", "charmer", "human", "sam", "balanced-notemple"]
# (scenario name, policy, extra bot args, tuning override or None, what it shows)
SCENARIOS = [(p, p, [], None, "policy") for p in POLICIES] + [
    ("orig-compare", "orig-compare", [], None, "compare"),  # a simplified run (attack only, no shopping, no scrolls) used as a fixed baseline
    ("balanced-noregen", "balanced", [], {"dungeon": {"regenSteps": 0}}, "ablation"),
    ("balanced-explore0", "balanced", ["--set", "explore=0"], None, "ablation"),
    ("balanced-explore300", "balanced", ["--set", "explore=300"], None, "ablation"),
    ("balanced-explore1000", "balanced", ["--set", "explore=1000"], None, "ablation"),
    ("whatif-player-dmg-x1p5", "balanced", [], {"combat": {"playerDamageMultiplier": 1.5}}, "knob"),
    ("whatif-monster-dmg-x0p5", "balanced", [], {"combat": {"monsterDamageMultiplier": 0.5}}, "knob"),
    ("whatif-monster-hp-x0p7", "balanced", [], {"monsters": {"hpMultiplier": 0.7}}, "knob"),
    ("whatif-xp-x2", "balanced", [], {"progression": {"killExpMultiplier": 2.0}}, "knob"),
    ("whatif-gold-x2", "balanced", [], {"loot": {"killGoldMultiplier": 2.0}}, "knob"),
    ("whatif-regen-every-5", "balanced", [], {"dungeon": {"regenSteps": 5}}, "knob"),
    ("whatif-levelup-heal-50", "balanced", [], {"progression": {"levelUpHealPercent": 50}}, "knob"),
    ("whatif-encounters-3pct", "balanced", [], {"encounters": {"monsterChancePercent": 3}}, "knob"),
    ("whatif-start-stats-12-22", "balanced", [], {"player": {"statRollMin": 12, "statRollMax": 22}}, "knob"),
]
QUICK = ["balanced", "reckless", "balanced-noregen", "orig-compare"]


# ---------------------------------------------------------------------------------------------- running the bot
def run_scenarios(args, out):
    raw = os.path.join(out, "raw")
    tune = os.path.join(out, "tune")
    os.makedirs(raw, exist_ok=True)
    os.makedirs(tune, exist_ok=True)
    jobs = []
    chosen = [s for s in SCENARIOS if not args.quick or s[0] in QUICK]
    for name, policy, extra, tuning, _ in chosen:
        tfile = None
        if tuning:
            tfile = os.path.join(tune, name + ".json")
            with open(tfile, "w") as f:
                json.dump(tuning, f, indent=1)
        n, k = args.seeds, args.jobs
        if "explore1000" in name:
            n = min(n, 1000)  # (very long runs)
        for i in range(k):
            lo, hi = 1 + n * i // k, n * (i + 1) // k
            if hi < lo:
                continue
            cmd = [args.bot, "--policy", policy, "--seeds", str(lo), str(hi), "--out", os.path.join(raw, f"{name}.{i}")] + extra
            if tfile:
                cmd += ["--tuning", tfile]
            if name not in DETAILED and name != "orig-compare":
                cmd += ["--brief"]  # the runs table is all the comparison needs
            jobs.append((name, cmd))
    t0 = time.time()

    def go(job):
        r = subprocess.run(job[1], capture_output=True, text=True)
        if r.returncode != 0:
            raise SystemExit(f"bot failed: {' '.join(job[1])}\n{r.stderr[-2000:]}")

    with ThreadPoolExecutor(max_workers=args.jobs) as ex:
        list(ex.map(go, jobs))
    for name, *_ in chosen:
        for kind in (("runs", "fights", "floors", "events") if (name in DETAILED or name == "orig-compare") else ("runs",)):
            merged = os.path.join(raw, f"{name}.{kind}.csv")
            with open(merged, "w") as o:
                first = True
                for i in range(args.jobs):
                    part = os.path.join(raw, f"{name}.{i}.{kind}.csv")
                    if not os.path.exists(part):
                        continue
                    with open(part) as p:
                        lines = p.readlines()
                    o.writelines(lines if first else lines[1:])
                    first = False
                    os.remove(part)
    for fn in os.listdir(raw):  # shard files of tables that were not merged (--brief scenarios)
        if re.search(r"\.\d+\.(runs|fights|floors|events)\.csv$", fn):
            os.remove(os.path.join(raw, fn))
    print(f"bot: {len(jobs)} jobs in {time.time() - t0:.1f}s", file=sys.stderr)


def load(out, name, kind):
    path = os.path.join(out, "raw", f"{name}.{kind}.csv")
    if not os.path.exists(path):
        return None
    with open(path, newline="") as f:
        return list(csv.DictReader(f))


# ---------------------------------------------------------------------------------------------- small helpers
def fnum(x):
    try:
        return float(x)
    except (TypeError, ValueError):
        return 0.0


def mean(xs):
    xs = list(xs)
    return sum(xs) / len(xs) if xs else 0.0


def pct(a, b):
    return 100.0 * a / b if b else 0.0


def quant(xs, q):
    xs = sorted(xs)
    if not xs:
        return 0
    return xs[min(len(xs) - 1, int(q * len(xs)))]


def md(headers, rows, align=None):
    out = ["| " + " | ".join(headers) + " |", "|" + "|".join(("---:" if (i > 0 and (align is None or align[i] == "r")) else ":---") for i in range(len(headers))) + "|"]
    for r in rows:
        out.append("| " + " | ".join(str(c) for c in r) + " |")
    return "\n".join(out)


def f1(x):
    return f"{x:.1f}"


def f2(x):
    return f"{x:.2f}"


def write_csv(out, name, headers, rows):
    with open(os.path.join(out, name), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(headers)
        w.writerows(rows)


# ---------------------------------------------------------------------------------------------- analyses
DETAILED = ("balanced", "human")  # only these scenarios get their fights/floors/events tables loaded (the others only need the runs: it is millions of rows otherwise)


class Scenario:
    def __init__(self, out, name):
        self.name = name
        self.runs = load(out, name, "runs") or []
        full = name in DETAILED
        self.fights = (load(out, name, "fights") or []) if full else []
        self.floors = (load(out, name, "floors") or []) if full else []
        self.events = (load(out, name, "events") or []) if full else []
        for r in self.runs:
            r["_maxf"] = int(r["max_floor"])
        self.n = len(self.runs)

    def depth(self):
        return [r["_maxf"] for r in self.runs]

    def headline(self):
        d = self.depth()
        n = max(1, len(d))
        return {
            "runs": self.n,
            "mean": mean(d),
            "median": statistics.median(d) if d else 0,
            "p90": quant(d, 0.9),
            "max": max(d) if d else 0,
            "floor1": pct(sum(1 for x in d if x == 1), n),
            "ge3": pct(sum(1 for x in d if x >= 3), n),
            "ge5": pct(sum(1 for x in d if x >= 5), n),
            "ge10": pct(sum(1 for x in d if x >= 10), n),
            "level": mean(fnum(r["hero_level"]) for r in self.runs),
            "score": mean(fnum(r["score"]) for r in self.runs),
            "steps": mean(fnum(r["steps"]) for r in self.runs),
            "fights": mean(fnum(r["fights"]) for r in self.runs),
            "cleared": mean(fnum(r["floors_cleared"]) for r in self.runs),
            "notdied": sum(1 for r in self.runs if r["result"] != "died"),
        }


def section_policies(scs, names):
    rows = []
    for n in names:
        s = scs.get(n)
        if not s or not s.runs:
            continue
        h = s.headline()
        rows.append([n, h["runs"], f2(h["mean"]), h["median"], h["p90"], h["max"], f1(h["floor1"]), f1(h["ge3"]), f1(h["ge5"]), f1(h["ge10"]), f1(h["level"]), f1(h["fights"]), int(h["score"]), h["notdied"]])
    return md(["policy", "runs", "mean deepest floor", "median", "p90", "max", "% die on floor 1", "% reach 3+", "% 5+", "% 10+", "hero level at end", "fights/run", "score", "not dead (capped/stuck)"], rows)


def section_depth(scs, names, maxf=15):
    heads = ["floor"] + [f"{n} reach%" for n in names if n in scs] + ["balanced: die here", "balanced: % die (of reached)"]
    s = scs["balanced"]
    d = s.depth()
    n = len(d)
    rows = []
    csvrows = []
    for f in range(1, maxf + 1):
        row = [f]
        for nm in names:
            if nm not in scs:
                continue
            dd = scs[nm].depth()
            row.append(f1(pct(sum(1 for x in dd if x >= f), len(dd))))
        reached = sum(1 for x in d if x >= f)
        died = sum(1 for r in s.runs if r["result"] == "died" and int(r["death_floor"]) == f)
        row += [died, f"{pct(died, reached):.1f}%"]
        rows.append(row)
        csvrows.append(row)
    write_csv(OUT, "depth.csv", heads, csvrows)
    return md(heads, rows)


def section_death_causes(s):
    died = [r for r in s.runs if r["result"] == "died"]
    n = max(1, len(died))
    byk = collections.Counter(r["killer"] for r in died)
    boss = sum(1 for r in died if r["killer_boss"] == "1")
    byc = collections.Counter(r["killer_class"] for r in died)
    out = [f"Of {len(died)} deaths, **{f1(pct(boss, n))}%** were to a floor boss and {f1(pct(n - boss, n))}% to a regular encounter.\n"]
    out.append(md(["class", "deaths", "%"], [[c, k, f1(pct(k, n))] for c, k in byc.most_common()]))
    out.append("\nTop killers (display name, so a prefix like 'Cursed' counts separately; bosses are marked):\n")
    kb = collections.Counter((r["killer"], r["killer_boss"]) for r in died)
    out.append(md(["killer", "boss", "deaths", "%", "typical floor"], [[k[0], "yes" if k[1] == "1" else "", c, f1(pct(c, n)),
                                                                          int(statistics.median(int(r["death_floor"]) for r in died if r["killer"] == k[0] and r["killer_boss"] == k[1]))] for k, c in kb.most_common(15)]))
    # deaths per floor: boss vs regular
    fl = collections.defaultdict(lambda: [0, 0])
    for r in died:
        fl[int(r["death_floor"])][0 if r["killer_boss"] == "1" else 1] += 1
    out.append("\nDeaths by floor:\n")
    out.append(md(["floor", "boss", "regular", "% boss"], [[f, fl[f][0], fl[f][1], f1(pct(fl[f][0], sum(fl[f])))] for f in sorted(fl)[:15]]))
    return "\n".join(out)


def floor_table(s):
    """Per floor: reached, died here, boss/regular fight stats, pacing and money."""
    reached = collections.Counter()
    died = collections.Counter()
    acc = collections.defaultdict(lambda: collections.defaultdict(list))
    for r in s.floors:
        f = int(r["floor"])
        reached[f] += 1
        died[f] += int(r["died"])
        for k in ("steps", "fights", "hp_lost", "regen_hp", "gold_earned", "level_in", "level_out"):
            acc[f][k].append(fnum(r[k]))
        acc[f]["boss_killed"].append(fnum(r["boss_killed"]))
    boss = collections.defaultdict(lambda: [0, 0])
    reg = collections.defaultdict(lambda: [0, 0, 0.0])
    classes = collections.defaultdict(collections.Counter)
    for r in s.fights:
        f = int(r["floor"])
        if r["boss"] != "1":
            classes[f][r["class"]] += 1
        if r["boss"] == "1":
            boss[f][0] += 1
            boss[f][1] += r["outcome"] == "died"
        else:
            reg[f][0] += 1
            reg[f][1] += r["outcome"] == "died"
            reg[f][2] += fnum(r["hp_lost"]) / max(1, fnum(r["hero_maxhp"]))
    rows = []
    prev_haz = None
    for f in range(1, 31):
        if reached[f] < 20:
            continue
        haz = pct(died[f], reached[f])
        flag = ""
        if haz >= 30 and (prev_haz is None or haz >= 1.4 * prev_haz) and f > 1:
            flag = "WALL"
        elif haz < 1.5 and reached[f] >= 100:
            flag = "dead zone"
        elif haz >= 30:
            flag = "hard"
        prev_haz = haz
        a = acc[f]
        rows.append({
            "floor": f, "reached": reached[f], "died": died[f], "hazard": haz,
            "boss_fights": boss[f][0], "boss_death": pct(boss[f][1], boss[f][0]),
            "reg_fights": reg[f][0], "reg_death": pct(reg[f][1], reg[f][0]), "reg_hp_pct": 100 * reg[f][2] / max(1, reg[f][0]),
            "steps": mean(a["steps"]), "fights": mean(a["fights"]), "hp_lost": mean(a["hp_lost"]), "regen": mean(a["regen_hp"]),
            "gold": mean(a["gold_earned"]), "lv_in": mean(a["level_in"]), "lv_out": mean(a["level_out"]), "flag": flag,
            "class": classes[f].most_common(1)[0][0] if classes[f] else ""})
    return rows


def section_floors(s):
    rows = floor_table(s)
    write_csv(OUT, "floors.csv", list(rows[0].keys()) if rows else [], [[(f"{v:.3f}" if isinstance(v, float) else v) for v in r.values()] for r in rows])
    return md(["floor", "floor's main class", "runs reaching", "died here", "% die (of reached)", "boss fights", "% die vs boss", "regular fights", "% die vs regular", "avg HP lost / regular fight (% of max)", "steps", "fights", "HP lost", "HP regen", "gold (chests)", "hero lvl in", "lvl out", "flag"],
              [[r["floor"], r["class"], r["reached"], r["died"], f1(r["hazard"]), r["boss_fights"], f1(r["boss_death"]), r["reg_fights"], f1(r["reg_death"]), f1(r["reg_hp_pct"]), f1(r["steps"]), f1(r["fights"]), f1(r["hp_lost"]), f1(r["regen"]), f1(r["gold"]), f1(r["lv_in"]), f1(r["lv_out"]), r["flag"]] for r in rows])


def section_hp_class(s, maxf=12):
    classes = ["HUMAN", "UNDEAD", "ANIMAL", "DEMON", "FAE"]
    acc = collections.defaultdict(lambda: [0, 0.0, 0.0])
    for r in s.fights:
        f = int(r["floor"])
        key = (f, "BOSS" if r["boss"] == "1" else r["class"])
        a = acc[key]
        a[0] += 1
        a[1] += fnum(r["hp_lost"])
        a[2] += 100 * fnum(r["hp_lost"]) / max(1, fnum(r["hero_maxhp"]))
    heads = ["floor"] + classes + ["BOSS"]
    rows, csvrows = [], []
    for f in range(1, maxf + 1):
        row = [f]
        crow = [f]
        for c in classes + ["BOSS"]:
            a = acc.get((f, c))
            if a and a[0] >= 10:
                row.append(f"{a[1] / a[0]:.1f} ({a[2] / a[0]:.0f}%) n={a[0]}")
                crow.append(f"{a[1] / a[0]:.2f}")
            else:
                row.append("")
                crow.append("")
        if any(x != "" for x in row[1:]):
            rows.append(row)
            csvrows.append(crow)
    write_csv(OUT, "hp_by_floor_class.csv", heads, csvrows)
    return "Average HP lost per fight (survived or not; in brackets as % of the hero's max HP), n = fights.\n\n" + md(heads, rows)


def monster_tables(s):
    # baseline death / hp-loss per (floor, boss) from all fights, so a kind is judged against what fights on its floors usually do
    base = collections.defaultdict(lambda: [0, 0, 0.0])
    for r in s.fights:
        b = base[(int(r["floor"]), r["boss"])]
        b[0] += 1
        b[1] += r["outcome"] == "died"
        b[2] += fnum(r["hp_lost"]) / max(1, fnum(r["hero_maxhp"]))
    per = {}
    for key_field in ("kind", "prefix"):
        agg = collections.defaultdict(lambda: {"n": 0, "died": 0, "won": 0, "fled": 0, "charmed": 0, "prayed": 0, "exp": 0.0, "hp": 0.0, "exp_hp": 0.0, "lvl": 0.0, "cls": ""})
        for r in s.fights:
            if r["boss"] == "1" and key_field == "prefix":
                continue
            k = r[key_field] or ("(none)" if key_field == "prefix" else r["kind"])
            a = agg[k]
            b = base[(int(r["floor"]), r["boss"])]
            a["n"] += 1
            a[r["outcome"]] += 1
            a["exp"] += b[1] / b[0]
            a["hp"] += fnum(r["hp_lost"]) / max(1, fnum(r["hero_maxhp"]))
            a["exp_hp"] += b[2] / b[0]
            a["lvl"] += fnum(r["m_level"])
            a["cls"] = r["class"]
        per[key_field] = agg
    return per


def section_monsters(s):
    per = monster_tables(s)
    out = []
    for field, title in (("kind", "Monster kinds"), ("prefix", "Prefixes (regular encounters only)")):
        agg = per[field]
        rows = []
        for k, a in agg.items():
            oe = a["died"] / a["exp"] if a["exp"] > 0 else 0
            hpr = a["hp"] / a["exp_hp"] if a["exp_hp"] > 0 else 0
            flag = ""
            if a["n"] >= 40:
                if oe >= 2.0 and a["died"] >= 10:
                    flag = "KILLER"
                elif oe >= 1.5 and a["died"] >= 10:
                    flag = "tough"
                elif a["died"] == 0 and hpr < 0.6:
                    flag = "trivial"
                elif hpr < 0.5:
                    flag = "trivial?"
                elif oe <= 0.4 and a["exp"] >= 5:
                    flag = "easy"
            if field == "kind" and a["n"] >= 20 and a["won"] + a["charmed"] == 0:
                flag = "NEVER BEATEN"
            rows.append((k, a, oe, hpr, flag))
        rows.sort(key=lambda t: (-t[2], -t[1]["n"]))
        csvrows = [[k, a["cls"], a["n"], a["died"], a["won"], a["fled"], a["charmed"], a["prayed"], f"{a['exp']:.2f}", f"{oe:.2f}", f"{hpr:.2f}", f"{a['lvl'] / a['n']:.1f}", flag] for k, a, oe, hpr, flag in rows]
        write_csv(OUT, "monsters.csv" if field == "kind" else "prefixes.csv", [field, "class", "fights", "died", "won", "fled", "charmed", "prayed", "expected_deaths", "obs_over_exp", "hp_loss_vs_floor_norm", "avg_level", "flag"], csvrows)
        flagged = [r for r in rows if r[4]]
        out.append(f"**{title}** ({len(rows)} entries; O/E = deaths observed / deaths expected from the average fight on the same floors; HP ratio = HP lost vs the floor's average fight). Flagged:\n")
        if flagged:
            out.append(md([field, "class", "fights", "died", "won", "fled", "O/E", "HP ratio", "avg lvl", "flag"],
                          [[k, a["cls"], a["n"], a["died"], a["won"], a["fled"], f2(oe), f2(hpr), f1(a["lvl"] / a["n"]), flag] for k, a, oe, hpr, flag in flagged[:40]]))
        else:
            out.append("(nothing flagged)")
        out.append("")
        ranked = [r for r in rows if r[1]["n"] >= 40]
        if ranked:
            show = ranked[:8] + [r for r in ranked[-8:] if r not in ranked[:8]]
            out.append(f"Most and least dangerous {field}s with 40+ fights (full list in the CSV):\n")
            out.append(md([field, "class", "fights", "died", "won", "O/E", "HP ratio", "avg lvl"],
                          [[k, a["cls"], a["n"], a["died"], a["won"], f2(oe), f2(hpr), f1(a["lvl"] / a["n"])] for k, a, oe, hpr, _ in show]))
            out.append("")
        if field == "kind":
            out.append("Per class (all kinds):\n")
            cagg = collections.defaultdict(lambda: [0, 0, 0, 0.0, 0.0])
            for k, a, oe, hpr, _ in rows:
                c = cagg[a["cls"]]
                c[0] += a["n"]
                c[1] += a["died"]
                c[2] += a["won"]
                c[3] += a["exp"]
                c[4] += a["hp"]
            out.append(md(["class", "fights", "died", "% die", "O/E", "avg HP lost (% of max)"], [[c, v[0], v[1], f1(pct(v[1], v[0])), f2(v[1] / v[3] if v[3] else 0), f1(100 * v[4] / v[0])] for c, v in sorted(cagg.items())]))
            out.append("")
    return "\n".join(out)


def section_gold(s):
    stock = collections.defaultdict(list)  # floor -> (price, gold)
    visits = collections.defaultdict(set)
    buys = collections.defaultdict(list)
    temple = collections.defaultdict(list)
    for e in s.events:
        f = int(e["floor"])
        if e["type"] == "stock":
            stock[f].append((fnum(e["a"]), fnum(e["c"]), fnum(e["b"])))
        elif e["type"] == "buy":
            buys[f].append(fnum(e["a"]))
        elif e["type"] == "temple":
            temple[f].append(fnum(e["a"]))
    # chests per floor and kind
    chest = collections.defaultdict(lambda: collections.defaultdict(list))
    for e in s.events:
        if e["type"].startswith("chest"):
            chest[int(e["floor"])][e["type"]].append(fnum(e["c"]))
    reached = collections.Counter(int(r["floor"]) for r in s.floors)
    gold_floor = collections.defaultdict(list)
    for r in s.floors:
        gold_floor[int(r["floor"])].append(fnum(r["gold_earned"]))
    rows, csvrows = [], []
    cum = 0.0
    for f in range(1, 16):
        if reached[f] < 20:
            continue
        g = mean(gold_floor[f])
        cum += g
        st = stock[f]
        med = statistics.median(p for p, _, _ in st) if st else 0
        aff = pct(sum(1 for p, gold, _ in st if p <= gold), len(st))
        avggold = mean(gold for _, gold, _ in st)
        c = chest[f]
        row = [f, f1(g), f1(cum), f1(mean(c["chest"])), f1(mean(c["chest_level"])), f1(mean(c["chest_boss"])), int(med), f1(aff), f1(avggold), len(buys[f]), f1(mean(buys[f])) if buys[f] else "", f"{len(stock[f]) // 7}"]
        rows.append(row)
        csvrows.append(row)
    heads = ["floor", "gold earned on floor", "cumulative (avg)", "avg regular chest", "avg level chest", "avg boss chest", "median shop price (items seen at hero lvl)", "% of stock affordable on arrival", "avg gold at shop", "purchases", "avg price paid", "shop visits"]
    write_csv(OUT, "gold.csv", heads, csvrows)
    ge = mean(fnum(r["gold_earned"]) for r in s.runs)
    gs = mean(fnum(r["gold_spent"]) for r in s.runs)
    ge_end = mean(fnum(r["gold_end"]) for r in s.runs)
    txt = f"Per run: earned {f1(ge)} gold, spent {f1(gs)} ({f1(pct(gs, ge))}%), left unspent at the end {f1(ge_end)}. Purchases: {f1(mean(fnum(r['buys']) for r in s.runs))} per run.\n\n"
    return txt + md(heads, rows)


def section_regen(scs):
    s = scs["balanced"]
    n = max(1, s.n)
    regen = mean(fnum(r["regen_hp"]) for r in s.runs)
    pot = mean(fnum(r["potion_hp"]) for r in s.runs)
    lv = mean(fnum(r["levelup_hp"]) for r in s.runs)
    tem = mean(fnum(r["temple_hp"]) for r in s.runs)
    tot = regen + pot + lv + tem
    lost = mean(sum(fnum(f["hp_lost"]) for f in [])) if False else sum(fnum(r["hp_lost"]) for r in s.floors) / n
    out = [f"HP recovered per run (balanced): walking regeneration {f1(regen)}, potions from chests {f1(pot)}, level-up heal {f1(lv)}, Temple {f1(tem)} (total {f1(tot)}); HP lost in fights per run {f1(lost)}. Share: regen {f1(pct(regen, tot))}%, potions {f1(pct(pot, tot))}%, Temple {f1(pct(tem, tot))}%.\n"]
    rows = []
    for nm, label in (("balanced", "baseline"), ("balanced-noregen", "no walking regeneration (regenSteps 0)"), ("balanced-notemple", "no Temple healing"), ("whatif-regen-every-5", "regeneration every 5 steps (x4)"), ("balanced-explore0", "no random exploring"), ("balanced-explore300", "300 steps exploring per floor"), ("balanced-explore1000", "1000 steps exploring per floor")):
        t = scs.get(nm)
        if not t or not t.runs:
            continue
        h = t.headline()
        rows.append([label, f2(h["mean"]), h["median"], f1(h["ge5"]), f1(h["level"]), f1(mean(fnum(r["steps"]) for r in t.runs))])
    out.append(md(["scenario (balanced policy)", "mean deepest floor", "median", "% reach 5+", "hero level", "steps/run"], rows))
    return "\n".join(out)


def section_levels(s):
    arr = collections.defaultdict(list)
    for r in s.floors:
        arr[int(r["floor"])].append(fnum(r["level_in"]))
    xp = {}
    rows = [[f, len(v), f1(mean(v)), f1(statistics.median(v)), f1(quant(v, 0.1)), f1(quant(v, 0.9))] for f, v in sorted(arr.items()) if len(v) >= 20 and f <= 15]
    return md(["floor", "runs", "mean hero level on arrival", "median", "p10", "p90"], rows)


def section_whatif(scs):
    base = scs["balanced"].headline()
    rows = []
    for name, policy, extra, tuning, kind in SCENARIOS:
        if kind != "knob" or name not in scs or not scs[name].runs:
            continue
        h = scs[name].headline()
        rows.append([name.replace("whatif-", ""), json.dumps(tuning), f2(h["mean"]), f"{h['mean'] - base['mean']:+.2f}", h["median"], f1(h["ge5"]), f1(h["ge10"]), f1(h["level"])])
    rows.insert(0, ["(baseline)", "defaults", f2(base["mean"]), "", base["median"], f1(base["ge5"]), f1(base["ge10"]), f1(base["level"])])
    write_csv(OUT, "whatif.csv", ["scenario", "override", "mean_floor", "delta", "median", "ge5", "ge10", "level"], rows)
    return md(["what-if (balanced policy)", "tuning override", "mean deepest floor", "delta", "median", "% 5+", "% 10+", "hero level"], rows)


def section_fights_overview(s):
    out = collections.Counter(r["outcome"] for r in s.fights)
    tot = sum(out.values())
    boss = collections.Counter(r["outcome"] for r in s.fights if r["boss"] == "1")
    bt = sum(boss.values())
    turns = mean(fnum(r["turns"]) for r in s.fights)
    return (f"{tot} fights. Outcomes: " + ", ".join(f"{k} {f1(pct(v, tot))}%" for k, v in out.most_common()) +
            f". Bosses ({bt}): " + ", ".join(f"{k} {f1(pct(v, bt))}%" for k, v in boss.most_common()) + f". Average {f1(turns)} turns per fight.")


def section_anomalies(scs):
    s = scs["balanced"]
    z = [e for e in s.events if e["type"] == "item_zero_hp"]
    runs_z = len({e["seed"] for e in z})
    lines = [f"- Chest items that would take the hero's max HP to 0 or below (the game allows it; the bot refuses to wear them): {len(z)} offered across {runs_z} of {s.n} runs. Examples: " + ", ".join(sorted({e['name'] for e in z})[:8]) + "." if z else "- No chest item pushed max HP to zero or below."]
    pr = sum(1 for f in s.fights if f["outcome"] == "prayed")
    lines.append(f"- Prayers answered (beamed to town): {pr} of {len(s.fights)} fights (the bot prays only below its HP threshold and when the chance is at least 15%, which it never is at the default numbers: section 10).")
    stuck = sum(1 for sc in scs.values() for r in sc.runs if r["result"] == "stuck")
    lines.append(f"- Runs where the bot got stuck (no progress for 4000 keys, or an unreachable goal): {stuck} over all scenarios; capped at floor {26 - 1}: " + str(sum(1 for sc in scs.values() for r in sc.runs if r["result"] == "capped")) + " (counted as the deepest floor 26).")
    return "\n".join(lines)


def section_risk(s):
    by_lv = collections.defaultdict(lambda: [0, 0, 0.0, 0, 0])
    by_delta = collections.defaultdict(lambda: [0, 0, 0.0])
    for r in s.fights:
        lv = int(r["hero_level"])
        boss = r["boss"] == "1"
        a = by_lv[(lv, boss)]
        a[0] += 1
        a[1] += r["outcome"] == "died"
        a[2] += 100 * fnum(r["hp_lost"]) / max(1, fnum(r["hero_maxhp"]))
        d = max(-4, min(4, int(r["hero_level"]) - int(r["m_level"])))
        b = by_delta[(d, boss)]
        b[0] += 1
        b[1] += r["outcome"] == "died"
        b[2] += 100 * fnum(r["hp_lost"]) / max(1, fnum(r["hero_maxhp"]))
    rows = []
    for lv in range(1, 13):
        r, b = by_lv.get((lv, False)), by_lv.get((lv, True))
        if not r or r[0] < 30:
            continue
        rows.append([lv, r[0], f1(pct(r[1], r[0])), f1(r[2] / r[0]), (b[0] if b else 0), f1(pct(b[1], b[0])) if b and b[0] >= 20 else ""])
    t1 = md(["hero level", "regular fights", "% die", "avg HP lost (% max)", "boss fights", "% die vs boss"], rows)
    rows = []
    for d in range(-4, 5):
        r, b = by_delta.get((d, False)), by_delta.get((d, True))
        if not r or r[0] < 30:
            continue
        rows.append([("%+d" % d) if abs(d) < 4 else ("<= -4" if d < 0 else ">= +4"), r[0], f1(pct(r[1], r[0])), f1(r[2] / r[0]), (b[0] if b else 0), f1(pct(b[1], b[0])) if b and b[0] >= 20 else ""])
    t2 = md(["hero level minus monster level", "regular fights", "% die", "avg HP lost (% max)", "boss fights", "% die vs boss"], rows)
    return "By the hero's own level:\n\n" + t1 + "\n\nBy level difference (monsters on a floor are about floor level; bosses about 1.1 x floor + 2):\n\n" + t2


def section_actions(s):
    acc = collections.defaultdict(lambda: [0, 0.0, 0.0, 0.0, 0.0, 0])
    for r in s.fights:
        f = int(r["floor"])
        a = acc[f]
        a[0] += 1
        if r["class"] != "UNDEAD":
            a[1] += fnum(r["est_charm"])
            a[5] += 1
        a[2] += fnum(r["est_trick"])
        a[3] += fnum(r["est_pray"])
        a[4] += r["outcome"] in ("charmed",)
    rows = [[f, a[0], f"{100 * a[1] / max(1, a[5]):.1f}%", f"{100 * a[2] / a[0]:.1f}%", f"{100 * a[3] / a[0]:.1f}%"] for f, a in sorted(acc.items()) if a[0] >= 50 and f <= 15]
    return ("Average chance that the other combat actions would work at the start of a fight (formulas copied from Combat; the bot only uses them above its thresholds: charm >= 50%, trick >= 50%, prayer >= 15%). "
            "Low numbers mean the action is effectively not an option.\n\n" + md(["floor", "fights", "Charm (non-undead)", "Trick (instant kill)", "Pray (beamed to town)"], rows))


def section_hero_stats(s):
    acc = collections.defaultdict(lambda: collections.defaultdict(list))
    for r in s.fights:
        if "hero_cha" not in r:
            continue
        lv = int(r["hero_level"])
        for k in ("str", "dex", "int", "vit", "cha"):
            acc[lv][k].append(fnum(r["hero_" + k]))
    def sd(v):
        m = mean(v)
        return (sum((x - m) ** 2 for x in v) / max(1, len(v))) ** 0.5
    rows = []
    for lv in sorted(acc):
        d = acc[lv]
        if len(d["cha"]) < 200 or lv > 20:
            continue
        rows.append([lv, len(d["cha"])] + [f"{mean(d[k]):.1f} (sd {sd(d[k]):.1f})" for k in ("str", "dex", "int", "vit", "cha")])
    return ("Hero stats at the start of a fight (including gear) by hero level, mean and spread over all fights of the balanced bot. This is the 'average INT / CHA for a level' the Charm and Trick targets are measured from. "
            "The bot's perks and gear rating favor STR/DEX/VIT, so its INT and CHA are those of a hero who does not invest in them.\n\n"
            + md(["hero level", "fights", "STR", "DEX", "INT", "VIT", "CHA"], rows))


def section_levers(scs):
    base = scs["balanced"].headline()["mean"]
    rows = []
    for n, s in scs.items():
        if n in POLICIES or n in ("balanced", "orig-compare"):
            continue
        rows.append((s.headline()["mean"] - base, n))
    rows.sort(reverse=True)
    return md(["scenario", "mean deepest floor delta vs balanced"], [[n, f"{d:+.2f}"] for d, n in rows])


# ---------------------------------------------------------------------------------------------- main
def git_rev():
    try:
        return subprocess.run(["git", "-C", ROOT, "rev-parse", "--short", "HEAD"], capture_output=True, text=True).stdout.strip()
    except Exception:
        return "?"


# ---------------------------------------------------------------------------------------------- target check
# The author's survival goals (tools/balance_survival_goals.txt): share of ALL runs that completed floor N. A loose slope, not hard numbers.
SURVIVAL_GOALS = {0: 100, 1: 92, 2: 89, 3: 80, 4: 72, 5: 65, 6: 56, 8: 50, 10: 40, 12: 32, 15: 20, 20: 11, 25: 5.5, 30: 2.5, 40: 0.8, 50: 0.2}


def goal_survival(n):
    ks = sorted(SURVIVAL_GOALS)
    for a, b in zip(ks, ks[1:]):
        if a <= n <= b:
            t = (n - a) / (b - a)
            return math.exp((1 - t) * math.log(SURVIVAL_GOALS[a]) + t * math.log(SURVIVAL_GOALS[b]))
    return SURVIVAL_GOALS[ks[-1]]


def section_target_check(scs, b):
    """One pass/fail row per decided target. A row with too little data says n/a, never a pass."""
    rows = []
    fl = {r["floor"]: r for r in floor_table(b)}
    d = b.depth()
    n = max(1, len(d))

    def add(name, goal, actual, ok):
        rows.append([name, goal, actual, "n/a" if ok is None else ("PASS" if ok else "**FAIL**")])

    # 1. survival curve: alive after floor N, as a share of ALL players. The player population is modelled as 25% `sam` (plays as smart as it can), 50% `human`
    # (a careful player) and 25% `balanced` (sloppy); tolerance max(6 points, 30% of the goal).
    mix = [(scs[n], w) for n, w in (("sam", 0.25), ("human", 0.5), ("balanced", 0.25)) if n in scs]
    wsum = sum(w for _, w in mix) or 1.0
    for f in (1, 3, 5, 8, 10, 15, 20):
        actual = sum(w * pct(sum(1 for x in sc.depth() if x >= f + 1), max(1, len(sc.depth()))) for sc, w in mix) / wsum
        goal = goal_survival(f)
        add(f"Completed floor {f} (% of all players: 25% sam, 50% human, 25% balanced)", f"{goal:.0f}% (+/- {max(6, 0.3 * goal):.0f})", f"{actual:.1f}%", abs(actual - goal) <= max(6, 0.3 * goal))
    # 2. no wall except the Boss10 floors; Boss10 kills about 35% of the heroes who face it
    walls = [f for f, r in fl.items() if r["hazard"] > 30 and f % 10 != 0 and r["reached"] >= 100]
    add("No floor kills over 30% of arrivals (except Boss10 floors)", "none", str(walls or "none"), not walls)
    for f in (10, 20):  # the share of the heroes who ARRIVE on the floor that its Boss10 kills
        arrived = [r for r in b.runs if r["_maxf"] >= f]
        killed = sum(1 for r in arrived if r["result"] == "died" and int(r["death_floor"]) == f and r["killer_boss"] == "1")
        if len(arrived) >= 100:
            share = pct(killed, len(arrived))
            add(f"Boss10 on floor {f}: % of arriving heroes it kills", "30-40%", f"{share:.0f}% (n={len(arrived)})", 30 <= share <= 40)
        else:
            add(f"Boss10 on floor {f}: % of arriving heroes it kills", "30-40%", "too few runs", None)
    # 3. recovery floor after Boss10: clearly easier than the floor 3 before it
    for f in (11, 21):
        r, ref = fl.get(f), fl.get(f - 3)
        if r and ref and r["reached"] >= 100 and ref["reached"] >= 100:
            add(f"Recovery floor {f}: hazard vs floor {f - 3}", f"<= floor {f - 3} + 3 pts", f"{r['hazard']:.0f}% vs {ref['hazard']:.0f}%", r["hazard"] <= ref["hazard"] + 3)
        else:
            add(f"Recovery floor {f}: hazard vs floor {f - 3}", f"<= floor {f - 3} + 3 pts", "too few runs", None)
    # 4. hero level about equal to the floor number (mean difference over floors 2-15 with 100+ runs)
    diffs = [r["lv_in"] - f for f, r in fl.items() if 2 <= f <= 15 and r["reached"] >= 100]
    if diffs:
        add("Hero level on arrival minus floor (mean, floors 2-15)", "within +/- 1", f"{mean(diffs):+.1f}", abs(mean(diffs)) <= 1.0)
    # 5. Charm 50% / Trick 33% for an average hero (balanced bot's fights; non-undead for Charm)
    ch = [fnum(r["est_charm"]) for r in b.fights if r["class"] != "UNDEAD" and r["boss"] != "1"]
    tr = [fnum(r["est_trick"]) for r in b.fights if r["boss"] != "1"]
    if ch:
        add("Average Charm chance, regular fights (not undead)", "50% (+/- 8)", f"{100 * mean(ch):.0f}%", abs(100 * mean(ch) - 50) <= 8)
    if tr:
        add("Average Trick chance, regular fights", "33% (+/- 8)", f"{100 * mean(tr):.0f}%", abs(100 * mean(tr) - 33) <= 8)
    pr = [fnum(r["est_pray"]) for r in b.fights]
    if pr:
        add("Pray chance never above its cap", "<= 50%", f"max {100 * max(pr):.0f}%", max(pr) <= 0.5001)
    # 5b. prefix tiers: HP lost in a fight vs a plain monster of the same floor (regular fights, floors 3-18): weak about 0.7, middle about 1.8, strong about 3
    tiers = {"weak": ("Pale", "Cursed", "Bloody"), "middle": ("Hungry", "Flaming", "Champion", "Damned"), "strong": ("Tainted", "Deep")}
    bands = {"weak": (0.4, 1.2), "middle": (1.4, 2.6), "strong": (2.1, 3.9)}
    plain = collections.defaultdict(list)
    pre = collections.defaultdict(list)
    for r in b.fights:
        if r["boss"] == "1" or not r["floor"].isdigit() or not 3 <= int(r["floor"]) <= 18:
            continue
        v = fnum(r["hp_lost"]) / max(1, fnum(r["hero_maxhp"]))
        if r["prefix"] in ("", "none", "(none)"):
            plain[int(r["floor"])].append(v)
        else:
            pre[r["prefix"]].append((int(r["floor"]), v))
    pm = {f: mean(v) for f, v in plain.items() if v}
    for tier, names in tiers.items():
        ratios = [v / pm[f] for nm in names for f, v in pre.get(nm, []) if pm.get(f)]
        if len(ratios) >= 200:
            lo, hi = bands[tier]
            add(f"Prefix tier {tier} ({', '.join(names)}): HP lost vs plain monster", f"{lo}-{hi}x", f"{mean(ratios):.2f}x (n={len(ratios)})", lo <= mean(ratios) <= hi)
    # 5c. an even ramp: no class's floors are much easier or harder than the others', and no single hit takes a big share of the hero's HP
    slot = collections.defaultdict(list)
    spikes = reg_n = 0
    for r in b.fights:
        if r["boss"] == "1" or not r["floor"].isdigit() or not 3 <= int(r["floor"]) <= 20:
            continue
        mh = max(1.0, fnum(r["hero_maxhp"]))
        slot[(int(r["floor"]) - 1) % 5].append(fnum(r["hp_lost"]) / mh)
        reg_n += 1
        spikes += fnum(r.get("max_hit", 0)) / mh >= 0.4
    if reg_n >= 1000 and all(slot.get(k) for k in range(5)):
        means = [mean(v) for v in slot.values()]
        add("Class floors: HP lost per regular fight, hardest slot / easiest slot", "<= 2.5x", f"{max(means) / max(1e-9, min(means)):.1f}x", max(means) / max(1e-9, min(means)) <= 2.5)
        add("Regular fights with one hit of 40%+ of max HP", "<= 0.3%", f"{100 * spikes / reg_n:.2f}%", spikes / reg_n <= 0.003)
    # 6. no dominant strategy: no policy goes much deeper than the balanced bot, grinding pays but not too much
    base = b.headline()["mean"]
    for nm, cap in (("scroll-heavy", 1.5), ("grinder", 1.5), ("balanced-explore300", 1.7)):
        if nm in scs:
            m = scs[nm].headline()["mean"]
            add(f"{nm}: mean deepest floor vs human", f"<= {cap}x", f"{m / base:.2f}x ({m:.1f} vs {base:.1f})", m / base <= cap)
    if "charmer" in scs:
        m = scs["charmer"].headline()["mean"]
        add("charmer (charm + hide + sell build): mean deepest floor vs human", "0.9x - 1.4x", f"{m / base:.2f}x ({m:.1f} vs {base:.1f})", 0.9 <= m / base <= 1.4)
    passed = sum(1 for r in rows if r[3] == "PASS")
    failed = sum(1 for r in rows if r[3].startswith("**"))
    head = f"**{passed} pass, {failed} fail, {len(rows) - passed - failed} n/a** (targets: tools/balance_survival_goals.txt; the curve tolerance is deliberately loose).\n\n"
    return head + md(["target", "goal", "human bot now", "result"], rows, align=["l", "l", "r", "r"])


OUT = "."


def main():
    global OUT
    ap = argparse.ArgumentParser()
    ap.add_argument("--seeds", type=int, default=5000)
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    ap.add_argument("--out", default=os.path.join(ROOT, "build/balance"))
    ap.add_argument("--bot", default=os.path.join(ROOT, "build/balance_bot"))
    ap.add_argument("--skip-run", action="store_true")
    ap.add_argument("--quick", action="store_true")
    args = ap.parse_args()
    if args.quick:
        args.seeds = min(args.seeds, 500)
    OUT = os.path.abspath(args.out)
    os.makedirs(OUT, exist_ok=True)
    if not args.skip_run:
        run_scenarios(args, OUT)
    scs = {}
    for name, *_ in SCENARIOS:
        s = Scenario(OUT, name)
        if s.runs:
            scs[name] = s
    if "balanced" not in scs:
        raise SystemExit("no balanced runs: run without --skip-run first")
    b = scs["balanced"]
    h = b.headline()
    pol_names = [p for p in POLICIES if p in scs]

    doc = []
    doc.append("# LabyTerm balance report (phase 1: measurement only)\n")
    doc.append(f"Generated {time.strftime('%Y-%m-%d %H:%M')} from commit `{git_rev()}`; {b.n} seeds per scenario; tuning = repo defaults (`data/gameplay.json`, your `~/.config/labyterm/tuning.json` is ignored). "
               "Nothing in `data/` was changed. Regenerate: `tools/build.sh --target balance_bot && tools/balance_report.py`.\n")
    doc.append("**How to read it.** A bot plays complete runs on the real game logic (`WalkSession`) by pressing keys. It is a competent but not clever player: "
               "it knows the whole map (so it walks less than a human), always takes the level chest then fights the boss, then rides down; it attacks by default, "
               "casts damage scrolls when it has MP, prays/hides when low, rides to the Temple when hurt and buys the gear that raises its own rating (3 STR + 2 DEX + 0.7 maxHP + 3 AC + w INT). "
               "It never uses secret doors/fountains, Wizard Eye, Invisibility, ESP, Feeble Mind, and never sells charmees. "
               "Policies change only the thresholds (see below). Numbers are **relative evidence**, not a verdict on how a human fares.\n")

    doc.append("## 0. Target check\n")
    doc.append(section_target_check(scs, scs.get("human", b)) + "\n")

    doc.append("## 1. Headlines\n")
    top = collections.Counter(r["killer"] for r in b.runs if r["result"] == "died").most_common(3)
    doc.append(f"- Balanced bot: median deepest floor **{h['median']}**, mean {f2(h['mean'])}, p90 {h['p90']}, best {h['max']}. **{f1(h['floor1'])}%** of runs die on floor 1, {f1(h['ge5'])}% reach floor 5, {f1(h['ge10'])}% reach floor 10.")
    doc.append(f"- Hero level at death: mean {f1(h['level'])}; {f1(h['cleared'])} floors cleared per run on average; {f1(h['fights'])} fights and {f1(h['steps'])} steps per run.")
    doc.append("- Most common killers: " + ", ".join(f"{k} ({c})" for (k, c) in top) + ".")
    fl = floor_table(b)
    walls = [r["floor"] for r in fl if r["flag"] == "WALL"]
    dead = [r["floor"] for r in fl if r["flag"] == "dead zone"]
    doc.append(f"- Floors flagged as walls: {walls or 'none'}; dead zones: {dead or 'none'} (rules in section 4).")
    doc.append(f"- Policy spread (mean deepest floor): " + ", ".join(f"{n} {f2(scs[n].headline()['mean'])}" for n in pol_names) + ".")
    if h["notdied"]:
        doc.append(f"- {h['notdied']} balanced runs did not end in death (hit the floor cap or got stuck): listed in `raw/balanced.runs.csv` (`result` column).")
    doc.append("")
    doc.append("### Fights overview\n\n" + section_fights_overview(b) + "\n")

    doc.append("## 2. Policies and sensitivity\n")
    doc.append("Policies: **balanced** (attack, cast Arrow/Missile and Holy Water, pray < 25% HP, hide < 40% with charmees, Temple trip < 40% HP, always takes the boss portal, buys gear and cheap scrolls, 100 random steps of exploring per floor), "
               "**reckless** (attack only, never retreats, portal only when < 50% HP, no scrolls, no exploring), **cautious** (retreats earlier at 60% HP, prays at 40%, and wanders on the floor until hero level >= 0.8 x floor before it fights the boss), **grinder** (cautious, but retreats at 50% and wants hero level >= floor + 1 before each boss: a patient human), "
               "**scroll-heavy** (balanced plus Sleep, Trick at 35% chance, INT perks and INT-weighted gear), **sam** (the author's own rules: re-rolls for DEX 17+ and CHA 15+, fountain first, then treasure and boss, town after every boss selling charmees under 2 levels above the hero, Hide often, charm when attacking does not work, Holy Water on undead, random +2 level-ups), **human** (balanced plus what a careful human does: heals in town under 60% HP and always before the boss, prays at 35% HP, 150 steps of exploring per floor), **charmer** (CHA-weighted gear, charms at 35% or better, hides behind charmees under 50% HP, sells outgrown charmees at BeastMart), **balanced-notemple** (ablation: no Temple healing at all), **orig-compare** (attack only, no shopping or scrolls: the run that section 12 plays on the original's code too).\n")
    doc.append(section_policies(scs, pol_names + [n for n, *_ in SCENARIOS if n.startswith("balanced-") and n not in POLICIES]) + "\n")
    doc.append("### Survival by floor (percent of runs that reach the floor)\n")
    doc.append(section_depth(scs, pol_names) + "\n")

    doc.append("## 3. Death causes (balanced)\n")
    doc.append(section_death_causes(b) + "\n")

    doc.append("## 4. Floors: walls and dead zones (balanced)\n")
    doc.append("Hazard = share of the runs that reached the floor and died on it. Flags: **WALL** = hazard >= 30% and at least 1.4x the floor before; **hard** = >= 30% but no sudden jump; **dead zone** = < 1.5% with 100+ runs (nothing happens there). "
               "Boss / regular death rates are per fight. Gold counts chests only (level chest + boss chest + regular chests).\n")
    doc.append(section_floors(b) + "\n")
    doc.append("### Hero level on arrival\n\n" + section_levels(b) + "\n")

    doc.append("## 5. HP lost per fight by floor and monster class (balanced)\n")
    doc.append(section_hp_class(b) + "\n")
    doc.append("### Fight risk by hero level and by level difference\n")
    doc.append(section_risk(b) + "\n")

    doc.append("### Hero stats by level\n")
    doc.append(section_hero_stats(b) + "\n")

    doc.append("## 6. Monsters and prefixes (balanced)\n")
    doc.append(section_monsters(b) + "\n")

    doc.append("## 7. Gold earned vs shop prices (balanced)\n")
    doc.append(section_gold(b) + "\n")

    doc.append("## 8. Regeneration and Temple healing\n")
    doc.append(section_regen(scs) + "\n")

    doc.append("## 9. What-if knobs (not applied: tuning override files in `tune/`)\n")
    doc.append(section_whatif(scs) + "\n")

    doc.append("## 10. The other combat actions\n")
    doc.append(section_actions(b) + "\n")

    doc.append("## 11. Anomalies and bot health\n")
    doc.append(section_anomalies(scs) + "\n")
    doc.append("### Levers ranked by effect on mean depth\n")
    doc.append(section_levers(scs) + "\n")

    with open(os.path.join(OUT, "REPORT.md"), "w") as f:
        f.write("\n".join(doc))
    write_csv(OUT, "policies.csv", ["policy", "runs", "mean", "median", "p90", "max", "pct_floor1", "pct_ge3", "pct_ge5", "pct_ge10", "level", "score"],
              [[n, s.headline()["runs"], f"{s.headline()['mean']:.3f}", s.headline()["median"], s.headline()["p90"], s.headline()["max"], f"{s.headline()['floor1']:.2f}", f"{s.headline()['ge3']:.2f}", f"{s.headline()['ge5']:.2f}", f"{s.headline()['ge10']:.2f}", f"{s.headline()['level']:.2f}", int(s.headline()["score"])] for n, s in scs.items()])
    print(f"wrote {os.path.join(OUT, 'REPORT.md')}")


if __name__ == "__main__":
    main()
