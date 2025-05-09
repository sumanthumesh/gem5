CC := clang++
CXX_FLAGS := -g

GEM5_ROOT := /data2/sumanthu/gem5
GEM5_INCLUDE := ${GEM5_ROOT}/include
M5OP_LIB := ${GEM5_ROOT}/util/m5/src/abi/x86/libm5op.so

pseudo_ins : pseudo_ins.cpp
	$(CC) -o pseudo_ins $(CXX_FLAGS) pseudo_ins.cpp $(M5OP_LIB) -I$(GEM5_INCLUDE)

mem_region : mem_region.cpp
	$(CC) -o mem_region $(CXX_FLAGS) mem_region.cpp $(M5OP_LIB) -I$(GEM5_INCLUDE)
