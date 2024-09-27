#!/bin/sh

set -e
set -x

ANDROID_BUILD_TOP=$(realpath $(dirname $0)/../../..)
pushd $ANDROID_BUILD_TOP

#bash -c "source build/envsetup.sh && lunch aosp_cf_x86_64_phone-trunk_staging-userdebug && m cargo_embargo bpfmt"

PATH=$ANDROID_BUILD_TOP/out/host/linux-x86/bin:$PATH

#for CONFIG in $(find external/rust/crates -name cargo_embargo.json | grep -v quiche | grep -v grpcio) ; do

for CRATE in clang-sys csv csv-core libfuzzer-sys libsqlite3-sys matchit num-bigint quickcheck regex-automata rust-stemmers ryu same-file shared_child tikv-jemallocator tinyvec_macros unicode-ident vhost vhost-device-vsock virtio-bindings virtio-queue virtio-vsock vm-memory vulkano ; do
    CRATE_DIR=external/rust/crates/$CRATE
    if [ ! -f $CRATE_DIR/Android.bp ] ; then
        continue
    fi
    pushd $CRATE_DIR
    set +e
    ANDROID_BUILD_TOP=$ANDROID_BUILD_TOP $ANDROID_BUILD_TOP/out/host/linux-x86/bin/cargo_embargo generate cargo_embargo.json
    set -e
    if ! git diff --exit-code Android.bp ; then
        repo start cargo-embargo-all .
        git add Android.bp
        git commit -m "Update Android.bp by running cargo_embargo"$'\n'$'\n'"Test: ran cargo_embargo"
    fi
    rm -rf cargo.out cargo.metadata target.tmp rules.mk Cargo.lock Android.bp.orig
    git restore .
    popd
done

popd

# m rust
# repo upload -t --br=cargo-embargo-all --re=srhines@google.com
