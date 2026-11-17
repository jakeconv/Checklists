# Checklists for Palm OS

Reusable checklists for Palm OS. Make a **template** (for example a trip packing list), then start a **list** from it and check items off as you pack. Templates and lists in progress can be beamed over IR to other Palm devices.

Runs on Palm OS 3.0 and newer, including the monochrome Handspring Visor and color Palm devices (tested on a Visor Deluxe and a Palm TX).

## Using it

- **Templates tab:** tap *New...* to make a template, then add items. Tap a template to edit it.
- **Lists tab:** tap *New...* to start a list from a template. Tap an item to check it off. *Add* and *Edit* change the list without touching the template.
- **Menu > Copy...:** duplicates the open template or list under a new name (a copied list starts unchecked).
- **Menu > Beam:** sends the open template or list to another Palm.
- Limits: 100 items per list, 39 characters per item or name, 200 lists and templates in total.

A sample "Trip packing (sample)" template is created the first time the app runs.

## Installing

Install `Checklists.prc` with HotSync (or your usual Palm install tool) and run the app once. To receive beams, run the app once on each device first; that creates its database and registers the `.jcl` file type.

## Building

The app is built with CodeWarrior for Palm OS 5.2 on Windows, with resources compiled by PilRC. Open `Checklists.mcp`.

The application's **creator ID must be `JsCL`** (exactly, case-sensitive) in every build target's Palm OS Target settings. Beaming looks the app up by creator ID, so a different value makes beaming fail with "can't find application" errors even though the app itself runs fine.

| Folder | Contents |
| --- | --- |
| `Src/` | `checklist.c` (the whole app, C89) and `checklist_res.h` (resource IDs) |
| `Rsc/` | `checklist.rcp` (PilRC forms, menus, alerts) and the icon and logo bitmaps |
| `Tools/` | `make_assets.py` and the SVG sources that regenerate the bitmaps |

To regenerate the bitmaps after changing the icon or logo art, install Pillow and run `python Tools\make_assets.py`. It needs `rsvg-convert` only if you change `jakesoft-logo.svg`; otherwise it reuses `Tools\logo_big.png`.

## Data format

One database (type `Data`, creator `JsCL`, name `JakesoftChecklists`) with one record per template or list. This is also exactly what is beamed:

```
UInt8  kind        0 = list in progress, 1 = template
UInt8  count       number of items
char   name[]      NUL-terminated
count x { UInt8 done; char text[]; }    text NUL-terminated
```

## Made with generative AI

This project was made with the help of generative AI. The code, resources and bitmap-generation scripts were written with Claude (Anthropic) working from my direction, and I build and test the result on real Palm hardware. The Jakesoft logo is my own artwork. Treat the code accordingly: it works on my devices, but review it before relying on it.

## License

MIT. See [LICENSE](LICENSE).
