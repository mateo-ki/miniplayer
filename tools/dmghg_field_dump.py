#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Dump the full key set of the anime detail/play responses to look for intro/outro seconds."""
import sys, json

sys.path.insert(0, r"D:/project/ida/dm/scripts")
import dmghg_api as api

OUT = r"D:/project/cpp/miniplayer/dmghg_keys.txt"
out = open(OUT, "w", encoding="utf-8")


def w(*args):
    print(*args, file=out)
    out.flush()


def keys(obj):
    return json.dumps(sorted(obj.keys()), ensure_ascii=False) if isinstance(obj, dict) else repr(obj)[:120]


for host in ("http://bljhm.dokiapp.tech", "http://bkbfdm.hzhcbkj.cn"):
    try:
        api.HOST = host
        st, lst = api.get_video_list(channel=0, page=1, limit=3, sort="hits")
        data = (lst or {}).get("data") or {}
        items = data.get("items") or data.get("list") or []
        if not items:
            w(f"HOST {host}: list status={st} no items -> {keys(lst)}")
            continue
        w(f"HOST {host} OK, list items={len(items)}")
        w("  list item keys:", keys(items[0]))
        vid = items[0].get("id")
        st, det = api.get_video_detail(vid)
        d = (det or {}).get("data") or {}
        w("  detail status:", st)
        w("  detail keys:", keys(d))
        parts = d.get("parts") or []
        if parts:
            w("  parts[0] keys:", keys(parts[0]))
            plist = parts[0].get("part") or []
            if plist:
                first = plist[0] if isinstance(plist[0], str) else json.dumps(plist[0], ensure_ascii=False)
                w("  parts[0].part[0]:", first[:80])
                st, play = api.get_play(vid, first)
                w("  play status:", st)
                w("  play top keys:", keys(play))
                pd = (play or {}).get("data")
                w("  play.data keys:", keys(pd) if isinstance(pd, dict) else repr(pd)[:200])
                w("  play raw (2000):", json.dumps(play, ensure_ascii=False)[:2000])
        w("  detail raw (2500):", json.dumps(d, ensure_ascii=False)[:2500])
        break
    except Exception as exc:
        w(f"HOST {host}: FAILED {type(exc).__name__}: {exc}")

out.close()
print("wrote", OUT)
