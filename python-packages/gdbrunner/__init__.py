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

'''Helpers used by both gdbclient.py and ndk-gdb.py.'''

import adb
import argparse
import atexit
import os
import subprocess
import tempfile

class ArgumentParser(argparse.ArgumentParser):
    """ArgumentParser subclass that provides adb device selection."""

    class DeviceAction(argparse.Action):
        def __call__(self, parser, namespace, values, option_string=None):
            if option_string is None:
                raise RuntimeError("DeviceAction called without option_string")
            elif option_string == "-a":
                # Handled in parse_args
                return
            elif option_string == "-d":
                namespace.device = adb.get_usb_device()
            elif option_string == "-e":
                namespace.device = adb.get_emulator_device()
            elif option_string == "-s":
                namespace.device = adb.get_device(values[0])
            else:
                raise RuntimeError("Unexpected flag {}".format(option_string))

    def __init__(self):
        super(ArgumentParser, self).__init__()
        group = self.add_argument_group(title="device selection")
        group = group.add_mutually_exclusive_group()
        group.add_argument(
            "-a", nargs=0, action=self.DeviceAction,
            help="directs commands to all interfaces")
        group.add_argument(
            "-d", nargs=0, action=self.DeviceAction,
            help="directs commands to the only connected USB device")
        group.add_argument(
            "-e", nargs=0, action=self.DeviceAction,
            help="directs commands to the only connected emulator")
        group.add_argument(
            "-s", nargs=1, metavar="SERIAL", action=self.DeviceAction,
            help="directs commands to device/emulator with the given serial")

    def parse_args(self, args=None, namespace=None):
        result = super(ArgumentParser, self).parse_args(args, namespace)
        # Default to -a behavior if no flags are given.
        if "device" not in result:
            result.device = adb.get_device()
        return result


def get_run_as_cmd(user, cmd):
    """Generate a run-as or su command depending on user."""

    if user is None:
        return cmd
    elif user == "root":
        return ["su", "0"] + cmd
    else:
        return ["run-as", user] + cmd


def get_processes(device):
    """Return a dict from process name to list of running PIDs on the device."""

    # Some custom ROMs use busybox instead of toolbox for ps. Without -w,
    # busybox truncates the output, and very long package names like
    # com.exampleisverylongtoolongbyfar.plasma exceed the limit.
    #
    # Perform the check for this on the device to avoid an adb roundtrip
    # Some devices might not have readlink or which, so we need to handle
    # this as well.

    ps_script = """
        if [ ! -x /system/bin/readlink -o ! -x /system/bin/which ]; then
            ps;
        elif [ $(readlink $(which ps)) == "toolbox" ]; then
            ps;
        else
            ps -w;
        fi
    """
    ps_script = " ".join([line.strip() for line in ps_script.splitlines()])

    output, _ = device.shell([ps_script])

    processes = dict()
    output = output.replace("\r", "").splitlines()
    columns = output.pop(0).split()
    try:
        pid_column = columns.index("PID")
    except ValueError:
        pid_column = 1
    while output:
        columns = output.pop().split()
        process_name = columns[-1]
        pid = int(columns[pid_column])
        if process_name in processes:
            processes[process_name].append(pid)
        else:
            processes[process_name] = [pid]

    return processes


def start_gdbserver(device, gdbserver_local_path, gdbserver_remote_path,
                    target_pid, run_cmd, debug_socket, port, user=None):
    """Start gdbserver in the background and forward necessary ports.

    Args:
        device: ADB device to start gdbserver on.
        gdbserver_local_path: Host path to push gdbserver from.
        gdbserver_remote_path: Device path to push gdbserver to.
        target_pid: PID of device process to attach to.
        run_cmd: Command to run on the device.
        debug_socket: Device path to place gdbserver unix domain socket.
        port: Host port to forward the debug_socket to.
        user: Device user to run gdbserver as.

    Returns:
        Popen handle to the `adb shell` process gdbserver was started with.
    """

    assert target_pid is None or run_cmd is None

    # Push gdbserver to the target.
    device.push(gdbserver_local_path, gdbserver_remote_path)

    # Run gdbserver.
    gdbserver_cmd = [gdbserver_remote_path, "--once",
                     "+{}".format(debug_socket)]

    if target_pid is not None:
        gdbserver_cmd += ["--attach", str(target_pid)]
    else:
        gdbserver_cmd += run_cmd

    device.forward("tcp:{}".format(port),
                   "localfilesystem:{}".format(debug_socket))
    atexit.register(lambda: device.forward_remove("tcp:{}".format(port)))
    gdbserver_cmd = get_run_as_cmd(user, gdbserver_cmd)

    # Use ppid so that the file path stays the same.
    gdbclient_output_path = os.path.join(tempfile.gettempdir(),
                                         "gdbclient-{}".format(os.getppid()))
    print "Redirecting gdbclient output to {}".format(gdbclient_output_path)
    gdbclient_output = file(gdbclient_output_path, 'w')
    return device.shell_popen(gdbserver_cmd, stdout=gdbclient_output,
                              stderr=gdbclient_output)


def pull_file(device, path, user=None):
    """Pull a file from a device as a user."""

    file_name = "gdbclient-binary-{}".format(os.getppid())
    remote_temp_path = "/data/local/tmp/{}".format(file_name)
    local_temp_path = os.path.join(tempfile.gettempdir(), file_name)
    cmd = get_run_as_cmd(user, ["cat", path, ">", remote_temp_path])
    try:
        device.shell(cmd)
    except adb.ShellError:
        raise RuntimeError("Failed to copy file to temporary folder on device")
    device.pull(remote_temp_path, local_temp_path)
    with open(local_temp_path, "r") as temp_file:
        return temp_file.read()


def pull_binary(device, pid, user=None):
    """Pull a running process's binary from a device as a user"""
    return pull_file(device, "/proc/{}/exe".format(pid), user)


def get_binary_arch(binary):
    """Parse a binary's ELF header for arch."""

    ei_class = ord(binary[0x4]) # 1 = 32-bit, 2 = 64-bit
    ei_data = ord(binary[0x5]) # Endianness

    assert ei_class == 1 or ei_class == 2
    if ei_data != 1:
        raise RuntimeError("binary isn't little-endian?")

    e_machine = ord(binary[0x13]) << 8 | ord(binary[0x12])
    if e_machine == 0x28:
        assert ei_class == 1
        return "arm"
    elif e_machine == 0xB7:
        assert ei_class == 2
        return "arm64"
    elif e_machine == 0x03:
        assert ei_class == 1
        return "x86"
    elif e_machine == 0x3E:
        assert ei_class == 2
        return "x86_64"
    elif e_machine == 0x08:
        if ei_class == 1:
            return "mips"
        else:
            return "mips64"
    else:
        raise RuntimeError("unknown architecture: 0x{:x}".format(e_machine))


def start_gdb(gdb_path, gdb_commands):
    """Start gdb in the background and block until it finishes.

    Args:
        gdb_path: Path of the gdb binary.
        gdb_commands: Contents of GDB script to run.
    """

    with tempfile.NamedTemporaryFile() as gdb_script:
        gdb_script.write(gdb_commands)
        gdb_script.flush()
        gdb_args = [gdb_path, "-x", gdb_script.name]
        gdb_process = subprocess.Popen(gdb_args)
        while gdb_process.returncode is None:
            try:
                gdb_process.communicate()
            except KeyboardInterrupt:
                pass

