#!/bin/bash

set -e
set -x

for EXT in patch diff ; do
  echo $EXT
  for CRATE in $(find external/rust/crates -name Android.bp.$EXT | grep -v tikv-jemalloc-sys | cut -d / -f 4) ; do
    pushd external/rust/crates/$CRATE
    cargo_embargo generate cargo_embargo.json
    if [ -f Android.bp.orig ] ; then
      repo start recontextualize-android-bp
      git diff -U10 --no-index Android.bp.orig Android.bp | sed 's/Android.bp.orig/Android.bp/' > patches/Android.bp.$EXT
      rm -f Android.bp.orig
    fi
    popd
  done
done
