CXX := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Iinclude -Ithird_party/include/harfbuzz -Ithird_party/include/fribidi
LDFLAGS := -Lthird_party/lib -lharfbuzz -lfribidi -Wl,-rpath,'$$ORIGIN/../third_party/lib'

LIB_OBJS := build/utf8.o build/segments.o build/engine.o build/selection.o

all: bin/demo bin/selftest

build/%.o: src/%.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/libsubtitle_raster251.a: $(LIB_OBJS)
	ar rcs $@ $^

bin/demo: examples/demo.cpp build/libsubtitle_raster251.a | bin
	$(CXX) $(CXXFLAGS) $< build/libsubtitle_raster251.a -o $@ $(LDFLAGS)

bin/selftest: tests/selftest.cpp build/libsubtitle_raster251.a | bin
	$(CXX) $(CXXFLAGS) $< build/libsubtitle_raster251.a -o $@ $(LDFLAGS)

build bin:
	mkdir -p $@

test: bin/selftest
	./bin/selftest

clean:
	rm -rf build bin

.PHONY: all test clean
