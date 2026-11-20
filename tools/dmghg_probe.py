#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""dmghg 网关探针：用现有协议常量在多个候选域名上请求 pc/config，报告响应码。"""
import sys, json
sys.path.insert(0, r"D:/project/ida/dm/scripts")
import dmghg_api as api

CANDIDATE_HOSTS = [
    "http://bljhm.dokiapp.tech",        # 旧主网关 (miniplayer 当前使用)
    "http://bkbfdm.hzhcbkj.cn",         # 概念版 2026-09-12 实测成功的域名
    "http://139.199.159.222:7862",      # 概念版 runtime_last_gateway
    "http://bkbf.xn--vhqr42drhf5k7b.com",  # 概念版 legacy_host_fallback
]

def probe(host):
    try:
        api.HOST = host
        status, parsed = api.get_config()
        if isinstance(parsed, dict):
            return f"status={status} code={parsed.get('code')} msg={parsed.get('message', parsed.get('msg', ''))}"
        return f"status={status} body={str(parsed)[:80]}"
    except Exception as e:
        return f"FAIL {type(e).__name__}: {e}"

if __name__ == "__main__":
    for host in CANDIDATE_HOSTS:
        print(f"{host}\n    ->  {probe(host)}")
