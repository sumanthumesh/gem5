from m5.SimObject import *
from m5.params import *
from m5.objects.AbstractMemory import *

class SimpleGem5Mem(AbstractMemory):
  type = "SimpleGem5Mem"
  cxx_class = "gem5::memory::SimpleGem5Mem"
  cxx_header = "mem/simple_gem5_mem.hh"

  port = ResponsePort("The port for receiving memory requests and sending responses")

  