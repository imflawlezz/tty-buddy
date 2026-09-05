# hardware

Physical device files for tty-buddy.

| Path | Contents |
|------|----------|
| [`wiring.md`](wiring.md) | Quick pin table |
| [`cad/`](cad/) | Enclosure CAD |
| [`schematics/`](schematics/) | KiCad schematic |

## CAD

```text
cad/
├── tty-buddy-enclosure.f3d
└── export/          # STEP / STL / 3MF
```

## Schematics

Open the KiCad project:

`schematics/tty-buddy/tty-buddy.kicad_pro`

```text
schematics/tty-buddy/
├── tty-buddy.kicad_pro
├── tty-buddy.kicad_sch
├── sym-lib-table
├── libraries/
│   └── tty-buddy.kicad_sym
└── export/
    └── tty-buddy-schematic.pdf
```
