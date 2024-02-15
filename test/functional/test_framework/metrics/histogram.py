"""Plot histograms and percentiles from the bitcoind.log

Usage:
  histogram.py [options] [<dir>... | --last]

Options:
  -h --help          Show this help.
  --last             Find the last experiment in the current folder, default.
  --histograms       Show only histograms
  --percentiles      Show only percentiles
  --only=<pattern>   Show only graphs with title containing pattern


Find all bitcoind.log files in the specified locations, extract last logged
histograms and display them

"""
import collections
import datetime
import dateutil.parser
import decimal
import itertools
import os
import pathlib
from typing import Iterator, List, Callable
from pathlib import Path

import matplotlib.pyplot as plt             # type: ignore
import numpy as np                          # type: ignore


class Histogram(dict):
    def __init__(self, data, maxVal=None, maxCount=None):
        super().__init__(data)
        self.overVal = maxVal
        self.overCount = maxCount

    def percentiles(self, q):
        if isinstance(q, (float, decimal.Decimal)):
            q = [q]
        q = iter(map(decimal.Decimal, sorted(q)))
        items = iter(self.items())
        total = decimal.Decimal(sum(self.values()) + (self.overCount or 0))
        target = next(q)
        k, v = next(items)
        cum = decimal.Decimal(v)
        try:
            while True:
                pct = cum / total
                if pct >= target:
                    yield (k, target)
                    target = next(q)
                else:
                    k, v = next(items)
                    cum += v
        except StopIteration:
            pass

    def items_(self, hide_overmax=False):
        yield from super().items()
        if not hide_overmax and self.overCount is not None:
            yield (self.overVal, self.overCount)

    def __repr__(self):
        if self.overCount is None:
            return f"Histogram({super().__repr__()})"
        else:
            return f"Histogram({super().__repr__()}, {self.overVal}, {self.overCount})"

    @classmethod
    def parse(cls, ser):
        return eval(ser, {"Histogram":cls})


HistogramLog = collections.namedtuple("HistogramLog", "dataset, ts, name, histogram")


def open_at_offset(offset: int = 0):
    def opener(*args, **kwargs):
        fd = os.open(*args, **kwargs)
        if offset < 0:
            if os.fstat(fd).st_size > -offset:
                os.lseek(fd, offset, os.SEEK_END)
            with open(fd, buffering=1, closefd=False) as f:
                # gobble a partial line
                f.readline()
        else:
            os.lseek(fd, offset, os.SEEK_SET)
            pass
        return fd
    return opener


def read_histograms(name: str, *, dataset=None, offset:int = 0) -> Iterator[HistogramLog]:
    bug = "An invalid reference: Node doesn't exist"
    pattern = " = Histogram("
    with open(name, opener=open_at_offset(offset)) as fd:
        for line in fd:
            found = line.find(pattern)
            if found < 0:
                continue
            b = line.find(bug)
            if b >= 0:
                line = line.replace(bug,"")
                found = line.find(pattern)
            histogram = line[found+3:].strip()
            parts = line[:found].strip().split()
            # , {"Histogram":Histogram}
            yield HistogramLog(dataset if dataset is not None else name,
                               " ".join(parts[0:2]),
                               parts[-1],
                               histogram)


def parse(log: HistogramLog) -> HistogramLog:
    ts = dateutil.parser.parse(log.ts)
    return HistogramLog(log.dataset, ts, log.name, Histogram.parse(log.histogram))


def percentile_range(nines=6, steps=5):
    pct = decimal.Decimal(0)
    delta = decimal.Decimal(1)
    limit = delta - delta / (decimal.Decimal(10) ** nines)
    while pct <= limit:
        delta = delta / 2
        step = delta / steps
        for _ in range(steps):
            pct += step
            yield pct


def strip_common_prefix(names):
    names = list(names)
    if len(names) < 2:
        return [n.split("/", 1)[0] for n in names]
    cp = 0
    for cp, x in enumerate(zip(*names)):
        if len(set(x)) == 1:
            continue
        else:
            break
    else:
        cp += 1

    return [name[cp:] for name in names]


def assert_eq(a,b):
    assert (a==b), f"{a} != {b}"


assert_eq(strip_common_prefix(["a/b"]), ["a"])
assert_eq(strip_common_prefix(["", ""]), ["", ""])
assert_eq(strip_common_prefix(["a", "a"]), ["", ""])
assert_eq(strip_common_prefix(["ab", "ac"]), ["b", "c"])
assert_eq(strip_common_prefix(["ab"]), ["ab"])


def flip(names):
    return [name[::-1] for name in names]


assert_eq(flip(["ab", "ac"]), ["ba", "ca"])


def unique_parts(names):
    names = list(map(str, names))
    front = strip_common_prefix(names)
    ret = flip(strip_common_prefix(flip(front)))
    return ret


assert_eq(unique_parts(["ab1c", "ad2c"]), ["b1", "d2"])
assert_eq(unique_parts(["a/b/c"]), ["a"])


def plot_histogram(ax, ls, *, bins=20):
    hide_overmax = ls[0].name.lower().endswith("_us")
    ps = [np.asarray(list(l.histogram.items_(hide_overmax))).transpose()
          for l in ls]
    minp = min(p[0].min() for p in ps)
    maxp = max(p[0].max() for p in ps)
    hs = [np.histogram(p[0], range=(minp, maxp), bins=bins, weights=p[1], density=False)
          for p in ps]
    hists, bins = list(zip(*hs))
    bins = [list(bin) for bin in bins]
    hists = [list(hist) for hist in hists]
    plt.hist([bin[:-1] for bin in bins], bins[0], weights=hists, label=unique_parts(l.dataset for l in ls), log=True)
    plt.title(ls[0].name)


def plot_percentiles(ax, ls, *, key):
    pr = list(percentile_range(12, 5))
    for l, p in zip(ls, unique_parts(l.dataset for l in ls)):
        ps = list(l.histogram.percentiles(pr))
        if not ps:
            continue
        x, y = tuple(zip(*ps))
        plt.plot(x, y, label=f"{p} - {l.name}")
#        for t in [1-3237/100000]:
#            xy = [(x,y) for x,y in zip(ps, pr) if y >= t]
#            if xy:
#                xy = xy[0]
#                plt.annotate(f"{100*xy[1]:.3f}% < {xy[0]}", xy)
    plt.title(key(ls[0]))
    plt.yscale("logit")
    plt.ylim((1e-1,1-1e-6))


def prefix(l):
    return l.name


def prefix_t(l):
    return l.name.replace("_CPU_", ".").replace("_TIME_", ".")


HistogramFilter = Callable[[HistogramLog], bool]


def load_histograms(fs: List[str], histogram_filter: HistogramFilter) -> List[HistogramLog]:
    def keep_last(it, *, key=lambda x:x.name):
        return {key(elt): elt for elt in it}.values()

    def read_last(f, dataset):
        return keep_last(read_histograms(f, dataset=dataset, offset=-200000000))

    return [parse(h)
            for f, dataset in zip(fs, unique_parts(fs))
            for h in read_last(f, dataset)
            if histogram_filter(h)]


def grouped(hists: List[HistogramLog], key):
    return dict((k, list(g))
                for k, g in itertools.groupby(
                    sorted(hists, key=key),
                    key=key)
                )


def histogram_filter_all(h: HistogramLog) -> bool:
    return True


def histogram_filter_matching(substr: str) -> HistogramFilter:
    if substr:
        def matching(h: HistogramLog) -> bool:
            return substr in h.name
        return matching
    else:
        return histogram_filter_all


def show_percentiles(logs, *,
                     key=prefix_t,
                     histogram_filter: HistogramFilter = histogram_filter_all):

    for r, (k,v) in enumerate(grouped(load_histograms(logs, histogram_filter), key=key).items(), 1):
        print("percentiles =======", k, [l.dataset for l in v])
        fig = plt.figure(figsize=(8,6))
        ax = fig.add_subplot(111)
        plot_percentiles(ax, v, key=key)
        plt.grid(which='major', axis='both')
        plt.legend()


def show_histograms(logs, *, key=prefix,
                    histogram_filter: HistogramFilter = histogram_filter_all):

    for r, (k,v) in enumerate(grouped(load_histograms(logs, histogram_filter), key=key).items(), 1):
        print("histogram =======", k, [l.dataset for l in v])
        fig = plt.figure(figsize=(8,6))
        ax = fig.add_subplot(111)
        plot_histogram(ax, v)
        plt.grid(which='major', axis='both')
        plt.legend()


def find_last_experiment(base: Path) -> List[str]:
    last = base / sorted(base.rglob("bitcoind.log"))[-1].relative_to(base).parts[0]
    print("last:", repr(last))
    return list(map(str, last.rglob("bitcoind.log") if last.is_dir() else [last]))


def filter_experiments(dirs: List[str]) -> List[Path]:
    return [path
            for glob in (dir.rglob("bitcoind.log") if dir.is_dir() else [dir]
                         for dir in map(Path, dirs))
            for path in glob
            if path.exists()]


def plot_last_experiment():
    fs = find_last_experiment()
    show_histograms(fs)
    show_percentiles(fs)


def main():
    from docopt import docopt                          # type: ignore
    args = docopt(__doc__, version="0.1")
    print(args)
    dirs = args["<dir>"]
    if dirs:
        paths = filter_experiments(dirs)
    else:
        paths = find_last_experiment(Path())
    histogram_filter = histogram_filter_matching(args["--only"])
    if args["--histograms"] or not args["--percentiles"]:
        show_histograms(paths, histogram_filter=histogram_filter)
    if not args["--histograms"] or args["--percentiles"]:
        show_percentiles(paths, histogram_filter=histogram_filter)

    plt.show()


if __name__ == "__main__":
    main()
