from m5.objects import *

# Custom cache classes (add these if not imported directly)
class L1ICache(Cache):
    def __init__(self, size='32kB', assoc=4):
        super(L1ICache, self).__init__()
        self.size = size
        self.assoc = assoc
        self.tag_latency = 2
        self.data_latency = 2
        self.response_latency = 2
        self.mshrs = 4
        self.tgts_per_mshr = 20

class L1DCache(Cache):
    def __init__(self, size='32kB', assoc=4):
        super(L1DCache, self).__init__()
        self.size = size
        self.assoc = assoc
        self.tag_latency = 2
        self.data_latency = 2
        self.response_latency = 2
        self.mshrs = 4
        self.tgts_per_mshr = 20

class L2Cache(Cache):
    def __init__(self, size='256kB', assoc=8):
        super(L2Cache, self).__init__()
        self.size = size
        self.assoc = assoc
        self.tag_latency = 20
        self.data_latency = 20
        self.response_latency = 20
        self.mshrs = 20
        self.tgts_per_mshr = 12


class L3Cache(Cache):
    def __init__(self, size='8MB', assoc=16):
        super(L3Cache, self).__init__()
        self.size = size
        self.assoc = assoc
        self.tag_latency = 30
        self.data_latency = 30
        self.response_latency = 30
        self.mshrs = 32
        self.tgts_per_mshr = 12
        self.is_llc = True