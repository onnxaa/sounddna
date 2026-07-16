CXX = g++
CXXFLAGS = -std=c++17 -O2 -I../src -I../src/DSP -L/usr/lib/aarch64-linux-gnu
LDLIBS = -lfftw3 -lsndfile -lpthread

OBJS = Geno_FFT.o Geno_Analyzer.o Geno_SpectralProcessor.o \
       Geno_DynamicsProcessor.o Geno_StereoProcessor.o \
       Geno_NoiseProcessor.o Geno_TextureProcessor.o \
       Geno_AirProcessor.o Geno_MovementProcessor.o \
       Geno_SpaceProcessor.o Geno_GlueProcessor.o \
       Geno_ResonanceProcessor.o Geno_TransferEngine.o \
       Geno_MorphEngine.o Geno_Types.o

vpath %.cpp ../src/DSP
vpath %.h ../src/DSP ../src

.PHONY: all clean

all: test_dsp test_edge test_deep test_pipeline

test_dsp: $(OBJS) test_dsp.o
	$(CXX) $(CXXFLAGS) $^ $(LDLIBS) -o $@

test_edge: $(OBJS) test_edge.o
	$(CXX) $(CXXFLAGS) $^ $(LDLIBS) -o $@

test_deep: $(OBJS) test_deep.o
	$(CXX) $(CXXFLAGS) $^ $(LDLIBS) -o $@

test_pipeline: $(OBJS) test_pipeline.o
	$(CXX) $(CXXFLAGS) $^ $(LDLIBS) -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

test_dsp.o: test_dsp.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

test_edge.o: test_edge.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

test_deep.o: test_deep.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

test_pipeline.o: test_pipeline.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f *.o test_dsp test_edge test_deep test_pipeline
