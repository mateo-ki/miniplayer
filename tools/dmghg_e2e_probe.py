#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""端到端探针：新主网关 -> 列表/详情/播放 -> jx 二次解析(新旧域名对比)。"""
import sys, json
sys.path.insert(0, r"D:/project/ida/dm/scripts")
import dmghg_api as api
import jx_parse as jx

api.HOST = "http://bkbfdm.hzhcbkj.cn"

def show(tag, result):
    status, parsed = result
    if isinstance(parsed, dict):
        print(f"  [{tag}] status={status} code={parsed.get('code')} msg={parsed.get('message', parsed.get('msg',''))}")
        return parsed
    print(f"  [{tag}] status={status} body={str(parsed)[:120]}")
    return None

def e2e():
    print("== 1) video list ==")
    lst = show("list", api.get_video_list(channel=0, page=1, limit=5, sort="hits"))
    items = (lst.get("data") or {}).get("items") or (lst.get("data") or {}).get("list") or []
    if not items:
        print("  no items, abort"); return
    vid = items[0]["id"]
    name = items[0].get("name") or items[0].get("title")
    print(f"  picked vid={vid} name={name}")

    print("== 2) detail ==")
    det = show("detail", api.get_video_detail(vid))
    parts = (det.get("data") or {}).get("parts") or []
    if not parts:
        print("  no parts, abort"); return
    play = parts[0].get("play", "cn")
    part = (parts[0].get("part") or ["第01集"])[0]
    print(f"  play={play} part={part}")

    print("== 3) play (source id) ==")
    ply = show("play", api.get_play(vid, part, play))
    data = ply.get("data") or []
    if not data:
        print("  no play data, abort"); return
    source = data[0].get("url", "")
    print(f"  source={source}")

    print("== 4) jx parse ==")
    for tag, endpoint in (("new-jx", "http://jxbfdm.xn--2qq276avrz.top/?md5="),
                          ("old-jx", "http://new.jx.dokiapp.tech/?md5=")):
        try:
            jx.API_ENDPOINT = endpoint
            res = jx.parse(source)
            status = res.get("status")
            if status == "OK":
                print(f"  [{tag}] OK items={len(res['items'])}")
                for it in res["items"][:2]:
                    print(f"      url_hint={it['url'][:110]} type={it['type']}")
            elif status == "ERROR":
                print(f"  [{tag}] ERROR error={res.get('error')} obj={str(res.get('obj', res.get('body','')))[:120]}")
            else:
                print(f"  [{tag}] {status} obj={str(res.get('obj',''))[:160]}")
        except Exception as e:
            print(f"  [{tag}] FAIL {type(e).__name__}: {e}")

if __name__ == "__main__":
    e2e()
