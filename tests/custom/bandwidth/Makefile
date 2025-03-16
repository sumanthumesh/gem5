CC := clang++
INC := ../../../include
LIB := ../../../util/m5/src/abi/x86/libm5op.a

TARGET := test_bandwidth

$(TARGET) : $(TARGET).cpp
	$(CC) -o $(TARGET) $(TARGET).cpp $(LIB) -I$(INC)