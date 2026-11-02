#!/bin/bash
# Downloads the matrices listed in LIST (Group/Name per line) into DIR and keeps Name.mtx.
#   LIST=list.txt DIR=matrices ./get_suitesparse.sh
# JOBS parallel downloads (default 4).
set -e
LIST="${LIST:-list.txt}"
DIR="${DIR:-matrices}"
JOBS="${JOBS:-4}"
mkdir -p "$DIR"
fetch() {
  gm="$1"; name="${gm#*/}"; dir="$2"
  [ -s "$dir/$name.mtx" ] && { echo "$name: already there"; return 0; }
  for base in https://sparse.tamu.edu/MM https://suitesparse-collection-website.herokuapp.com/MM; do
    if curl -sSfL "$base/$gm.tar.gz" -o "$dir/$name.tar.gz"; then
      tar -xzf "$dir/$name.tar.gz" -C "$dir" "$name/$name.mtx" && mv "$dir/$name/$name.mtx" "$dir/$name.mtx"
      rm -rf "$dir/$name" "$dir/$name.tar.gz"
      echo "$name: ok"; return 0
    fi
  done
  echo "$name: FAILED"; rm -f "$dir/$name.tar.gz"
}
export -f fetch
grep -v '^\s*$' "$LIST" | xargs -P "$JOBS" -I{} bash -c 'fetch "$@"' _ {} "$DIR"
