#!/usr/bin/env python
#
# Copyright (C) 2015 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

import adb
import logging
import os
import sys

# Shared functions across gdbclient.py and ndk-gdb.py.
import gdbrunner

def get_gdbserver_path(root, arch):
    path = "{}/prebuilts/misc/android-{}/gdbserver{}/gdbserver{}"
    if arch.endswith("64"):
        return path.format(root, arch, "64", "64")
    else:
        return path.format(root, arch, "", "")


def parse_args():
    parser = gdbrunner.ArgumentParser()

    group = parser.add_argument_group(title="attach target")
    group = group.add_mutually_exclusive_group(required=True)
    group.add_argument(
        "-p", dest="target_pid", metavar="PID", type=int,
        help="attach to a process with specified PID")
    group.add_argument(
        "-n", dest="target_name", metavar="NAME",
        help="attach to a process with specified name")
    group.add_argument(
        "-r", dest="run_cmd", metavar="CMD", nargs="+",
        help="run a binary on the device, with args")
    group.add_argument(
        "-u", dest="upload_cmd", metavar="CMD", nargs="+",
        help="upload and run a binary on the device, with args")

    parser.add_argument(
        "--port", nargs="?", default="5039",
        help="override the port used on the host")
    parser.add_argument(
        "--user", nargs="?", default="root",
        help="user to run commands as on the device [default: root]")

    return parser.parse_args()


def check_product_out_dir(product):
    root = os.environ["ANDROID_BUILD_TOP"]
    path = "{}/out/target/product/{}".format(root, product)
    if os.path.isdir(path):
        return path
    return None


def find_out_dir(props, root):
    if "ANDROID_BUILD_TOP" not in os.environ:
        sys.exit("$ANDROID_BUILD_TOP is not set. Source build/envsetup.sh "
                 "and run lunch.")

    if not os.path.exists(root):
        sys.exit("ANDROID_BUILD_TOP [{}] doesn't exist".format(root))
    if not os.path.isdir(root):
        sys.exit("ANDROID_BUILD_TOP [{}] isn't a directory".format(root))

    if "ro.hardware" in props:
        out_dir = check_product_out_dir(props["ro.hardware"])

    if out_dir is None and "ro.product.device" in props:
        out_dir = check_product_out_dir(props["ro.product.device"])

    if out_dir is None:
        msg = ("Failed to find product out directory for ro.hardware = '{}', " +
               "ro.product.device = '{}'")
        msg = msg.format(props.get("ro.hardware", ""),
                         props.get("ro.product.device", ""))
        sys.exit(msg)

    return out_dir


def get_remote_pid(device, process_name):
    processes = gdbrunner.get_processes(device)
    if process_name not in processes:
        msg = "failed to find running process {}".format(process_name)
        sys.exit(msg)
    pids = processes[process_name]
    if len(pids) > 1:
        msg = "multiple processes match '{}': {}".format(process_name, pids)
        sys.exit(msg)

    # Fetch the binary using the PID later.
    return pids[0]


def upload_binary(device, remote_path, sysroot):
    local_path = os.path.normpath("{}/{}".format(sysroot, remote_path))
    if not os.path.exists(local_path):
        sys.exit("nonexistent upload path {}".format(local_path))
    if not os.path.isfile(local_path):
        sys.exit("upload path {} isn't a file".format(local_path))

    logging.info("uploading {} to {}".format(local_path, remote_path))
    logging.warning("running adb root")
    device.root()
    remount_msg = device.remount()
    if "verity" in remount_msg:
        sys.exit(remount_msg)

    device.push(local_path, remote_path)
    return open(local_path, "r")


def handle_switches(args, sysroot):
    """Fetch the targeted binary and determine how to attach gdb.

    Args:
        args: Parsed arguments.
        sysroot: Local sysroot path.

    Returns:
        (binary_file, attach_pid, run_cmd).
        Precisely one of attach_pid or run_cmd will be None.
    """

    device = args.device
    binary_file = None
    pid = None
    run_cmd = None

    if args.target_pid:
        # Fetch the binary using the PID later.
        pid = args.target_pid
    elif args.target_name:
        # Fetch the binary using the PID later.
        pid = get_remote_pid(device, args.target_name)
    elif args.run_cmd:
        if not args.run_cmd[0]:
            sys.exit("empty command passed to -r")
        if not args.run_cmd[0].startswith("/"):
            sys.exit("commands passed to -r must use absolute paths")
        run_cmd = args.run_cmd
        binary_file = gdbrunner.pull_file(device, run_cmd[0], user=args.user)
    elif args.upload_cmd:
        if not args.upload_cmd[0]:
            sys.exit("empty command passed to -u")
        if not args.upload_cmd[0].startswith("/"):
            sys.exit("commands passed to -u must use absolute paths")
        run_cmd = args.upload_cmd
        binary_file = upload_binary(device, run_cmd[0], sysroot)
    if binary_file is None:
        assert pid is not None
        try:
            binary_file = gdbrunner.pull_binary(device, pid=pid, user=args.user)
        except adb.ShellError:
            sys.exit("failed to pull binary for PID {}".format(pid))

    return (binary_file, pid, run_cmd)

def generate_gdb_script(sysroot, binary_file, is64bit, port):
    # Generate a gdb script.
    # TODO: Make stuff work for tapas?
    # TODO: Detect the zygote and run 'art-on' automatically.
    root = os.environ["ANDROID_BUILD_TOP"]
    symbols_dir = os.path.join(sysroot, "system", "lib64" if is64bit else "lib")
    vendor_dir = os.path.join(sysroot, "vendor", "lib64" if is64bit else "lib")

    solib_search_path = []
    symbols_paths = ["", "hw", "ssl/engines", "drm", "egl", "soundfx"]
    vendor_paths = ["", "hw", "egl"]
    solib_search_path += [os.path.join(symbols_dir, x) for x in symbols_paths]
    solib_search_path += [os.path.join(vendor_dir, x) for x in vendor_paths]
    solib_search_path = ":".join(solib_search_path)

    gdb_commands = ""
    gdb_commands += "file '{}'\n".format(binary_file.name)
    gdb_commands += "set solib-absolute-prefix {}\n".format(sysroot)
    gdb_commands += "set solib-search-path {}\n".format(solib_search_path)

    dalvik_gdb_script = os.path.join(root, "development", "scripts", "gdb",
                                     "dalvik.gdb")
    if not os.path.exists(dalvik_gdb_script):
        logging.warning(("couldn't find {} - ART debugging options will not " +
                         "be available").format(dalvik_gdb_script))
    else:
        gdb_commands += "source {}\n".format(dalvik_gdb_script)

    gdb_commands += "target remote :{}\n".format(port)
    return gdb_commands


def main():
    args = parse_args()
    device = args.device
    props = device.get_props()

    root = os.environ["ANDROID_BUILD_TOP"]
    out_dir = find_out_dir(props, root)
    sysroot = os.path.join(out_dir, "symbols")

    debug_socket = "/data/local/tmp/debug_socket"
    pid = None
    run_cmd = None

    # Fetch binary for -p, -n.
    binary_file, pid, run_cmd = handle_switches(args, sysroot)

    with binary_file:
        arch = gdbrunner.get_binary_arch(binary_file)
        is64bit = arch.endswith("64")

        # Start gdbserver.
        gdbserver_local_path = get_gdbserver_path(root, arch)
        gdbserver_remote_path = "/data/local/tmp/{}-gdbserver".format(arch)
        gdbrunner.start_gdbserver(
            device, gdbserver_local_path, gdbserver_remote_path,
            target_pid=pid, run_cmd=run_cmd, debug_socket=debug_socket,
            port=args.port, user=args.user)

        # Generate a gdb script.
        gdb_commands = generate_gdb_script(sysroot=sysroot,
                                           binary_file=binary_file,
                                           is64bit=is64bit,
                                           port=args.port)

        # Find where gdb is
        if sys.platform.startswith("linux"):
            platform_name = "linux-x86"
        elif sys.platform.startswith("darwin"):
            platform_name = "darwin-x86"
        else:
            sys.exit("Unknown platform: {}".format(sys.platform))
        gdb_path = os.path.join(root, "prebuilts", "gdb", platform_name, "bin",
                                "gdb")

        # Start gdb.
        gdbrunner.start_gdb(gdb_path, gdb_commands)

if __name__ == "__main__":
    main()
