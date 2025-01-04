CC := g++
CXX_FLAGS := -g
SHARED_FLAGS := -fPIC -shared

SOURCE_BASE := m5op

INCLUDE_DIR := /data1/sumanthu/gem5/include

TARGET :
	$(CC) -o lib$(SOURCE_BASE).so $(CXX_FLAGS) $(SHARED_FLAGS) $(SOURCE_BASE).S -I$(INCLUDE_DIR)
	$(CC) -o $(SOURCE_BASE).o $(CXX_FLAGS) -c $(SOURCE_BASE).S -I$(INCLUDE_DIR)
	ar rcs lib$(SOURCE_BASE).a $(SOURCE_BASE).o

clean:
	rm -f *.so *.a
