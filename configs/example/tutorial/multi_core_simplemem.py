import argparse
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

args = parser.parse_args()

NUM_CORES: int = int(args.num_cores)
cmd: str = args.command

system = System()

system.clk_domain = SrcClockDomain()
system.clk_domain.clock = "1GHz"
system.clk_domain.voltage_domain = VoltageDomain()

system.mem_mode = "timing"
system.mem_ranges = [AddrRange("8GB")]

# Memory bus
system.membus = SystemXBar()

# Create CPUs
system.cpus = [DerivO3CPU(cpu_id=i) for i in range(NUM_CORES)]

system.l3cache = L3Cache(size="8MB", assoc=16)  # Shared L3 cache

system.l2_to_l3bus = SystemXBar()

for cpu in system.cpus:
    # Interrupt stuff
    cpu.createInterruptController()
    cpu.interrupts[0].pio = system.membus.mem_side_ports
    cpu.interrupts[0].int_requestor = system.membus.cpu_side_ports
    cpu.interrupts[0].int_responder = system.membus.mem_side_ports

    # Create L1 caches (private to each core)
    cpu.icache = L1ICache(size="32kB", assoc=4)
    cpu.dcache = L1DCache(size="32kB", assoc=4)

    # Connect CPU to L1
    cpu.icache_port = cpu.icache.cpu_side
    cpu.dcache_port = cpu.dcache.cpu_side

    # Create L2
    cpu.l2cache = L2Cache(size="256kB", assoc=8)

    # Create bus from L1 to L2
    cpu.l1_to_l2bus = L2XBar()

    # Connect L1 to bus
    cpu.icache.mem_side = cpu.l1_to_l2bus.cpu_side_ports
    cpu.dcache.mem_side = cpu.l1_to_l2bus.cpu_side_ports

    # Connect bus to L2
    cpu.l2cache.cpu_side = cpu.l1_to_l2bus.mem_side_ports

    # Connect L2 to shared L3
    cpu.l2cache.mem_side = system.l2_to_l3bus.cpu_side_ports

# Connect shared bus to L3
system.l3cache.cpu_side = system.l2_to_l3bus.mem_side_ports

# Connect L3 to system bus
system.l3cache.mem_side = system.membus.cpu_side_ports

system.mem_ctrl = SimpleGem5Mem()
system.mem_ctrl.range = system.mem_ranges[0]
system.mem_ctrl.tck = 1 / 2.4
system.mem_ctrl.port = system.membus.mem_side_ports

# system.mem_ctrl = MemCtrl()
# system.mem_ctrl.dram = DDR3_1600_8x8()
# system.mem_ctrl.dram.range = system.mem_ranges[0]
# system.mem_ctrl.port = system.membus.mem_side_ports

# system.mem_ctrl = Ramulator2()
# system.mem_ctrl.config_path = './ext/ramulator2/ramulator2/example_config.yaml'
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
m5.instantiate()

print("Beginning simulation!")
exit_event = m5.simulate()

print(
    f"Exiting @ tick {m5.curTick()} because {exit_event.getCause()}"
)
