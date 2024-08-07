#!/bin/bash -e

BIN_DIR="$(dirname "${BASH_SOURCE[0]}")"
SPLIT3="${BIN_DIR}/split3.awk"

TEMP_FILES=()

LOG() {
  echo >&2 "${@}"
}

cleanup() {
  EXIT_CODE="$?"
  rm -rf "${TEMP_FILES[@]}" || true
  exit "$EXIT_CODE"
}

trap cleanup EXIT

if [[ "${#}" -eq 0 ]]; then
  FILES=($(git grep -l -e '^<<<<<<<' || true))
  LOG "Files with unresolved merge markers:"
  for file in "${FILES[@]}"; do
    LOG "    ${file}"
  done
else
  FILES=("${@}")
fi

for file in "${FILES[@]}"; do
  TEMP_FILES+=("${file}:1" "${file}:2" "${file}:3")
  awk -f "$SPLIT3" -v TARGET=BASE <"$file" >"${file}:1"
  awk -f "$SPLIT3" -v TARGET=HEAD <"$file" >"${file}:2"
  awk -f "$SPLIT3" -v TARGET=OTHER <"$file" >"${file}:3"

  if cmp -s "${file}:1" "${file}:2" && cmp -s "${file}:2" "${file}:3"; then
    LOG "Skipped ${file} as there are no conflicts detected."
    continue
  fi

  if bcompare "${file}:2" "${file}:3" "${file}:1" "$file"; then
    ( set -x && git add "$file" )
  else
    LOG "Failed to merge ${file}"
  fi
done

