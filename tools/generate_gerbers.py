#!/usr/bin/env python3
"""
GhostBlade Gerber Generation Script
==================================
Generates manufacturing-ready Gerber files, drill files, and pick-and-place
from KiCad PCB for submission to PCB fabricators.

Usage:
    python3 generate_gerbers.py [--kicad_pcb path/to/ghostblade.kicad_pcb] [--output gerbers/]

Requirements:
    KiCad 8+ must be installed with kicad-cli available in PATH.

Output:
    gerbers/
    ├── ghostblade-F_Cu.gbr          (Top copper - Signal + RF)
    ├── ghostblade-In1_Cu.gbr        (Inner copper 1 - Ground Plane)
    ├── ghostblade-In2_Cu.gbr        (Inner copper 2 - Signal/Low-Speed)
    ├── ghostblade-In3_Cu.gbr        (Inner copper 3 - Power Plane)
    ├── ghostblade-In4_Cu.gbr        (Inner copper 4 - Ground Plane 2)
    ├── ghostblade-B_Cu.gbr          (Bottom copper - Power distribution)
    ├── ghostblade-F_Paste.gbr       (Top solder paste)
    ├── ghostblade-B_Paste.gbr       (Bottom solder paste)
    ├── ghostblade-F_Mask.gbr        (Top solder mask)
    ├── ghostblade-B_Mask.gbr        (Bottom solder mask)
    ├── ghostblade-F_SilkS.gbr       (Top silkscreen)
    ├── ghostblade-B_SilkS.gbr       (Bottom silkscreen)
    ├── ghostblade-Edge_Cuts.gbr     (Board outline)
    ├── ghostblade-Vias.gbr          (Via holes - thermal + signal)
    ├── ghostblade.drl               (Excellon drill file)
    ├── ghostblade-NPTH.drl          (Non-plated through-hole drill)
    └── ghostblade-PickPlace.pos     (Pick-and-place file)

Manufacturing Notes:
    - 6-layer PCB, 1.6mm FR-4 (Isola 370HR or equivalent High-TG)
    - IPC Class 3
    - ENIG surface finish (Electroless Nickel Immersion Gold)
    - 1 oz copper external, 0.5 oz internal
    - Solder mask: Green (or Black per aesthetic preference)
    - Silkscreen: White
    - Minimum trace/space: 0.1mm / 0.1mm (4mil/4mil)
    - Minimum via: 0.2mm hole / 0.4mm pad (for BGA escape)
    - Impedance control: 50 ohm SE, 100 ohm diff on Layer 1
    - RF section: controlled impedance, no vias in RF path
    - Thermal vias under RK3576: 0.3mm hole, 0.6mm pad, 1.0mm pitch grid
    - Board edge: 2mm keepout from antenna connectors
    - Gold plating on antenna connector pads (hard gold, 30 microinches)
    - Via-in-pad allowed for BGA components (copper-filled, capped)

SPDX-License-Identifier: GPL-2.0-or-later
"""

import subprocess
import os
import sys
import argparse
import json
from pathlib import Path
from datetime import datetime


# Layer definitions for 6-layer stackup
GERBER_LAYERS = {
    "F.Cu":       "ghostblade-F_Cu.gbr",
    "In1.Cu":     "ghostblade-In1_Cu.gbr",
    "In2.Cu":     "ghostblade-In2_Cu.gbr",
    "In3.Cu":     "ghostblade-In3_Cu.gbr",
    "In4.Cu":     "ghostblade-In4_Cu.gbr",
    "B.Cu":       "ghostblade-B_Cu.gbr",
    "F.Paste":    "ghostblade-F_Paste.gbr",
    "B.Paste":    "ghostblade-B_Paste.gbr",
    "F.Mask":     "ghostblade-F_Mask.gbr",
    "B.Mask":     "ghostblade-B_Mask.gbr",
    "F.SilkS":    "ghostblade-F_SilkS.gbr",
    "B.SilkS":    "ghostblade-B_SilkS.gbr",
    "Edge.Cuts":  "ghostblade-Edge_Cuts.gbr",
    "In1.Cu":     "ghostblade-Vias.gbr",  # Via layer
}


def generate_gerbers(kicad_pcb: str, output_dir: str) -> bool:
    """Generate Gerber files using KiCad CLI."""
    pcb_path = Path(kicad_pcb)
    out_path = Path(output_dir)

    if not pcb_path.exists():
        print(f"ERROR: KiCad PCB file not found: {pcb_path}")
        return False

    out_path.mkdir(parents=True, exist_ok=True)

    print(f"Generating Gerbers from: {pcb_path}")
    print(f"Output directory: {out_path}")

    # Generate each Gerber layer
    for layer_id, filename in GERBER_LAYERS.items():
        cmd = [
            "kicad-cli", "pcb", "export", "gerbers",
            "--layers", layer_id,
            "--output", str(out_path),
            str(pcb_path)
        ]
        print(f"  Generating {filename} ({layer_id})...")
        try:
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
            if result.returncode != 0:
                print(f"  WARNING: {layer_id} generation returned {result.returncode}")
                if result.stderr:
                    print(f"    stderr: {result.stderr[:200]}")
        except FileNotFoundError:
            print("ERROR: kicad-cli not found. Install KiCad 8+ and ensure kicad-cli is in PATH.")
            return False
        except subprocess.TimeoutExpired:
            print(f"  WARNING: {layer_id} generation timed out")

    # Generate drill files
    print("  Generating drill files...")
    drill_cmd = [
        "kicad-cli", "pcb", "export", "drill",
        "--format", "excellon",
        "--output", str(out_path),
        str(pcb_path)
    ]
    try:
        result = subprocess.run(drill_cmd, capture_output=True, text=True, timeout=60)
        if result.returncode != 0:
            print(f"  WARNING: Drill generation returned {result.returncode}")
    except (FileNotFoundError, subprocess.TimeoutExpired) as e:
        print(f"  WARNING: Drill generation failed: {e}")

    # Generate pick-and-place file
    print("  Generating pick-and-place file...")
    pos_cmd = [
        "kicad-cli", "pcb", "export", "pos",
        "--format", "csv",
        "--output", str(out_path / "ghostblade-PickPlace.pos"),
        str(pcb_path)
    ]
    try:
        subprocess.run(pos_cmd, capture_output=True, text=True, timeout=60)
    except (FileNotFoundError, subprocess.TimeoutExpired):
        pass

    print(f"\nGerber generation complete. Files in: {out_path}")
    return True


def generate_fab_note(output_dir: str):
    """Generate fabrication notes as a JSON file for the fab house."""
    fab_data = {
        "project": "GhostBlade - Project NullSpectre",
        "revision": "1.0",
        "date": datetime.now().isoformat(),
        "board_specifications": {
            "layers": 6,
            "thickness_mm": 1.6,
            "material": "FR-4 High-TG (Isola 370HR or equivalent)",
            "copper_weight_external": "1 oz (35 um)",
            "copper_weight_internal": "0.5 oz (17.5 um)",
            "surface_finish": "ENIG (Electroless Nickel / Immersion Gold)",
            "solder_mask_color": "Green (or Black)",
            "silkscreen_color": "White",
            "ipc_class": 3,
            "minimum_trace_width_mm": 0.1,
            "minimum_spacing_mm": 0.1,
            "minimum_via_hole_mm": 0.2,
            "minimum_via_pad_mm": 0.4,
            "impedance_control": [
                {"layer": "F.Cu", "type": "single-ended", "target_ohm": 50, "tolerance_pct": 10},
                {"layer": "F.Cu", "type": "differential", "target_ohm": 100, "tolerance_pct": 10},
            ],
            "via_in_pad": True,
            "via_in_pad_fill": "copper-filled and capped",
            "thermal_via_hole_mm": 0.3,
            "thermal_via_pad_mm": 0.6,
            "thermal_via_pitch_mm": 1.0,
            "gold_plating_antenna_pads": "Hard gold, 30 microinches minimum",
            "edge_connector_bevel": None,
            "controlled_depth_routing": None,
        },
        "stackup": [
            {"layer": 1, "name": "F.Cu", "type": "Signal (High-Speed RF)", "thickness_oz": 1.0, "impedance_notes": "50 ohm SE, 100 ohm diff"},
            {"layer": 2, "name": "In1.Cu", "type": "Ground Plane (Solid)", "thickness_oz": 0.5, "notes": "Continuous under RF section"},
            {"layer": 3, "name": "In2.Cu", "type": "Signal (Low-Speed Buses)", "thickness_oz": 0.5, "notes": "I2C, UART, SPI"},
            {"layer": 4, "name": "In3.Cu", "type": "Power Plane (Segmented)", "thickness_oz": 0.5, "notes": "1.1V SoC, 1.8V VIO, 3.3V Peripherals"},
            {"layer": 5, "name": "In4.Cu", "type": "Ground Plane 2", "thickness_oz": 0.5, "notes": "Return path for bottom signals"},
            {"layer": 6, "name": "B.Cu", "type": "Signal (Power distribution)", "thickness_oz": 1.0, "notes": "Power routing, component escape"},
        ],
        "special_requirements": [
            "RF shielding can solder pads on F.Cu around LMS7002M and CC1101 zones",
            "Impedance-controlled traces on F.Cu (50 ohm microstrip, 100 ohm differential)",
            "No vias within RF trace paths between antenna connectors and ICs",
            "Thermal via matrix (31x31 = 961 vias) directly beneath RK3576 exposed pad",
            "Copper-filled and capped vias-in-pad for all BGA components (RK3576, LPDDR5, eMMC)",
            "2mm keepout zone from board edge to nearest antenna connector pad",
            "Fiducial markers: 3x on top, 3x on bottom (1.5mm from board edge)",
            "Panelization: V-scored, 5mm rails, with breakaway tabs",
        ],
    }

    fab_path = Path(output_dir) / "ghostblade-fab-notes.json"
    fab_path.parent.mkdir(parents=True, exist_ok=True)
    with open(fab_path, 'w') as f:
        json.dump(fab_data, f, indent=2)
    print(f"Fabrication notes written to: {fab_path}")


def generate_zip(output_dir: str):
    """Create a ZIP archive of all Gerber files for fab house submission."""
    import zipfile

    zip_path = Path(output_dir) / "ghostblade-gerbers.zip"
    gerber_dir = Path(output_dir)

    with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED) as zf:
        for ext in ['*.gbr', '*.gbo', '*.gbs', '*.gbl', '*.gbr', '*.drl', '*.pos', '*.json']:
            for f in gerber_dir.glob(ext):
                zf.write(f, f.name)

    print(f"Gerber archive: {zip_path}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="GhostBlade Gerber Generation")
    parser.add_argument("--kicad-pcb", default="hardware/kicad/ghostblade.kicad_pcb",
                        help="Path to KiCad PCB file")
    parser.add_argument("--output", default="hardware/gerbers",
                        help="Output directory for Gerber files")
    parser.add_argument("--zip", action="store_true",
                        help="Create ZIP archive for fab house submission")
    parser.add_argument("--fab-note", action="store_true",
                        help="Generate fabrication notes JSON")
    args = parser.parse_args()

    success = True

    if args.fab_note or not args.zip:
        generate_fab_note(args.output)
        success = True

    if args.kicad_pcb and Path(args.kicad_pcb).exists():
        success = generate_gerbers(args.kicad_pcb, args.output)

    if args.zip:
        generate_zip(args.output)

    sys.exit(0 if success else 1)