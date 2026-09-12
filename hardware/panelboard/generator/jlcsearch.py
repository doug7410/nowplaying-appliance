#!/usr/bin/env python3
"""Search JLCPCB parts (same endpoint jlc-mcp uses). Usage: jlcsearch.py 'keyword' [limit]"""
import json, sys, urllib.request

def search(keyword, limit=8, in_stock=True):
    body = {"currentPage": 1, "pageSize": limit, "keyword": keyword, "searchType": 2}
    if in_stock:
        body["presaleType"] = "stock"
    req = urllib.request.Request(
        "https://jlcpcb.com/api/overseas-pcb-order/v1/shoppingCart/smtGood/selectSmtComponentList/v2",
        data=json.dumps(body).encode(),
        headers={"Content-Type": "application/json", "Accept": "application/json",
                 "User-Agent": "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36"})
    d = json.load(urllib.request.urlopen(req, timeout=30))
    if d.get("code") != 200:
        sys.exit(f"API error: {d.get('message')}")
    return d["data"].get("componentPageInfo", {}).get("list") or []

if __name__ == "__main__":
    kw = sys.argv[1]
    limit = int(sys.argv[2]) if len(sys.argv) > 2 else 8
    for c in search(kw, limit):
        price = c.get("componentPrices") or [{}]
        print(f"{c['componentCode']:>10}  {c.get('componentLibraryType','?'):8} "
              f"stock={c.get('stockCount',0):<7} ${price[0].get('productPrice','?'):<8} "
              f"{c.get('componentBrandEn','')} | {c.get('componentModelEn','')} | "
              f"{c.get('componentSpecificationEn','')} | {(c.get('describe') or '')[:80]}")
