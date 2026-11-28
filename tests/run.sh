#!/usr/bin/env bash
# Builds and runs one host test folder, or all of them. From WSL:
#   bash tests/run.sh            all
#   bash tests/run.sh menu       one folder
set -uo pipefail
cd "$(dirname "$0")"
SK=../../code/nrf_emulator/pgpemu_xiao
declare -A SRC=(
  [anim]="anim_logic_test.cpp:$SK/pp_anim_logic.cpp $SK/pp_feedback_logic.cpp"
  [settings]="settings_logic_test.cpp:$SK/pp_settings_logic.cpp"
  [menu]="menu_logic_test.cpp:$SK/pp_menu_logic.cpp"
  [feedback]="feedback_logic_test.cpp:$SK/pp_feedback_logic.cpp"
  [logstore]="log_store_test.cpp:$SK/pp_feedback_log_store.cpp"
  [sleep]="sleep_night_test.cpp:$SK/pp_sleep_night.cpp"
  [gachispecies]="gachi_species_test.cpp:$SK/pp_gachi_species.cpp"
  [gachilogic]="gachi_logic_test.cpp:$SK/pp_gachi_logic.cpp $SK/pp_gachi_species.cpp"
  [gachibattle]="gachi_battle_test.cpp:$SK/pp_gachi_battle.cpp"
  [gachisave]="gachi_save_test.cpp:$SK/pp_gachi_save.cpp $SK/pp_gachi_logic.cpp $SK/pp_gachi_species.cpp"
  [gachiflow]="gachi_flow_test.cpp:$SK/pp_gachi_flow.cpp $SK/pp_gachi_battle.cpp"
  [gachitext]="gachi_text_test.cpp:$SK/pp_gachi_text.cpp"
  [gachistage]="gachi_stage_test.cpp:$SK/pp_gachi_stage.cpp"
)
fail=0
for dir in "${@:-anim settings menu feedback logstore sleep gachispecies gachilogic gachibattle gachisave gachiflow gachitext gachistage}"; do
  for d in $dir; do
    entry="${SRC[$d]:-}"
    if [[ -z "$entry" ]]; then echo "unknown test folder: $d"; fail=1; continue; fi
    test="${entry%%:*}"; sources="${entry#*:}"
    folder="$d"; if [[ "$d" == "logstore" ]]; then folder="feedback"; fi
    if [[ "$d" == "gachispecies" || "$d" == "gachilogic" || "$d" == "gachibattle" || "$d" == "gachisave" || "$d" == "gachiflow" || "$d" == "gachitext" || "$d" == "gachistage" ]]; then folder="gachi"; fi
    ( cd "$folder" && g++ -std=c++17 -Wall -Wextra -Werror -I"$SK" "$test" $sources -o /tmp/pp_test_$d && /tmp/pp_test_$d ) || fail=1
  done
done
exit $fail
