CXX = g++
CXXFLAGS = -std=c++11 -Wall -O2
CPPFLAGS = -I../include
LDFLAGS =
LIBS = -llapack -lblas

HEADERS = $(wildcard ../include/LinAlg/*.h) $(wildcard ../include/MatOp/*.h) $(wildcard ../include/*.h)

.PHONY: all test clean

all: QR.out Eigen.out LDL.out LU.out SymEigs.out SymEigsShift.out GenEigs.out GenEigsRealShift.out

test:
	-./QR.out
	-./Eigen.out
	-./LDL.out
	-./LU.out
	-./SymEigs.out
	-./SymEigsShift.out
	-./GenEigs.out
	-./GenEigsRealShift.out

%.out: %.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $< -o $@ $(LDFLAGS) $(LIBS)

clean:
	-rm *.out
