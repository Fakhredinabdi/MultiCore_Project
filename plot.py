import os
import re
import sys
import matplotlib.pyplot as plt
from collections import defaultdict

def parse_filename(fn):
    base = os.path.basename(fn)
    parts = base.split('_')
    if len(parts) < 6:
        raise ValueError(f"unexpected filename format: {fn}")
    dataSize   = int(parts[-3])
    threads    = int(parts[-2])
    tableSize  = int(parts[-1].replace('.txt',''))
    return dataSize, threads, tableSize

def parse_file(path):
    exec_ms    = None
    collisions = None
    with open(path, 'r') as f:
        for line in f:
            line = line.strip()
            if line.lower().startswith('executiontime'):
                m = re.search(r'([\d\.]+)', line)
                if m:
                    exec_ms = float(m.group(1))
            elif line.lower().startswith('numberofhandledcollision'):
                m = re.search(r'(\d+)', line)
                if m:
                    collisions = int(m.group(1))
    if exec_ms is None or collisions is None:
        raise ValueError(f"could not parse {path}")
    return exec_ms, collisions

def plot_series(threads_list, values_list, ylabel, title, out_png):
    plt.figure(figsize=(6,4))
    plt.plot(threads_list, values_list, marker='o', linewidth=2)
    plt.xscale('log', base=2)
    plt.xticks(threads_list, threads_list)
    plt.xlabel("Number of Threads")
    plt.ylabel(ylabel)
    plt.title(title)
    plt.grid(True, linestyle='--', alpha=0.5)
    plt.tight_layout()
    plt.savefig(out_png, dpi=300)
    print(f"  -> saved {out_png}")
    plt.close()

def main():
    base_dir = os.path.dirname(__file__)
    res_dir  = os.path.join(base_dir, "results")
    if not os.path.isdir(res_dir):
        print("ERROR: 'results/' directory not found", file=sys.stderr)
        sys.exit(1)

    stats = defaultdict(dict)

    for fn in os.listdir(res_dir):
        if not fn.startswith("Results_") or not fn.endswith(".txt"):
            continue
        path = os.path.join(res_dir, fn)
        try:
            ds, th, ts = parse_filename(fn)
            exec_ms, col = parse_file(path)
        except Exception as e:
            print(f"Skipping {fn}: {e}", file=sys.stderr)
            continue
        stats[(ds, ts)][th] = (exec_ms, col)

    for (ds, ts), th_map in sorted(stats.items()):
        threads = sorted(th_map.keys())
        execs   = [th_map[t][0] for t in threads]
        colls   = [th_map[t][1] for t in threads]

        title = f"Exec Time (data={ds}, table={ts})"
        fname = f"execTime_{ds}_{ts}.png"
        plot_series(threads, execs, "Time (ms)", title, fname)

        title = f"Collisions (data={ds}, table={ts})"
        fname = f"collisions_{ds}_{ts}.png"
        plot_series(threads, colls, "Handled Collisions", title, fname)

    print("All done.")

if __name__ == "__main__":
    main()
