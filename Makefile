TARGET := toy.cpp
OUTPUT := toy

CXX := clang++-3.8
LLVM_CONFIG := llvm-config-3.8
CXXFLAGS := -O3 -std=c++11 $(shell $(LLVM_CONFIG) --cxxflags) -Wall
LIBS := -lm $(shell $(LLVM_CONFIG) --system-libs --ldflags --libs --system-libs core)

all:
	$(CXX) $(TARGET) $(CXXFLAGS) $(LIBS) -o $(OUTPUT)