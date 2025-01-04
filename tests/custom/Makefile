CC := clang++
CXX_FLAGS := -g

GEM5_INCLUDE := /data1/sumanthu/gem5/include
M5OP_LIB := /data1/sumanthu/gem5/util/m5/src/abi/x86/libm5op.so

pseudo_ins : pseudo_ins.cpp
	$(CC) -o pseudo_ins $(CXX_FLAGS) pseudo_ins.cpp $(M5OP_LIB) -I$(GEM5_INCLUDE)