# Define the compiler (using the same clang version as your build)
CXX = clang++
LINKER=clang++
LD = /usr/bin/mold

# Directories
BIN_DIR = bazel-out/k8-fastbuild/bin
OBJ = $(BIN_DIR)/_objs/background_subtractor_test/background_subtractor_test.o
LIBS = $(BIN_DIR)/libbackground_subtractor_lib.a \
       $(BIN_DIR)/external/googletest~/libgtest_main.a \
       $(BIN_DIR)/external/googletest~/libgtest.a

# Final binary
TARGET = dev_test

# Build command (using bazelisk as preferred by the user)
$(TARGET): $(OBJ) $(LIBS)
	@echo "Linking with mold..."
	@$(LINKER) -fuse-ld=mold -o $(TARGET) $(OBJ) $(LIBS) -rdynamic -lpthread -lm -pthread -labic++

# Build command (if you change a .cc file, just run 'make')
all: $(TARGET)
	@./$(TARGET)

clean:
	rm -f $(TARGET)
