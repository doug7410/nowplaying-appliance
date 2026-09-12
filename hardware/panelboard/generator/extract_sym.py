#!/usr/bin/env python3
"""Extract a symbol block from a .kicad_sym, rename to Lib:Name, print pins."""
import sys, re

def extract(path, name, lib):
    txt = open(path).read()
    key = f'(symbol "{name}"'
    i = txt.index(key)
    # walk back to line start (indentation)
    ls = txt.rfind("\n", 0, i) + 1
    depth = 0; j = i
    while True:
        c = txt[j]
        if c == '"':  # skip strings
            j = txt.index('"', j + 1)
        elif c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                break
        j += 1
    block = txt[ls:j + 1]
    block = block.replace(f'(symbol "{name}"', f'(symbol "{lib}:{name}"', 1)
    # inner units keep "Name_0_1" style — fine as-is
    return block

if __name__ == "__main__":
    path, name, lib, out = sys.argv[1:5]
    b = extract(path, name, lib)
    open(out, "w").write(b)
    for m in re.finditer(r'\(pin \S+ \S+\s*\(at ([-\d.]+) ([-\d.]+) (\d+)\)[\s\S]*?\(name "([^"]*)"[\s\S]*?\(number "([^"]*)"', b):
        print("pin", m.group(5), m.group(4), "at", m.group(1), m.group(2), "rot", m.group(3))
