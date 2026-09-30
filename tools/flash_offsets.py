# -----------------------------------------------------------------------------
#  Puts boot_app0.bin where OUR partition table says otadata is.
#
#  The build adds that image to FLASH_EXTRA_IMAGES at a hardcoded 0xE000 - the
#  offset otadata has in every stock table. Ours moved it to make room for a
#  bigger NVS, and 0xE000 is now inside NVS: left alone, every upload writes an
#  OTA selector into the middle of the settings partition and erases whatever
#  was there.
#
#  board_upload.ota_partition_offset does not help. The board config accepts it
#  - it reads back correctly - but the code that assembles the image list never
#  consults it on an Arduino-only build.
#
#  So the list is corrected here, from the table itself rather than from another
#  constant that could drift away from it in the same way.
# -----------------------------------------------------------------------------
Import("env")

import os
import re


def otadata_offset(csv_path):
    """The offset of the `data, ota` row, or None."""
    if not csv_path or not os.path.isfile(csv_path):
        return None
    with open(csv_path) as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            cols = [c.strip() for c in line.split(",")]
            if len(cols) >= 4 and cols[1] == "data" and cols[2] == "ota":
                return int(cols[3], 0)
    return None


want = otadata_offset(env.subst("$PARTITIONS_TABLE_CSV"))
if want is not None:
    fixed = []
    moved = False
    for offset, path in env.get("FLASH_EXTRA_IMAGES", []):
        if os.path.basename(str(path)) == "boot_app0.bin" and int(str(offset), 0) != want:
            print("flash_offsets: boot_app0.bin %s -> %s (otadata, from the table)"
                  % (offset, hex(want)))
            offset = hex(want)
            moved = True
        fixed.append((offset, path))
    if moved:
        env.Replace(FLASH_EXTRA_IMAGES=fixed)
