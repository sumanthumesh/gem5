import m5
from m5.objects import *
from cache import L1DCache,L1ICache,L2Cache
import sys

NUM_CORES = int(sys.argv[1])

system = System()

system.clk_domain = SrcClockDomain()
system.clk_domain.clock = '1GHz'
system.clk_domain.voltage_domain = VoltageDomain()

system.mem_mode = 'timing'
system.mem_ranges = [AddrRange('8GB')]

#Memory bus
system.membus = SystemXBar()

#Create CPUs
system.cpus = [DerivO3CPU(cpu_id=i) for i in range(NUM_CORES)]

for cpu in system.cpus:

    # Interrupt stuff
    cpu.createInterruptController()
    cpu.interrupts[0].pio = system.membus.mem_side_ports
    cpu.interrupts[0].int_requestor = system.membus.cpu_side_ports
    cpu.interrupts[0].int_responder = system.membus.mem_side_ports

    # Create L1 caches (private to each core)
    cpu.icache = L1ICache(size='32kB', assoc=4)
    cpu.dcache = L1DCache(size='32kB', assoc=4)

    # Connect CPU to L1
    cpu.icache_port = cpu.icache.cpu_side
    cpu.dcache_port = cpu.dcache.cpu_side

    # Create L2
    cpu.l2cache = L2Cache(size='256kB', assoc=8)

    # Create bus from L1 to L2 
    cpu.l1_to_l2bus = L2XBar()

    # Connect L1 to bus
    cpu.icache.mem_side = cpu.l1_to_l2bus.cpu_side_ports
    cpu.dcache.mem_side = cpu.l1_to_l2bus.cpu_side_ports

    # Connect bus to L2
    cpu.l2cache.cpu_side = cpu.l1_to_l2bus.mem_side_ports

    #Connect L2 to Memory bus
    cpu.l2cache.mem_side = system.membus.cpu_side_ports

system.mem_ctrl = MemCtrl()
system.mem_ctrl.dram = DDR3_1600_8x8()
system.mem_ctrl.dram.range = system.mem_ranges[0]
system.mem_ctrl.port = system.membus.mem_side_ports

#Connect system port
system.system_port = system.membus.cpu_side_ports

# binary = 'tests/test-progs/hello/bin/x86/linux/hello'
binary = '/data1/sumanthu/Splash-3/codes/kernels/fft/FFT'

# for gem5 V21 and beyond
system.workload = SEWorkload.init_compatible(binary)

process = Process()
process.cmd = [binary,"-p2","-m16"]
for cpu in system.cpus:
    cpu.workload = process
    cpu.createThreads()

root = Root(full_system = False, system = system)
m5.instantiate()

print("Beginning simulation!")
exit_event = m5.simulate()

print('Exiting @ tick {} because {}'
      .format(m5.curTick(), exit_event.getCause()))