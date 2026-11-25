#!/usr/bin/env python3
"""Turns wardrive logs from the SD card (meshcore/wd-*.csv) into one KML file for Google Earth / Google My Maps.

Each sample is a dot coloured by SNR (green strong ... red weak, grey = ping not answered), with a thin line to the
repeater when its location is known. One folder per repeater, so they can be switched on and off.

Usage: wardrive_kml.py <wd-*.csv ...> [-o out.kml]
"""
import argparse
import csv
from collections import defaultdict
from xml.sax.saxutils import escape

BANDS = [(5, "ff00c000", "SNR >= 5"), (0, "ff00ffff", "0..5"), (-5, "ff0090ff", "-5..0"),
         (-999, "ff0000ff", "below -5")]   # KML colours are aabbggrr
GREY = "ff909090"


def colour(row):
    if row["Status"] == "0" or not row["SNR"]:
        return GREY
    snr = float(row["SNR"])
    return next(c for lim, c, _ in BANDS if snr >= lim)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv", nargs="+")
    ap.add_argument("-o", "--out", default="wardrive.kml")
    a = ap.parse_args()

    groups = defaultdict(list)
    for path in a.csv:
        with open(path, newline="", encoding="utf-8", errors="replace") as f:
            for row in csv.DictReader(f):
                key = row["Name"] or (f"[{row['Repeater']}]" if row["Repeater"] else "No answer")
                groups[key].append(row)

    styles = {c for _, c, _ in BANDS} | {GREY}
    out = ['<?xml version="1.0" encoding="UTF-8"?>', '<kml xmlns="http://www.opengis.net/kml/2.2"><Document>',
           "<name>MeshCore wardrive</name>"]
    for c in sorted(styles):
        out.append(f'<Style id="s{c}"><IconStyle><color>{c}</color><scale>0.6</scale><Icon><href>'
                   "http://maps.google.com/mapfiles/kml/shapes/shaded_dot.png</href></Icon></IconStyle>"
                   f'<LineStyle><color>60{c[2:]}</color><width>1</width></LineStyle></Style>')
    n = 0
    for key in sorted(groups):
        out.append(f"<Folder><name>{escape(key)}</name>")
        for r in groups[key]:
            what = {"5": "heard", "6": "answered ping", "0": "ping not answered"}.get(r["Status"], r["Status"])
            desc = f"{what}; SNR {r['SNR'] or '-'} dB, RSSI {r['RSSI'] or '-'}, they heard us {r['RemoteSNR'] or '-'}" \
                   f"; {r['Packet']} ~{r['Accuracy']} m; t={r['Time']}"
            geo = f"<Point><coordinates>{r['Longitude']},{r['Latitude']}</coordinates></Point>"
            if r["RptLat"] and r["RptLon"]:
                geo = (f"<MultiGeometry>{geo}<LineString><coordinates>{r['Longitude']},{r['Latitude']} "
                       f"{r['RptLon']},{r['RptLat']}</coordinates></LineString></MultiGeometry>")
            out.append(f"<Placemark><styleUrl>#s{colour(r)}</styleUrl><description>{escape(desc)}</description>"
                       f"{geo}</Placemark>")
            n += 1
        out.append("</Folder>")
    out.append("</Document></kml>")
    with open(a.out, "w", encoding="utf-8") as f:
        f.write("\n".join(out))
    print(f"{n} samples, {len(groups)} repeaters -> {a.out}")


if __name__ == "__main__":
    main()
