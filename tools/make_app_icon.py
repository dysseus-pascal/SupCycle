#!/usr/bin/env python3
"""App-Symbol: Pilly, die Kapsel.

Aufruf: make_app_icon.py <zielordner>          -> system_icon.png (25x25)
        make_app_icon.py --store <zielordner>  -> icon-144.png, icon-48.png

Dieselbe Form wie src/c/pill_fx.c, nur ohne Gesicht: bei 25 Punkten waeren
Augen und Mund drei Flecken, die alles verschmieren. Was Pilly bei dieser
Groesse erkennbar macht, ist die geteilte Kapsel, nicht die Miene.

MASSSTAB IST DAS SYSTEMSYMBOL. Die Uhr-Kachel von "Watchfaces" im Starter wurde
Punkt fuer Punkt nachgemessen: 24 von 25 Punkten hoch, Linien 2 bis 3 Punkte
stark, rund 180 schwarze Punkte. Danach richten sich Groesse und Strichstaerke
hier - eine duennere Linie sieht daneben aus wie ein Versehen.

NUR LINIEN, KEINE FLAECHE, und keine ~bw-Fassung. Der Starter zeichnet Symbole
einfarbig: eine farbige Flaeche kam dort als grauer Fleck heraus (im Emulator
nachgemessen - Rot 255,0,0 wurde zu Grau 171,171,171). Eine schwarze Linie ist
auf jeder Uhr dieselbe Datei.

Die Teilungslinie gehoert dazu: ohne sie waere es ein abgerundetes Rechteck
und keine Kapsel. Sie ist so stark wie die Kontur.

DER STORE NIMMT NICHTS AUS DER .pbw. Im Entwicklerportal liegen zwei eigene
Bilder, `icon_large` und `icon_small`; angefordert werden sie in festen Massen
(gross 80 und 144, klein 28 und 48), jeweils mit `exact` in der Adresse, also
erzwungen statt eingepasst - etwas Nicht-Quadratisches kommt verzogen zurueck.
Darum hier eine gefuellte Kachel: das grosse Symbol legt der Store fuer sein
Teilen-Bild durch eine abgerundete Maske, und ueber einer durchsichtigen
Strichzeichnung taete die nichts. Auf der Kachel darf die Teilungslinie
endlich rot sein - auf der Uhr waere sie ein grauer Fleck.

DIE FORM STEHT NUR EINMAL DA. Alle Masse gelten auf einem Raster von 25
Punkten und werden mit s hochgerechnet; mit s = 1 kommt Punkt fuer Punkt das
alte Bild heraus.
"""
import os
import struct
import sys
import zlib

RASTER = 25                      # Bezugsraster, auf dem alle Masse gelten
SS = 4                           # Ueberabtastung je Achse
# Drei statt zwei: die Kapsel ist mit 14 Punkten nur halb so breit wie die
# Systemuhr, ihr Umriss ist also entsprechend kuerzer. Mit zwei Punkten Linie
# kam sie auf 128 schwarze Punkte gegen 180 beim Vorbild und wirkte daneben
# blass. Das Vorbild selbst hat 2 bis 3 - drei bleibt im Rahmen.
LINE = 3                         # Strichstaerke in Punkten

CX, CY = 12.0, 12.0              # Mitte
HW = 7.0                         # halbe Breite
HH = 11.5                        # halbe Hoehe

# Store-Kachel. Die Werte stammen aus src/c/theme.h (SC_COLOR_SIDEBAR und
# SC_COLOR_PILL), nachgeschlagen in gcolor_definitions.h des SDK - nicht aus
# dem Gedaechtnis.
GRUND = (0x00, 0x55, 0x55)       # GColorMidnightGreen
STRICH = (0xFF, 0xFF, 0xFF)      # weiss
TEILUNG = (0xFF, 0x00, 0x00)     # GColorRed
FUELL = 0.72                     # wie viel der Kachel die Kapsel einnimmt
STORE_GROESSEN = (144, 48)


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


def raster(test, n):
    """Vierfach ueberabtasten, bei halber Deckung schneiden. Harte Kanten."""
    grid = []
    for py in range(n):
        row = []
        for px in range(n):
            hits = 0
            for sy in range(SS):
                for sx in range(SS):
                    if test(px + (sx + 0.5) / SS, py + (sy + 0.5) / SS):
                        hits += 1
            row.append(hits * 2 >= SS * SS)
        grid.append(row)
    return grid


def erode(grid, k, n):
    """k-mal den Rand abtragen."""
    cur = grid
    for _ in range(k):
        nxt = []
        for y in range(n):
            row = []
            for x in range(n):
                keep = cur[y][x]
                if keep:
                    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                        nx, ny = x + dx, y + dy
                        if nx < 0 or ny < 0 or nx >= n or ny >= n or not cur[ny][nx]:
                            keep = False
                            break
                row.append(keep)
            nxt.append(row)
        cur = nxt
    return cur


def ring(grid, thick, n):
    """Der Rand der Form, thick Punkte stark.

    Gerechnet als Flaeche minus abgetragener Flaeche - so ist die Linie ueberall
    gleich stark, auch in flachen Winkeln, wo ein Nachbarschaftstest duenner
    wuerde.
    """
    inner = erode(grid, thick, n)
    return [[grid[y][x] and not inner[y][x] for x in range(n)] for y in range(n)]


def kapsel(n):
    """Kontur und Teilungslinie auf einem Raster von n Punkten.

    Getrennt zurueckgegeben, weil die Store-Kachel sie verschieden einfaerbt -
    auf der Uhr sind beide schwarz.
    """
    s = n / float(RASTER)
    cx, cy, hw, hh = CX * s, CY * s, HW * s, HH * s
    dicke = max(1, int(round(LINE * s)))

    def inside(x, y):
        """Ein Stadion: Rechteck mit halbrunden Enden. Gerechnet als Abstand zur
        senkrechten Mittelstrecke, verglichen mit der halben Breite."""
        dx = x - cx
        dy = y - cy
        span = hh - hw                   # halbe Laenge des geraden Teils
        if dy > span:
            dy -= span
        elif dy < -span:
            dy += span
        else:
            dy = 0.0
        return (dx * dx + dy * dy) ** 0.5 <= hw

    voll = raster(inside, n)
    kontur = ring(voll, dicke, n)

    # Teilungslinie quer durch die Kapsel, mittig und gleich stark.
    teilung = [[False] * n for _ in range(n)]
    mid = int(round(cy))
    for dy in range(dicke):
        y = mid - dicke // 2 + dy
        if 0 <= y < n:
            for x in range(n):
                if voll[y][x]:
                    teilung[y][x] = True
    return kontur, teilung


def schreibe_uhr(dest):
    n = RASTER
    kontur, teilung = kapsel(n)
    rows = []
    for y in range(n):
        r = []
        for x in range(n):
            gesetzt = kontur[y][x] or teilung[y][x]
            r += [0, 0, 0, 255] if gesetzt else [0, 0, 0, 0]
        rows.append(r)
    png(os.path.join(dest, "system_icon.png"), n, n, rows)
    old = os.path.join(dest, "system_icon~bw.png")
    if os.path.exists(old):
        os.remove(old)
        print("system_icon~bw.png entfernt - die Linie gilt fuer alle Uhren")
    punkte = sum(1 for y in range(n) for x in range(n) if kontur[y][x] or teilung[y][x])
    ys = [y for y in range(n) if any(kontur[y][x] or teilung[y][x] for x in range(n))]
    print("system_icon.png: %d Punkte schwarz, %d hoch (Vorbild: 180 / 24)"
          % (punkte, (ys[-1] - ys[0] + 1) if ys else 0))


def schreibe_store(dest):
    for gross in STORE_GROESSEN:
        innen = int(round(gross * FUELL))
        kontur, teilung = kapsel(innen)
        rand = (gross - innen) // 2
        rows = []
        for y in range(gross):
            r = []
            for x in range(gross):
                iy, ix = y - rand, x - rand
                farbe = GRUND
                if 0 <= iy < innen and 0 <= ix < innen:
                    if kontur[iy][ix]:
                        farbe = STRICH
                    elif teilung[iy][ix]:
                        farbe = TEILUNG
                r += [farbe[0], farbe[1], farbe[2], 255]
            rows.append(r)
        name = "icon-%d.png" % gross
        png(os.path.join(dest, name), gross, gross, rows)
        print("%s: Kachel %s, Kapsel weiss, Teilung %s"
              % (name, "#%02X%02X%02X" % GRUND, "#%02X%02X%02X" % TEILUNG))


def main():
    args = sys.argv[1:]
    store = "--store" in args
    if store:
        args.remove("--store")
    dest = args[0] if args else ("store" if store else "resources/images")
    os.makedirs(dest, exist_ok=True)
    if store:
        schreibe_store(dest)
    else:
        schreibe_uhr(dest)


if __name__ == "__main__":
    main()
