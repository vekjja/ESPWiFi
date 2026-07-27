#!/usr/bin/env python3
"""Generate wiring diagrams for ESPWiFi ham-radio interface."""

from __future__ import annotations

from pathlib import Path

import matplotlib.pyplot as plt
from matplotlib.patches import Circle, FancyBboxPatch, Rectangle

ROOT = Path(__file__).resolve().parents[1]
OUT_DIR = ROOT / "docs" / "radio"

# Shared style
FONT = "DejaVu Sans"
COL = {
    "wire": "#1e293b",
    "muted": "#64748b",
    "esp": "#dbeafe",
    "esp_edge": "#1d4ed8",
    "radio": "#dcfce7",
    "radio_edge": "#15803d",
    "rx": "#cffafe",
    "tx": "#fef9c3",
    "ptt": "#ffedd5",
    "passive": "#fef3c7",
    "passive_edge": "#b45309",
    "gnd": "#334155",
}


def _setup(title: str, w: float = 11, h: float = 5.5):
    fig, ax = plt.subplots(figsize=(w, h))
    ax.set_aspect("equal")
    ax.axis("off")
    ax.set_title(title, fontsize=14, weight="bold", pad=14, fontfamily=FONT)
    return fig, ax


def _save(fig, name: str):
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    path = OUT_DIR / name
    fig.savefig(path, dpi=180, bbox_inches="tight", facecolor="white")
    plt.close(fig)
    print(f"wrote {path}")


def _label(ax, x, y, text, size=9, color=COL["wire"], ha="center", va="center", bold=False):
    ax.text(
        x,
        y,
        text,
        fontsize=size,
        color=color,
        ha=ha,
        va=va,
        fontfamily=FONT,
        weight="bold" if bold else "normal",
    )


def _block(ax, cx, cy, w, h, text, fc, ec, size=9):
    patch = FancyBboxPatch(
        (cx - w / 2, cy - h / 2),
        w,
        h,
        boxstyle="round,pad=0.02,rounding_size=0.15",
        linewidth=1.4,
        edgecolor=ec,
        facecolor=fc,
    )
    ax.add_patch(patch)
    _label(ax, cx, cy, text, size=size)


def _dot(ax, x, y):
    ax.add_patch(Circle((x, y), 0.08, fc=COL["wire"], ec=COL["wire"], zorder=5))


def _hline(ax, x1, x2, y, label=None):
    ax.plot([x1, x2], [y, y], color=COL["wire"], linewidth=2, solid_capstyle="round", zorder=2)
    if label:
        _label(ax, (x1 + x2) / 2, y + 0.35, label, size=8, color=COL["muted"])


def _vline(ax, x, y1, y2):
    ax.plot([x, x], [y1, y2], color=COL["wire"], linewidth=2, solid_capstyle="round", zorder=2)


def _cap(ax, x, y, label="10 µF"):
    w = 0.55
    gap = 0.14
    _hline(ax, x - w, x - gap, y)
    _hline(ax, x + gap, x + w, y)
    _vline(ax, x - gap, y - 0.32, y + 0.32)
    _vline(ax, x + gap, y - 0.32, y + 0.32)
    _label(ax, x, y - 0.65, label, size=8, color=COL["muted"])


def _resistor(ax, x, y, label="47 kΩ", vertical=False):
    if vertical:
        rect = Rectangle((x - 0.14, y - 0.55), 0.28, 1.1, fc=COL["passive"], ec=COL["passive_edge"], lw=1.2, zorder=3)
        ax.add_patch(rect)
        _label(ax, x + 0.55, y, label, size=8, color=COL["muted"], ha="left")
    else:
        rect = Rectangle((x - 0.55, y - 0.14), 1.1, 0.28, fc=COL["passive"], ec=COL["passive_edge"], lw=1.2, zorder=3)
        ax.add_patch(rect)
        _label(ax, x, y + 0.45, label, size=8, color=COL["muted"])


def _gnd(ax, x, y):
    _vline(ax, x, y, y - 0.35)
    ax.plot([x - 0.35, x + 0.35], [y - 0.35, y - 0.35], color=COL["gnd"], lw=2)
    ax.plot([x - 0.22, x + 0.22], [y - 0.5, y - 0.5], color=COL["gnd"], lw=2)
    ax.plot([x - 0.1, x + 0.1], [y - 0.62, y - 0.62], color=COL["gnd"], lw=2)


def _vplus(ax, x, y, label="3.3 V"):
    _vline(ax, x, y, y + 0.35)
    _label(ax, x, y + 0.65, label, size=9, color=COL["gnd"], bold=True)


def diagram_radio_jack():
    fig, ax = _setup("Kenwood / Baofeng 2-pin accessory jack", w=10, h=6)
    ax.set_xlim(0, 10)
    ax.set_ylim(0, 6)

    # 2.5 mm jack symbol
    cx, cy = 2.2, 4.2
    ax.add_patch(Circle((cx, cy), 0.55, fill=False, ec=COL["radio_edge"], lw=2))
    ax.add_patch(Circle((cx, cy), 0.18, fc=COL["radio_edge"]))
    _label(ax, cx, cy - 1.05, "2.5 mm", size=10, bold=True)
    _block(ax, cx, 5.35, 2.4, 0.9, "Speaker / RX", COL["rx"], COL["radio_edge"], size=9)

    # 3.5 mm jack symbol
    cx2 = 2.2
    ax.add_patch(Circle((cx2, 1.8), 0.75, fill=False, ec=COL["radio_edge"], lw=2))
    ax.add_patch(Circle((cx2, 1.8), 0.28, fc=COL["radio_edge"]))
    _label(ax, cx2, 0.55, "3.5 mm", size=10, bold=True)
    _block(ax, cx2, 2.95, 2.4, 0.9, "Mic + PTT", COL["tx"], COL["radio_edge"], size=9)

    _vline(ax, cx, 3.65, 4.75)
    _label(ax, 2.2, 3.35, "12 mm", size=8, color=COL["muted"])

    rows = [
        (5.0, "2.5 mm TIP", "Speaker audio out  →  ESP32 GPIO 34 (RX)"),
        (4.35, "2.5 mm SLEEVE", "Ground  →  ESP32 GND"),
        (2.45, "3.5 mm TIP", "Mic in (+5 V bias)  →  ESP32 GPIO 25 (TX)"),
        (1.8, "3.5 mm SLEEVE", "PTT  →  GPIO 12 via transistor (short to GND to TX)"),
    ]
    for y, pin, desc in rows:
        _label(ax, 4.0, y, pin, size=9, ha="left", bold=True, color=COL["radio_edge"])
        _label(ax, 5.55, y, desc, size=9, ha="left")

    _save(fig, "01-radio-jack.png")


def diagram_overview():
    fig, ax = _setup("ESP32 ↔ radio — signal overview", w=10, h=4.8)
    ax.set_xlim(0, 10)
    ax.set_ylim(0, 4.8)

    rows = [
        ("GPIO 34", "10 µF cap + 47k/47k bias", "2.5 mm TIP", "Speaker out (RX)"),
        ("GPIO 25", "10 µF + 47 kΩ bleeder + optional 2.2k", "3.5 mm TIP", "Mic in (+5 V bias)"),
        ("GPIO 12", "1 kΩ + NPN/MOSFET", "3.5 mm SLEEVE", "PTT (short to GND)"),
        ("GND", "Direct wire", "2.5 mm SLEEVE", "Common ground"),
    ]

    col_x = [0.05, 0.22, 0.52, 0.72]
    col_w = [0.16, 0.28, 0.18, 0.26]
    headers = ["ESP32", "Interface", "Radio pin", "Signal"]
    y = 0.82
    h = 0.11

    for i, header in enumerate(headers):
        rect = Rectangle(
            (col_x[i], y),
            col_w[i],
            h,
            transform=ax.transAxes,
            fc=COL["esp"] if i == 0 else COL["radio"] if i >= 2 else "#f1f5f9",
            ec=COL["wire"],
            lw=1,
        )
        ax.add_patch(rect)
        ax.text(
            col_x[i] + col_w[i] / 2,
            y + h / 2,
            header,
            transform=ax.transAxes,
            ha="center",
            va="center",
            fontsize=9,
            weight="bold",
            fontfamily=FONT,
        )

    for r, row in enumerate(rows):
        yy = 0.82 - (r + 1) * h
        fc = "#ffffff" if r % 2 == 0 else "#f8fafc"
        for i, cell in enumerate(row):
            rect = Rectangle(
                (col_x[i], yy),
                col_w[i],
                h,
                transform=ax.transAxes,
                fc=fc,
                ec=COL["wire"],
                lw=0.8,
            )
            ax.add_patch(rect)
            ax.text(
                col_x[i] + col_w[i] / 2,
                yy + h / 2,
                cell,
                transform=ax.transAxes,
                ha="center",
                va="center",
                fontsize=8.5,
                fontfamily=FONT,
            )

    ax.text(
        0.5,
        0.06,
        "Half-duplex: record while receiving — TX (chime + playback) only after squelch closes.",
        transform=ax.transAxes,
        ha="center",
        fontsize=9,
        color=COL["muted"],
        fontfamily=FONT,
    )

    _save(fig, "02-system-overview.png")


def diagram_rx():
    fig, ax = _setup("RX — GPIO 34 with AC coupling and bias divider")
    ax.set_xlim(0, 12)
    ax.set_ylim(0, 6)

    node_x, node_y = 6.5, 3.2

    _block(ax, 1.3, node_y, 1.8, 1.0, "Radio\nSPK+", COL["rx"], COL["radio_edge"])
    _cap(ax, 3.3, node_y, "10 µF\n+ → radio")
    _block(ax, 10.5, node_y, 1.8, 1.0, "GPIO 34\nADC in", COL["esp"], COL["esp_edge"])

    _hline(ax, 2.2, 3.0, node_y)
    _hline(ax, 3.6, node_x, node_y)
    _dot(ax, node_x, node_y)
    _hline(ax, node_x, 9.6, node_y)

    # Bias divider: 3.3V --47k-- node --47k-- GND
    div_x = node_x
    _vplus(ax, div_x, 5.0)
    _vline(ax, div_x, 5.35, 4.45)
    _resistor(ax, div_x, 3.85, "47 kΩ", vertical=True)
    _vline(ax, div_x, 3.3, node_y)

    _vline(ax, div_x, node_y, 2.55)
    _resistor(ax, div_x, 1.85, "47 kΩ", vertical=True)
    _vline(ax, div_x, 1.3, 0.95)
    _gnd(ax, div_x, 0.95)
    _label(ax, div_x + 0.15, 3.2, "~1.65 V idle", size=8, color=COL["muted"], ha="left")

    _vline(ax, 1.3, 2.7, 0.95)
    _gnd(ax, 1.3, 0.95)
    _label(ax, 1.3, 0.35, "Radio GND", size=8, color=COL["muted"])

    _label(ax, 6.0, 5.55, "Bias divider centers the ADC input at ~1.65 V when idle.", size=9, color=COL["muted"])

    _save(fig, "03-rx-gpio34.png")


def diagram_tx():
    fig, ax = _setup("TX — GPIO 25 to radio mic input")
    ax.set_xlim(0, 12)
    ax.set_ylim(0, 5.2)

    node_x, y = 7.0, 2.8
    gnd_y = 0.75

    _block(ax, 10.5, y, 1.8, 1.0, "GPIO 25\nDAC out", COL["esp"], COL["esp_edge"])
    _resistor(ax, 8.8, y, "2.2 kΩ\n(optional)", vertical=False)
    _cap(ax, 4.8, y, "10 µF\n+ → radio")
    _block(ax, 1.3, y, 2.0, 1.0, "Radio\n3.5 mm TIP\n+5 V bias", COL["tx"], COL["radio_edge"])

    _hline(ax, 9.6, 9.35, y)
    _hline(ax, 8.25, node_x, y)
    _dot(ax, node_x, y)
    _hline(ax, node_x, 5.15, y)

    # 47k bleeder: ESP-side node → GND
    _vline(ax, node_x, gnd_y + 0.15, y)
    _resistor(ax, node_x, 1.75, "47 kΩ", vertical=True)
    _gnd(ax, node_x, gnd_y)
    _label(ax, node_x + 0.2, 2.35, "ESP-side\nnode", size=8, color=COL["muted"], ha="left")

    _label(ax, 6.0, 0.35, "Cap blocks radio +5 V bias. 47 kΩ bleeds the ESP-side node to GND.", size=9, color=COL["muted"])

    _save(fig, "04-tx-gpio25.png")


def diagram_ptt():
    fig, ax = _setup("PTT — GPIO 12 keys radio via transistor", w=11, h=4.5)
    ax.set_xlim(0, 11)
    ax.set_ylim(0, 4.5)

    y = 2.6
    gnd_y = 0.7
    qx = 5.4

    _block(ax, 1.0, y, 1.5, 0.85, "GPIO 12", COL["esp"], COL["esp_edge"])
    _resistor(ax, 2.55, y, "1 kΩ")
    _block(ax, 9.2, y, 2.2, 0.85, "Radio PTT\n3.5 mm sleeve", COL["tx"], COL["radio_edge"])

    _hline(ax, 1.75, 2.0, y)
    _hline(ax, 3.1, qx, y)  # base

    # NPN C-E vertical, base at midpoint
    _vline(ax, qx, y - 0.5, y + 0.5)
    _label(ax, qx - 0.55, y + 0.15, "B/G", size=8, color=COL["muted"], ha="right")
    _label(ax, qx - 0.15, y + 0.75, "C/D", size=8, color=COL["muted"], ha="right")
    _label(ax, qx - 0.15, y - 0.75, "E/S", size=8, color=COL["muted"], ha="right")
    _label(ax, qx + 0.25, y + 1.05, "2N2222 / 2N7000", size=8, color=COL["muted"], ha="left")

    # Collector → PTT (no permanent ground on PTT line)
    _hline(ax, qx, 8.1, y + 0.5)
    _vline(ax, 8.1, y, y + 0.5)
    _hline(ax, 8.1, 8.1, y)

    # Emitter → GND
    _vline(ax, qx, gnd_y + 0.15, y - 0.5)
    _gnd(ax, qx, gnd_y)

    _label(ax, 5.5, 0.22, "GPIO HIGH → transistor ON → PTT shorted to GND → radio keys up", size=9, color=COL["muted"])

    _save(fig, "05-ptt-gpio12.png")


def diagram_breadboard():
    fig, ax = _setup("Breadboard — RX audio node (one row)", w=12, h=6)
    ax.set_xlim(0, 12)
    ax.set_ylim(0, 6)

    rail_top, rail_bot = 5.2, 0.8
    ax.plot([1, 11], [rail_top, rail_top], color="#dc2626", lw=4, solid_capstyle="round")
    ax.plot([1, 11], [rail_bot, rail_bot], color="#2563eb", lw=4, solid_capstyle="round")
    _label(ax, 0.45, rail_top, "+", size=14, color="#dc2626", bold=True)
    _label(ax, 0.45, rail_bot, "−", size=14, color="#2563eb", bold=True)
    _label(ax, 11.35, rail_top, "3.3 V", size=9, ha="left", color="#dc2626")
    _label(ax, 11.35, rail_bot, "GND", size=9, ha="left", color="#2563eb")

    row_x, row_y = 6.0, 3.0
    for dx in (-1.0, -0.5, 0, 0.5, 1.0):
        ax.add_patch(Circle((row_x + dx, row_y), 0.11, fc="#cbd5e1", ec="#64748b", lw=0.8))

    _label(ax, row_x, 3.55, "same breadboard row", size=9, color=COL["muted"])

    _vline(ax, row_x, rail_top, row_y + 0.11)
    _resistor(ax, row_x, 4.1, "47 kΩ", vertical=True)
    _vline(ax, row_x, 3.65, row_y + 0.11)

    _vline(ax, row_x, row_y - 0.11, rail_bot)
    _resistor(ax, row_x, 1.9, "47 kΩ", vertical=True)

    _cap(ax, 3.8, row_y, "10 µF")
    _hline(ax, 2.0, 3.45, row_y)
    _label(ax, 1.5, row_y + 0.35, "SPK+", size=9, ha="center", color=COL["radio_edge"], bold=True)

    _hline(ax, row_x + 0.55, 9.0, row_y)
    _label(ax, 9.45, row_y + 0.35, "GPIO 34", size=9, ha="left", color=COL["esp_edge"], bold=True)

    steps = [
        (1.0, 5.75, "1. 3.3 V rail → 47 kΩ → row"),
        (1.0, 5.35, "2. Row → 47 kΩ → GND rail"),
        (1.0, 4.95, "3. Row → 10 µF → radio SPK+"),
        (1.0, 4.55, "4. Row → jumper → GPIO 34"),
        (1.0, 4.15, "5. Radio sleeve + ESP32 GND → GND rail"),
    ]
    for x, y, txt in steps:
        _label(ax, x, y, txt, size=9, ha="left")

    _save(fig, "06-breadboard-rx-node.png")


def main():
    diagram_radio_jack()
    diagram_overview()
    diagram_rx()
    diagram_tx()
    diagram_ptt()
    diagram_breadboard()
    print(f"Done. Diagrams in {OUT_DIR}")


if __name__ == "__main__":
    main()
