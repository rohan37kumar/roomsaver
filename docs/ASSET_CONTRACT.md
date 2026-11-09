# Asset Contract: Blender → RoomSaver

This is what the engine expects from a custom room. Model freely, but keep the
**object names** and **export settings** below so the engine can bind album art, the
picture, the clock and the reactive lights to the right meshes.

The generated `assets/room_placeholder.glb` (from `tools/make_placeholder_room.py`)
follows this contract. Open it in Blender as a reference for placement and naming.

## Where to put the finished file

Export to **`assets/room.glb`**. The engine loads it automatically and only falls back
to the placeholder if it's missing. Re-export, re-run, see it. (The build copies
`assets/` next to the exe, so rebuild or copy the file into `build\Release\assets\`
too.)

## Coordinate system, scale, units

- **Z-up in Blender** (normal). Export with **+Y up** (glTF default); the engine is Y-up.
- **Meters.** Roughly a **4 m × 4 m × 2.6 m** room works well. The camera auto-fits
  the whole model, so exact size doesn't matter.
- Two walls meet at one back corner; the **open side faces the camera** (the camera
  is a fixed isometric view). In the placeholder the walls are at `x=0` (picture
  wall) and `y=4` (TV wall).
- **Apply transforms** (Object → Apply → All Transforms) before export.

## Export settings (Blender glTF exporter)

- Format: **glTF Binary (.glb)**
- **+Y Up**: on
- **Apply Modifiers**: on
- Include **Normals** and **UVs**
- Materials: **Export** (Principled BSDF). Base Color (factor or texture), Emission
  and Emission Strength are read.

## Object names (exact, case-sensitive)

### Dynamic surfaces: flat quads with clean 0..1 UVs, normal facing the room

The engine replaces these surfaces' materials at runtime. Give each a single
rectangular face UV-unwrapped to fill 0..1, as a **separate object**.

| Name | What renders on it |
|---|---|
| `TV_Screen` | current track's **album art** (16:9 works best), or the idle text |
| `Picture_Image` | the **framed picture**, cover-fit to the quad's aspect |
| `Clock_Display` | live **digital clock** (`HH:MM`) |

The picture's UVs get a **90° rotation** by default, because that's what a Blender
plane rotated onto a side wall produces. If your picture shows sideways or upside
down, run with `ROOMSAVER_PICROT=<deg>` (0/90/180/270) to find the right value.

### Reactive lights: emissive meshes

Give these an **Emission** material (any color; the engine overrides it). Each
should have its own material.

| Name | Behavior |
|---|---|
| `TV_BiasLight` | glow behind/around the TV: **album color + beat flicker** |
| `Soundbar_LED` | *(optional)* LED strip: same album color + beat flicker |

### Optional

| Name | Behavior |
|---|---|
| `Lamp_Wall` | wall lamp; its center becomes a warm **point light** that lights the room around it |

### Everything else: name it however you like

Sofa, tables, TV body, frame, plant, walls, floor… are ordinary static meshes.
Anything with an Emission material glows and blooms (e.g. lamps, LED decor).

## Names used by the bundled room

The bundled `assets/room.glb` predates this contract and uses different names. The
engine accepts both; the contract name wins if both exist:

| Contract name | Bundled room name |
|---|---|
| `TV_Screen` | `TV_SCREEN` |
| `Picture_Image` | `PICTURE_FRAME` |
| `Clock_Display` | `CLOCK_SCREEN` |
| `TV_BiasLight` | `Television.001` |
| `Soundbar_LED` | `SOUNDBAR_LED` |
| `Lamp_Wall` | `Plane.001` |

A few fix-ups apply **only** when the bundled names match, because they were tuned
for that model: its backlight plane is re-centred on the TV (`Television`) and
enlarged so it peeks out around the edges, and its very bright lamp emission is
toned down.

## Materials / look

- Low-poly, flat-ish colors are perfect. Lighting is a warm key light + ambient +
  the wall-lamp point light, with HDR bloom on emissive surfaces.
- Base-color textures are supported (embedded in the .glb or next to a .gltf).
- Keep polycount modest; this is a screensaver.

## Debugging

- `%TEMP%\RoomSaver.log` lists which node was bound to each role (or `MISSING`).
- `ROOMSAVER_DUMP=1` logs every emissive node with its world-space bounds.
- `RoomSaver.exe --selftest=shot.png` renders one frame to a PNG for a quick check.
