from m5.objects.AbstractMemory import *
from m5.params import *
from m5.proxy import Parent
from m5.SimObject import *


class CXLSimGem5(AbstractMemory):
    type = "CXLSimGem5"
    cxx_class = "gem5::memory::CXLSimGem5"
    cxx_header = "mem/cxlsim_gem5.hh"

    port = ResponsePort(
        "The port for receiving memory requests and sending responses"
    )
    tck = Param.Float("DRAM clock period in ns")
    config_path = Param.String("Path to ramulator config file")
    skip_cycle = Param.Bool("Whether to skip cycles or not")
    all_dam = Param.Bool("Whether all accesses should goto DAM")
    all_cxl = Param.Bool("Whether all accesses should goto CXL")
    record = Param.Bool("Set true to record trace")
    res_size = Param.Unsigned("Size of the reserved DAM fraction in number of pages")
    dam_size = Param.Unsigned("Size of the DAM in number of pages")
    page_size = Param.Unsigned("Size of a page in Bytes")
    cxl_mode = Param.Unsigned("Mode in which memory should operate")

#   system = Param.System(Parent.any, "System this memory is part of")
