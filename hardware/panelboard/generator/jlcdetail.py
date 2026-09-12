#!/usr/bin/env python3
"""Full attribute dump for LCSC part numbers. Usage: jlcdetail.py C123 C456 ..."""
import sys
from jlcsearch import search

for code in sys.argv[1:]:
    hits = [c for c in search(code, 5, in_stock=False) if c["componentCode"] == code]
    if not hits:
        print(f"{code}: NOT FOUND")
        continue
    c = hits[0]
    print(f"== {code} {c.get('componentBrandEn')} {c.get('componentModelEn')} "
          f"stock={c.get('stockCount')} lib={c.get('componentLibraryType')}")
    print(f"   spec={c.get('componentSpecificationEn')}  describe={c.get('describe')}")
    for a in c.get("attributes") or []:
        v = a.get("attribute_value_name")
        if v and v != "-":
            print(f"   {a.get('attribute_name_en')}: {v}")
