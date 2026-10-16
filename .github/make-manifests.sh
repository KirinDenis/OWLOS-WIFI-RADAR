#!/usr/bin/env bash
# writes an ESP Web Tools manifest.json next to every screen's binaries
#   make-manifests.sh <fw folder> "<version text>"
# <fw folder>/<screen>/ holds bootloader.bin, partition-table.bin, owlos-wifi-radar.bin and
# the build's flash_args, which says at what address each one goes
set -euo pipefail
FW="$1"
VERSION="$2"

for dir in "$FW"/*/; do
  disp=$(basename "$dir")
  case "$disp" in
    ili9341) name='2.8" CYD (320x240, ILI9341 or ST7789)';        family='ESP32' ;;
    st7796)  name='3.5" CYD (ST7796 480x320)';                   family='ESP32' ;;
    ili9486) name='3.5" RPi LCD on OWLOSAirQuality (ILI9486)';    family='ESP32' ;;
    gc9a01)  name='1.28" round (GC9A01 240x240)';                 family='ESP32-C3' ;;
    epd400)  name='4.2" e-paper (SSD1683 400x300)';               family='ESP32-S3' ;;
    epd792)  name='5.79" e-paper (2x SSD1683 792x272)';           family='ESP32-S3' ;;
    *) echo "skipping unknown folder $disp"; continue ;;
  esac
  parts=""
  while read -r addr path; do
    case "$addr" in ""|--*) continue ;; esac          # the first line is the flash settings
    file=$(basename "$path")
    [ -f "$dir/$file" ] || { echo "$disp: $file is missing" >&2; exit 1; }
    parts="$parts${parts:+, }{ \"path\": \"$file\", \"offset\": $((addr)) }"
  done < "$dir/flash_args"
  cat > "$dir/manifest.json" <<EOF
{
  "name": "OWLOS WiFi Radar - ${name//\"/\\\"}",
  "version": "$VERSION",
  "new_install_prompt_erase": true,
  "builds": [ { "chipFamily": "$family", "parts": [ $parts ] } ]
}
EOF
  echo "$disp: $(tr -d '\n' < "$dir/manifest.json")"
done
