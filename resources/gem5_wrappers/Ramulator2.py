from m5.SimObject import *
from m5.params import *
from m5.objects.AbstractMemory import *

class Ramulator2(AbstractMemory):
  type = "Ramulator2"
  cxx_class = "gem5::memory::Ramulator2"
  cxx_header = "mem/ramulator2.hh"

  port = ResponsePort("The port for receiving memory requests and sending responses")
  config_path = Param.String("Path to the DRAMSys configuration")

  