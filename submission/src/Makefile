P1 = fractal
CPP = g++
CFLAGS = -O2 -Wall
OMPFLAG = -fopenmp

all: $(P1)

$(P1): $(P1).cpp
	$(CPP) $(CFLAGS) $(OMPFLAG) $(P1).cpp -o $(P1)

clean:
	rm -vf $(P1)
