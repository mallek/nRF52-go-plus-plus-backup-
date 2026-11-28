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
)
fail=0
for dir in "${@:-anim settings menu}"; do
  for d in $dir; do
    entry="${SRC[$d]:-}"
    if [[ -z "$entry" ]]; then echo "unknown test folder: $d"; fail=1; continue; fi
    test="${entry%%:*}"; sources="${entry#*:}"
    ( cd "$d" && g++ -std=c++17 -Wall -Wextra -Werror -I"$SK" "$test" $sources -o /tmp/pp_test_$d && /tmp/pp_test_$d ) || fail=1
  done
done
exit $fail
