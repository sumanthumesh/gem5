from m5.objects.AbstractMemory import *
from m5.params import *
from m5.proxy import Parent
from m5.SimObject import *


class SimpleGem5Mem(AbstractMemory):
    type = "SimpleGem5Mem"
    cxx_class = "gem5::memory::SimpleGem5Mem"
    cxx_header = "mem/simple_gem5_mem.hh"

    port = ResponsePort(
        "The port for receiving memory requests and sending responses"
    )
    tck = Param.Float("DRAM clock period in ns")
    record = Param.Bool("Set to true if you want to record traces")


#   system = Param.System(Parent.any, "System this memory is part of")
