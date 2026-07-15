CXX = g++
CXXFLAGS = -std=c++17 -O2 -I../src -I../src/DSP -L/usr/lib/aarch64-linux-gnu
LDLIBS = -lfftw3 -lsndfile -lpthread

OBJS = SDNA_FFT.o SDNA_Analyzer.o SDNA_SpectralProcessor.o \
       SDNA_DynamicsProcessor.o SDNA_StereoProcessor.o \
       SDNA_NoiseProcessor.o SDNA_TextureProcessor.o \
       SDNA_AirProcessor.o SDNA_MovementProcessor.o \
       SDNA_SpaceProcessor.o SDNA_GlueProcessor.o \
       SDNA_ResonanceProcessor.o SDNA_TransferEngine.o \
       SDNA_MorphEngine.o SDNA_Types.o

vpath %.cpp ../src/DSP
vpath %.h ../src/DSP ../src

.PHONY: all clean

all: test_dsp

test_dsp: $(OBJS) test_dsp.o
	$(CXX) $(CXXFLAGS) $^ $(LDLIBS) -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

test_dsp.o: test_dsp.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f *.o test_dsp
