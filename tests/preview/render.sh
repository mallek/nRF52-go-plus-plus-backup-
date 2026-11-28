#!/usr/bin/env bash
# Renders the real PokeGachi drawing code on the PC and writes one PPM per scene.
#
#   bash tests/preview/render.sh <Adafruit_GFX library dir> <out dir>
#
# Run this part in WSL (g++ is there). WSL python has no Pillow, so the PNG step
# runs separately with a python that has Pillow (for example the Windows one):
#   python -I tests/preview/ppm_to_png.py <out dir>
#
# The sprites come from whatever pp_gachi_sprites.generated.h is in the sketch
# folder; with none, placeholders are drawn. Write the output outside tracked
# files (for example into build/preview) and never commit it.
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "usage: render.sh <Adafruit_GFX library dir> <out dir>" >&2
  exit 2
fi
GFX="$1"
OUT="$2"
HERE="$(cd "$(dirname "$0")" && pwd)"
SK="$HERE/../../code/nrf_emulator/pgpemu_xiao"
mkdir -p "$OUT"
OBJ="$OUT/obj"
mkdir -p "$OBJ"

DEFS="-DARDUINO=100 -DARDUINO_NRF52840_CLUE"
INC="-I$HERE/stub -I$GFX -I$SK"

# The library is compiled quietly; our own units keep the strict flags.
g++ -std=c++17 -w $DEFS $INC -c "$GFX/Adafruit_GFX.cpp" -o "$OBJ/Adafruit_GFX.o"

g++ -std=c++17 -Wall -Wextra -Werror $DEFS $INC \
  "$HERE/preview_main.cpp" \
  "$OBJ/Adafruit_GFX.o" \
  "$SK/pp_menu_draw.cpp" \
  "$SK/pp_menu_logic.cpp" \
  "$SK/pp_menu_items.cpp" \
  "$HERE/menu_stubs.cpp" \
  "$SK/pp_gachi_ui.cpp" \
  "$SK/pp_gachi_sprites.cpp" \
  "$SK/pp_gachi_text.cpp" \
  "$SK/pp_gachi_logic.cpp" \
  "$SK/pp_gachi_species.cpp" \
  "$SK/pp_gachi_battle.cpp" \
  -o "$OUT/preview"

"$OUT/preview" "$OUT"
echo "wrote PPM files to $OUT"
