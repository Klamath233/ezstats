CFLAG="-g"

all:
	cc -o statsd gnuplot_i.c statsd.c

clean:
	rm statsd