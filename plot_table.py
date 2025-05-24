#!/usr/bin/env python3
# plot_table_effect.py

import os
import re
import sys
import argparse
import matplotlib.pyplot as plt
from collections import OrderedDict

def parse_filename(fn):
    parts = os.path.basename(fn).split('_')
    if len(parts) < 6 or not fn.endswith('.txt'):
        raise ValueError(f"Bad format: {fn}")
    dataSize  = int(parts[-3])
    threads   = int(parts[-2])
    tableSize = int(parts[-1].replace('.txt',''))
    return dataSize, threads, tableSize

def parse_file(path):
    exec_ms = None
    col     = None
    with open(path, 'r') as f:
        for L in f:
            L = L.strip()
            if L.lower().startswith('executiontime'):
                m = re.search(r'([\d\.]+)', L)
                if m: exec_ms = float(m.group(1))
            elif L.lower().startswith('numberofhandledcollision'):
                m = re.search(r'(\d+)', L)
                if m: col = int(m.group(1))
    if exec_ms is None or col is None:
        raise ValueError(f"Missing data in {path}")
    return exec_ms, col

def plot_xy(xs, ys, xlabel, ylabel, title, outfn):
    plt.figure(figsize=(6,4))
    plt.plot(xs, ys, marker='o', linestyle='-', color='tab:blue')
    plt.xlabel(xlabel)
    plt.ylabel(ylabel)
    plt.title(title)
    plt.grid(True, linestyle='--', alpha=0.5)
    plt.tight_layout()
    plt.savefig(outfn, dpi=300)
    print(f"  -> saved {outfn}")
    plt.close()

def main():
    p = argparse.ArgumentParser(
        description="Plot Exec‐Time and Collisions vs Table‐Size "
                    "for fixed thread‐count and data‐size."
    )
    p.add_argument("--threads",   "-t", type=int, required=True,
                   help="thread count to filter on")
    p.add_argument("--data-size", "-d", type=int, required=True,
                   help="input data size to filter on")
    args = p.parse_args()

    res_dir = os.path.join(os.path.dirname(__file__), "results")
    if not os.path.isdir(res_dir):
        print("Error: cannot find 'results/' directory", file=sys.stderr)
        sys.exit(1)

    data = {}
    for fn in os.listdir(res_dir):
        if not fn.startswith("Results_") or not fn.endswith(".txt"):
            continue
        path = os.path.join(res_dir, fn)
        try:
            ds, th, ts = parse_filename(fn)
            if ds != args.data_size or th != args.threads:
                continue
            exec_ms, col = parse_file(path)
            data[ts] = (exec_ms, col)
        except Exception as e:
            print(f"Skipping {fn}: {e}", file=sys.stderr)

    if not data:
        print("No matching results found.", file=sys.stderr)
        sys.exit(1)

    ordered = OrderedDict(sorted(data.items()))

    tables = list(ordered.keys())
    execs  = [v[0] for v in ordered.values()]
    colls  = [v[1] for v in ordered.values()]

    plot_xy(
        tables, execs,
        xlabel="Table Size",
        ylabel="Execution Time (ms)",
        title=f"Exec Time vs Table Size  (data={args.data_size}, threads={args.threads})",
        outfn=f"exec_vs_table_{args.data_size}_{args.threads}.png"
    )

    plot_xy(
        tables, colls,
        xlabel="Table Size",
        ylabel="Handled Collisions",
        title=f"Collisions vs Table Size  (data={args.data_size}, threads={args.threads})",
        outfn=f"coll_vs_table_{args.data_size}_{args.threads}.png"
    )

if __name__ == "__main__":
    main()
