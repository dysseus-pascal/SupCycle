#!/usr/bin/env python3
"""App-Symbol: Pilly, die Kapsel (25x25).

Aufruf: make_app_icon.py <zielordner>
Erzeugt system_icon.png - schwarze Linien auf durchsichtigem Grund.

Dieselbe Form wie src/c/pill_fx.c, nur ohne Gesicht: bei 25 Punkten waeren
Augen und Mund drei Flecken, die alles verschmieren. Was Pilly bei dieser
Groesse erkennbar macht, ist die geteilte Kapsel, nicht die Miene.

MASSSTAB IST DAS SYSTEMSYMBOL. Die Uhr-Kachel von "Watchfeces" im Starter wurde
Punkt fuer Punkt nachgemessen: 24 von 25 Punkten hoch, Linien 2 bis 3 Punkte
stark, rund 180 schwarze Punkte. Danach richten sich Groesse und Strichstaerke
hier - eine duennere Linie sieht daneben aus wie ein Versehen.

NUR LINIEN, KEINE FLAECHE, und keine ~bw-Fassung. Der Starter zeichnet Symbole
einfarbig: eine farbige Flaeche kam dort als grauer Fleck heraus (im Emulator
nachgemessen - Rot 255,0,0 wurde zu Grau 171,171,171). Eine schwarze Linie ist
auf jeder Uhr dieselbe Datei.

Die Teilungslinie gehoert dazu: ohne sie waere es ein abgerundetes Rechteck
und keine Kapsel. Sie ist so stark wie die Kontur.
"""
import os
import struct
import sys
import zlib

W = H = 25
SS = 4                           # Ueberabtastung je Achse
# Drei statt zwei: die Kapsel ist mit 14 Punkten nur halb so breit wie die
# Systemuhr, ihr Umriss ist also entsprechend kuerzer. Mit zwei Punkten Linie
# kam sie auf 128 schwarze Punkte gegen 180 beim Vorbild und wirkte daneben
# blass. Das Vorbild selbst hat 2 bis 3 - drei bleibt im Rahmen.
LINE = 3                         # Strichstaerke in Punkten

CX, CY = 12.0, 12.0              # Mitte
HW = 7.0                         # halbe Breite
HH = 11.5                        # halbe Hoehe


def png(path, w, h, rows):
    """Minimaler PNG-Schreiber, 8 Bit RGBA, ohne Fremdbibliothek."""
    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff)
    raw = b"".join(b"\x00" + bytes(r) for r in rows)
    out = b"\x89PNG\r\n\x1a\n"
    out += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    out += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(out)


def raster(test):
    """Vierfach ueberabtasten, bei halber Deckung schneiden. Harte Kanten."""
    grid = []
    for py in range(H):
        row = []
        for px in range(W):
            hits = 0
            for sy in range(SS):
                for sx in range(SS):
                    if test(px + (sx + 0.5) / SS, py + (sy + 0.5) / SS):
                        hits += 1
            row.append(hits * 2 >= SS * SS)
        grid.append(row)
    return grid


def write(dest, grid):
    rows = []
    for y in range(H):
        r = []
        for x in range(W):
            r += [0, 0, 0, 255] if grid[y][x] else [0, 0, 0, 0]
        rows.append(r)
    png(os.path.join(dest, "system_icon.png"), W, H, rows)
    old = os.path.join(dest, "system_icon~bw.png")
    if os.path.exists(old):
        os.remove(old)
        print("system_icon~bw.png entfernt - die Linie gilt fuer alle Uhren")
    n = sum(1 for r in grid for v in r if v)
    ys = [y for y in range(H) if any(grid[y])]
    print("system_icon.png: %d Punkte schwarz, %d hoch (Vorbild: 180 / 24)"
          % (n, (ys[-1] - ys[0] + 1) if ys else 0))


def erode(grid, n):
    """n-mal den Rand abtragen."""
    cur = grid
    for _ in range(n):
        nxt = []
        for y in range(H):
            row = []
            for x in range(W):
                keep = cur[y][x]
                if keep:
                    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                        nx, ny = x + dx, y + dy
                        if nx < 0 or ny < 0 or nx >= W or ny >= H or not cur[ny][nx]:
                            keep = False
                            break
                row.append(keep)
            nxt.append(row)
        cur = nxt
    return cur


def ring(grid, thick):
    """Der Rand der Form, thick Punkte stark.

    Gerechnet als Flaeche minus abgetragener Flaeche - so ist die Linie ueberall
    gleich stark, auch in flachen Winkeln, wo ein Nachbarschaftstest duenner
    wuerde.
    """
    inner = erode(grid, thick)
    return [[grid[y][x] and not inner[y][x] for x in range(W)] for y in range(H)]


def inside(x, y):
    """Ein Stadion: Rechteck mit halbrunden Enden. Gerechnet als Abstand zur
    senkrechten Mittelstrecke, verglichen mit der halben Breite."""
    dx = x - CX
    dy = y - CY
    span = HH - HW                   # halbe Laenge des geraden Teils
    if dy > span:
        dy -= span
    elif dy < -span:
        dy += span
    else:
        dy = 0.0
    return (dx * dx + dy * dy) ** 0.5 <= HW


def main():
    dest = sys.argv[1] if len(sys.argv) > 1 else "resources/images"
    os.makedirs(dest, exist_ok=True)
    solid = raster(inside)
    line = ring(solid, LINE)
    # Teilungslinie quer durch die Kapsel, mittig und gleich stark.
    mid = int(round(CY))
    for dy in range(LINE):
        y = mid - LINE // 2 + dy
        for x in range(W):
            if solid[y][x]:
                line[y][x] = True
    write(dest, line)


if __name__ == "__main__":
    main()
