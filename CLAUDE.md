# Jakesoft Checklists (Palm OS)

Reusable checklists for Palm OS. Make a **template** (e.g. a trip packing list), then start a **list** from it and check items off as you pack. Templates and in-progress lists can be **beamed** (IR) between Palm devices.

Main use case: packing lists before a trip, replacing a sheet of notebook paper the owner has reused a dozen times.

## Targets

- Must run on a **Handspring Visor, Palm OS 3.0** (monochrome, 160x160).
- Mostly used on **Palm OS 5.0** devices (color, still 160x160 low-res apps are fine).
- Palm OS 3.0 is the floor: no 3.5-only APIs, no color-only UI. SDK 3.5 headers are needed for the Exchange Manager, but the code checks the ROM version before using it.

## Build environment

- Builds happen with **CodeWarrior for Palm OS 5.2.1283 running directly on Windows 11**. Resources are compiled with **PilRC**.
- Building on macOS (ARM Mac, `savaughn/palmdev-macos` under `/usr/local`) was tried for an earlier project and didn't work out, so don't plan around it. An earlier project used a Windows 2000 VM; that is no longer the setup.
- The code as it stands builds and **runs on both a Palm TX (Palm OS 5.4) and a Handspring Visor Deluxe (Palm OS 3.0)**, so the project files, PilRC resources and SDK calls are known to work with this toolchain. Claude's sandbox has no Palm toolchain, so changes can only be syntax-checked there (`gcc -fsyntax-only` against a stub `PalmOS.h`); real builds and testing are done by the owner.
- Creator ID **`JsCL`**, database type `Data`, database name `JakesoftChecklists`, app name "Checklists". (The earlier daily-checklist app used `JsDL`.)

## Files

Folder layout: `Src\`, `Rsc\`, `Tools\`, plus `Checklists.mcp` (CodeWarrior project) and its `Checklists_Data\`, `Debug\` output at the root.

| File | Purpose |
| --- | --- |
| `Src\checklist.c` | Whole app: forms, event handlers, database, beaming. C89. |
| `Src\checklist_res.h` | Resource IDs shared by the .c and .rcp. |
| `Rsc\checklist.rcp` | PilRC resources: forms, menus, alerts, icon and logo bitmaps. |
| `Rsc\icon_*bpp.bmp`, `Rsc\small_*bpp.bmp` | Launcher icon 32x22 and small icon 15x9, at 1/2/4/8 bpp. |
| `Rsc\logo_*bpp.bmp` | Jakesoft logo, 120x45, at 1/2/4/8 bpp. |
| `Tools\make_assets.py` | Regenerates every bitmap (needs Python, Pillow, `rsvg-convert`). |
| `Tools\jakesoft-logo.svg` | The owner's Jakesoft logo, source for the About-box bitmaps. |
| `Tools\icon.svg` | 128x128 icon in the Rabbit r1 style (not used by the Palm build). |
| `Tools\preview_*.png`, `Tools\logo_big.png` | Enlarged previews and the intermediate 960x360 logo render. |
| `readme.txt` | Short user-facing setup and usage notes. |

Keep the `.bmp` files next to `checklist.rcp` (in `Rsc`) when building. `Rsc\logo_big.png` is a stray copy of the one in `Tools`; the build doesn't use it.

## How the app is structured

Forms (all 160x160 or modal dialogs): `frmMain` (Lists / Templates tabs), `frmChecklist` (tap a row to check it), `frmEdit` (Add / Edit / Del / Up / Dn), `frmText` (text entry, with a "More" button for adding several items in a row), `frmPick` (template picker), `frmAbout`.

Lists are custom-drawn (`LstSetDrawFunction`) with separate scroll bars, so there are no per-row string arrays. The ID-to-form mapping lives in `checklist_res.h`.

Limits: 100 items per list, 39 characters per item or name, 200 records.

### Data format

One database, one record per template or list. Record layout (also exactly what is beamed):

```
UInt8  kind        0 = list in progress, 1 = template
UInt8  count       number of items
char   name[]      NUL-terminated
count x { UInt8 done; char text[]; }    text NUL-terminated
```

`ValidRecord()` checks every record before it is parsed, whether it came from the database or from a beam. Anything that changes the format needs to keep that in mind, and beamed records from older versions should keep working or be rejected cleanly.

A sample "Trip packing (sample)" template is created the first time the database is created.

### Beaming

- Menu > Beam sends the open list or template with `ExgPut`/`ExgSend` (extension `.jcl`, target creator `JsCL`).
- Receiving is handled in `PilotMain` on `sysAppLaunchCmdExgReceiveData`. It must work when the app is **not** running (no globals), so `ReceiveBeam()` uses none and opens the database itself. If the database doesn't exist yet (app never run) the beam is refused, which is why the README says to run the app once on each device.
- Registration happens in `RegisterBeaming()` at launch, sync-notify and reset.
- Least tested part of the app: confirm on real hardware or an emulator with IR/beam support.

## Assets and style

- **App icon:** a big white checkbox with a bold orange tick on a Rabbit-orange (`#FE5000`) rounded tile, matching the owner's Rabbit r1 creation icons (128x128 SVGs in orange rounded squares). The first design (checklist rows) was replaced because it looked too much like an icon from an earlier project. On 1-bit screens the tile is solid black with a white box.
- **Jakesoft logo:** the owner's own `jakesoft-logo.svg` (white hand-drawn lettering with a blue outline on dark navy `#12234d`), rendered to 120x45 bitmaps for the About dialog. The owner said these bitmaps "worked out perfectly" and wants them **reused in future Palm projects**, so keep the generation approach in `make_assets.py`.
- 2-bit images are written as 4-bit BMPs using gray levels 0, 5, 10, 15 (BMP has no 2-bit format). Palette index 0 is white.
- Regenerate with `python Tools\make_assets.py` after changing icon art or the logo. It reads from `Tools` and writes the `.bmp` files into `Rsc` regardless of the current directory.

## Status and things still worth checking

- Confirmed by the owner: the app runs on a Palm TX (OS 5.4) and a Visor Deluxe (OS 3.0).
- Not yet reported as tested: beam send/receive between devices (especially the "app not running" receive path), and the About-box layout with the Jakesoft logo and the new checkbox icons on the monochrome Visor.
- If PilRC ever rejects the icon or logo lines, the README lists fallbacks (single `ICON` / `BITMAP` lines, dropping the 8-bit files).
- When changing code, keep it Palm OS 3.0 safe: check the ROM version before using anything newer, and avoid color-only UI.

## Working with the owner

- Palm OS app ideas come in a sequence of small apps for the Visor and newer Palms; they like finished, buildable project folders they can drop into CodeWarrior on Windows 11 and test on their own devices.
- Branding for their creations is "Jakesoft".
