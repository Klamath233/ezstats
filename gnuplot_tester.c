#include "gnuplot_i.h"
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
int main() {
	double x[] = {1.0, 2.0, 3.0, 4.0, 5.0};
	double y[] = {1.0, 2.0, 3.0, 4.0, 5.0};

	fclose(stdout);
	int pid = fork();

	if (pid == 0) {
		freopen("hellohello", "a+", stdout);
		gnuplot_ctrl *gc = gnuplot_init();
		gnuplot_cmd(gc, "set terminal dumb");
		gnuplot_setstyle(gc, "dots");
		gnuplot_plot_xy(gc, x, y, 5, "title");
		gnuplot_close(gc);
		exit(0);
	}

	int wstatus = 0;
	waitpid(pid, &wstatus, 0);
	printf("hi");
	return 0;
}