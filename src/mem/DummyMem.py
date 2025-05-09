from m5.objects.AbstractMemory import *
from m5.params import *
from m5.proxy import Parent
from m5.SimObject import *


class DummyMem(AbstractMemory):
    type = "DummyMem"
    cxx_class = "gem5::memory::DummyMem"
    cxx_header = "mem/dummymem.hh"

    port = ResponsePort(
        "The port for receiving memory requests and sending responses"
    )
    record = Param.Bool("Set true to record trace")


#   system = Param.System(Parent.any, "System this memory is part of")
