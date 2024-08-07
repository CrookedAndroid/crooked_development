#!/bin/bash -e

usage() {
  cat <<EOF
Usage:
  ${0}                  Remerge all files with conflict markers in the git working tree
  ${0} [FILE...]        Remerge the given files

Options:
  -t, --tool {bcompare,meld,vimdiff}
    Use the specified merge tool.
EOF
}

while [[ "$1" =~ ^- ]]; do
  arg="${1}"
  shift
  case "$arg" in
    -t | --tool)
      MERGETOOL="${1}"
      shift
      ;;
    --)
      break
      ;;
    -h | --help | --usage)
      usage
      exit 0
      ;;
    *)
      usage
      exit 1
      ;;
  esac
done

BIN_DIR="$(dirname "${BASH_SOURCE[0]}")"
SPLIT3="${BIN_DIR}/split3.awk"
MERGETOOL="${MERGETOOL:-bcompare}"
TEMP_FILES=()

cleanup() {
  set +e
  rm -rf "${TEMP_FILES[@]}"
}
trap cleanup EXIT

mergetool() {
  local file="${1}"
  local MERGED="$file"
  local BASE="${file}:BASE"
  local LOCAL="${file}:LOCAL"
  local REMOTE="${file}:REMOTE"
  TEMP_FILES+=("$BASE" "$LOCAL" "$REMOTE")

  local has_base=false
  if grep -qE '^[|]{7}( .+)?$' "$file"; then
    local has_base=true
  fi

  "$has_base" && awk -f "$SPLIT3" -v TARGET=BASE <"$file" >"$BASE"
  awk -f "$SPLIT3" -v TARGET=LOCAL <"$file" >"$LOCAL"
  awk -f "$SPLIT3" -v TARGET=REMOTE <"$file" >"$REMOTE"

  case "$MERGETOOL" in
    bcompare)
      if "$has_base"; then
        bcompare "$LOCAL" "$REMOTE" "$BASE" -mergeoutput="$MERGED"
      else
        bcompare "$LOCAL" "$REMOTE" -mergeoutput="$MERGED"
      fi
      ;;
    meld)
      if "$has_base"; then
        meld "$LOCAL" "$BASE" "$REMOTE" -o "$MERGED"
      else
        meld "$LOCAL" "$MERGED" "$REMOTE"
      fi
      ;;
    vimdiff)
      if "$has_base"; then
        vimdiff -c '4wincmd w | wincmd J' "$LOCAL" "$BASE" "$REMOTE" "$MERGED"
      else
        vimdiff -c 'wincmd l' "$LOCAL" "$MERGED" "$REMOTE"
      fi
      ;;
  esac
}

#
# BEGIN
#

FILES=()
if [[ "${#}" -eq 0 ]]; then
  while IFS= read -r -d '' ARG; do
    FILES+=("$ARG")
  done < <(git grep -zlE '^<{7}( .+)?$')

  echo "Found files with conflict markers:"
  for file in "${FILES[@]}"; do
    echo "    ${file}"
  done
  echo
else
  FILES+=("${@}")
fi

for file in "${FILES[@]}"; do
  if ! [[ -f "$file" ]]; then
    echo "${file}: No such file"
    exit 1
  fi

  if ! grep -qE '^<{7}( .+)?$' "$file"; then
    echo "[SKIPPED] No conflict markers found in ${file}"
    continue
  fi

  echo "Merging ${file}"
  if mergetool "$file"; then
    exit_code=0
    if git rev-parse >/dev/null 2>/dev/null && [[ -n $(git ls-files "$file") ]]; then
      ( set -x ; git add "$file" )
    fi
  else
    exit_code="$?"
    echo "Failed to merge ${file}"
  fi
  if [[ "$exit_code" -ne 0 ]] || [[ "$MERGETOOL" = meld ]] ; then
    read -p "Continue merging other files [y/n]?" yn
    ! [[ "$yn" =~ ^[Yy]$ ]] && exit "$exit_code"
  fi
done

