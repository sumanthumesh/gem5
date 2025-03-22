import argparse
import os
import sys

from cache import (
    L1DCache,
    L1ICache,
    L2Cache,
    L3Cache,
)

import m5
from m5.objects import *

parser = argparse.ArgumentParser(
    description="Multicore O3 CPU with private l1,l2 and shared l3. Memory is handled by ramulator"
)
parser.add_argument(
    "-c", "--command", help="Command to run enclosed in double quotes"
)
parser.add_argument(
    "-n", "--num-cores", help="Number of cores to simulate", default="1"
)
parser.add_argument(
    "--checkpoint-dir", help="Directory to store checkpoint", default=None
)
parser.add_argument(
    "--restore-dir",
    help="Directory from which to pick checkpoint",
    default=None,
)
parser.add_argument(
    "--no-cache",
    help="Instantiate system without any cache",
    action="store_true",
)
parser.add_argument(
    "--mem",
    help="Memory type: ramulator2, simplemem, atomicmem, cxlsim, dummymem",
    default="atomicmem",
)
parser.add_argument(
    "--record",
    help="Set to true if mem accesses need to be recorded",
    action="store_true",
    default=False
)
parser.add_argument(
    "--cpu",
    help="CPU type: timing, O3, atomic",
    default="O3",
)
parser.add_argument(
    "--mem-mode",
    help="Memory mode: timing, atomic",
    default="timing",
)
parser.add_argument(
    "--repeat-checkpoint",
    type=int,
    help="Repeatedly take checkpoint after these many instructions",
    default=None,
)
parser.add_argument(
    "--bus-width",
    type=int,
    help="Specify the system bus width",
    default=128,
)
parser.add_argument(
    "--cxlsim-config",
    help="Config file for cxlsim",
    default=None
)
parser.add_argument(
    "--all-dam",
    help="Set to true if all accesses need to goto DAM",
    action="store_true"
)
parser.add_argument(
    "--all-cxl",
    help="Set to true if all accesses need to goto CXL",
    action="store_true"
)


args = parser.parse_args()

# Both checkpoint-dir and restore-dir shouldnt be specified at the same time
assert not (
    args.restore_dir and args.checkpoint_dir
), "Cannot have both saving checkpoint and restoring it in same command"

# Check if the checkpoint dir exists
if args.restore_dir:
    assert os.path.exists(
        args.restore_dir
    ), f"Cannot find checkpoint at {args.restore_dir} to restore"

NUM_CORES: int = int(args.num_cores)
cmd: str = args.command

system = System()

system.clk_domain = SrcClockDomain()
system.clk_domain.clock = "1GHz"
system.clk_domain.voltage_domain = VoltageDomain()

if args.checkpoint_dir:
    system.mem_mode = "atomic"
else:
    system.mem_mode = args.mem_mode
system.mem_ranges = [AddrRange("8GB")]

# Memory bus
system.membus = SystemXBar()
if args.bus_width != None:
    system.membus.width = args.bus_width


# Create CPUs
if args.checkpoint_dir:
    system.cpus = [AtomicSimpleCPU(cpu_id=i) for i in range(NUM_CORES)]
else:
    if args.cpu == "timing":
        system.cpus = [TimingSimpleCPU(cpu_id=i) for i in range(NUM_CORES)]
    elif args.cpu == "O3":
        system.cpus = [DerivO3CPU(cpu_id=i) for i in range(NUM_CORES)]
    elif args.cpu == "atomic":
        system.cpus = [AtomicSimpleCPU(cpu_id=i) for i in range(NUM_CORES)]
# system.cpus = [TimingSimpleCPU(cpu_id=i) for i in range(NUM_CORES)]

if not args.no_cache:
    # system.l3cache = L3Cache(size="8MB", assoc=16)  # Shared L3 cache
    system.l3cache = L3Cache(size=f"2MB", assoc=16)  # Shared L3 cache

    system.l2_to_l3bus = SystemXBar()
    if args.bus_width != None:
        system.l2_to_l3bus.width = args.bus_width

for cpu in system.cpus:
    # Interrupt stuff
    cpu.createInterruptController()
    cpu.interrupts[0].pio = system.membus.mem_side_ports
    cpu.interrupts[0].int_requestor = system.membus.cpu_side_ports
    cpu.interrupts[0].int_responder = system.membus.mem_side_ports

    if not args.no_cache:
        # Create L1 caches (private to each core)
        # cpu.icache = L1ICache(size="1kB", assoc=4)
        # cpu.dcache = L1DCache(size="32kB", assoc=4)
        cpu.icache = L1ICache(size="32kB", assoc=4)
        cpu.dcache = L1DCache(size="32kB", assoc=4)

        # Connect CPU to L1
        cpu.icache_port = cpu.icache.cpu_side
        cpu.dcache_port = cpu.dcache.cpu_side

        # Create L2
        # cpu.l2cache = L2Cache(size="256kB", assoc=8)
        cpu.l2cache = L2Cache(size="256kB", assoc=8)

        # Create bus from L1 to L2
        cpu.l1_to_l2bus = L2XBar()
        if args.bus_width != None:
            cpu.l1_to_l2bus.width = args.bus_width

        # Connect L1 to bus
        cpu.icache.mem_side = cpu.l1_to_l2bus.cpu_side_ports
        cpu.dcache.mem_side = cpu.l1_to_l2bus.cpu_side_ports

        # Connect bus to L2
        cpu.l2cache.cpu_side = cpu.l1_to_l2bus.mem_side_ports

        # Connect L2 to shared L3
        cpu.l2cache.mem_side = system.l2_to_l3bus.cpu_side_ports
    else:
        cpu.icache_port = system.membus.cpu_side_ports
        cpu.dcache_port = system.membus.cpu_side_ports

if not args.no_cache:
    # Connect shared bus to L3
    system.l3cache.cpu_side = system.l2_to_l3bus.mem_side_ports

    # Connect L3 to system bus
    system.l3cache.mem_side = system.membus.cpu_side_ports

if (args.checkpoint_dir and not args.no_cache) or args.mem == "atomicmem":
    system.mem_ctrl = MemCtrl()
    system.mem_ctrl.dram = DDR3_1600_8x8()
    system.mem_ctrl.dram.range = system.mem_ranges[0]
    system.mem_ctrl.port = system.membus.mem_side_ports
elif args.mem == "simplemem":
    mem_ctrl = SimpleGem5Mem()
    mem_ctrl.range = system.mem_ranges[0]
    mem_ctrl.tck = 1 / 1.6
    mem_ctrl.port = system.membus.mem_side_ports
    mem_ctrl.record = args.record
    system.mem_ctrl = mem_ctrl
elif args.mem == "cxlsim":
    mem_ctrl = CXLSimGem5()
    mem_ctrl.range = system.mem_ranges[0]
    mem_ctrl.tck = 1 / 1.6
    mem_ctrl.port = system.membus.mem_side_ports
    if args.cxlsim_config == None:
        print(f"CXLSIM config not specified")
        print(f"Using /data2/sumanthu/gem5/ext/cxlsim/cxlsim/ramulator/configs/DDR4-config.cfg as default")
        mem_ctrl.config_path = "/data2/sumanthu/gem5/ext/cxlsim/cxlsim/ramulator/configs/DDR4-config.cfg"
    else:
        mem_ctrl.config_path = os.path.abspath(args.cxlsim_config)
        print(f"Using {args.cxlsim_config}")
    if args.all_dam:
        mem_ctrl.all_dam = True
    else:
        mem_ctrl.all_dam = False
    if args.all_cxl:
        mem_ctrl.all_cxl = True
    else:
        mem_ctrl.all_cxl = False
    mem_ctrl.skip_cycle = False
    mem_ctrl.record = args.record
    system.mem_ctrl = mem_ctrl
elif args.mem == "ramulator2":
    mem_ctrl = Ramulator2()
    mem_ctrl.config_path = "/data2/sumanthu/gem5/ext/ramulator2/ramulator2/example_config.yaml"
    mem_ctrl.range = system.mem_ranges[0]
    mem_ctrl.port = system.membus.mem_side_ports
    mem_ctrl.record = args.record
    system.mem_ctrl = mem_ctrl
elif args.mem == "dummymem":
    #Make sure memory mode is atomic
    assert args.mem_mode == "timing", f"DummyMem only supports timing mode, not {args.mem_mode}"
    assert args.cpu != "atomic", f"DummyMem only supports timing/O3 cpu, not {args.cpu}"
    mem_ctrl = DummyMem()
    mem_ctrl.range = system.mem_ranges[0]
    mem_ctrl.record = args.record
    mem_ctrl.port = system.membus.mem_side_ports
    system.mem_ctrl = mem_ctrl
else:
    print(f"Unknown memory {args.mem}")
    exit(1)

    # system.mem_ctrl = SimpleGem5Mem()
    # system.mem_ctrl.range = system.mem_ranges[0]
    # system.mem_ctrl.tck = 1 / 2.4
    # system.mem_ctrl.port = system.membus.mem_side_ports
    # system.mem_ctrl.system = system
    # system.mem_ctrl = Ramulator2()
    # system.mem_ctrl.config_path = (
    #     "/data1/sumanthu/gem5/ext/ramulator2/ramulator2/example_config.yaml"
    # )
    # system.mem_ctrl.range = system.mem_ranges[0]
    # system.mem_ctrl.port = system.membus.mem_side_ports

# Connect system port
system.system_port = system.membus.cpu_side_ports

# binary = 'tests/test-progs/hello/bin/x86/linux/hello'
# binary = '/data1/sumanthu/Splash-3/codes/apps/ocean/contiguous_partitions/OCEAN'
# binary = '/data1/sumanthu/Splash-3/codes/kernels/fft/FFT'
# binary = '/data1/sumanthu/Splash-3/codes/apps/ocean/contiguous_partitions/OCEAN'
# binary = '/data1/sumanthu/hyrise/build_release/hyriseConsole'
binary = cmd.split(" ")[0]
# for gem5 V21 and beyond
system.workload = SEWorkload.init_compatible(binary)

process = Process()
# process.cmd = [binary,'/data1/sumanthu/hyrise/build_release/script.sql']
# process.cmd = [binary,'-p2', '-n258']
# process.cmd = [binary,'-p2', '-m16']
process.cmd = cmd.split(" ")
print(process.cmd)
for cpu in system.cpus:
    cpu.workload = process
    cpu.createThreads()

root = Root(full_system=False, system=system)

# mem_ctrl.system = root.system
# system.mem_ctrl.sys(system)
# mem_ctrl.system(system)

# Explicitly trigger a checkpoint from the Python script
if args.restore_dir:
    m5.instantiate(args.restore_dir)
else:
    m5.instantiate()
# root.checkpoint_at = 1000000  # This is the tick when the checkpoint will be taken
# m5.checkpoint("/data1/sumanthu/gem5/m5out")

print("Beginning simulation!")
exit_event = m5.simulate()

print(f"Exiting @ tick {m5.curTick()} because {exit_event.getCause()}")

# If the simulation decides to exit due to checkpoint, then resume simulation afterwards
if exit_event.getCause() == "checkpoint":
    if args.checkpoint_dir:
        # Perform the checkpoint
        print(f"Attemting to write checkpint to {args.checkpoint_dir}")
        m5.checkpoint(args.checkpoint_dir)

    print("Resuming simulation")
    exit_event = m5.simulate()

    print(
        "Exiting @ tick {} because {}".format(
            m5.curTick(), exit_event.getCause()
        )
    )

    print(exit_event.getCode())
