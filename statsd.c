#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <float.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include "gnuplot_i.h"

#define BUF_SIZE 2048
#define NUM_MAX_CLIENTS 256
#define SV_SOCK_PATH "/tmp/statsd_server.sock"

typedef struct {
	int used;
	double x_sum;
	double y_sum;
	double x_sumq;
	double y_sumq;
	double x_cnt;
	double y_cnt;
	double x_min;
	double y_min;
	double x_max;
	double y_max;
	FILE *log;
	char *logname;
	char *graphname;
} client_state;

typedef struct {

} server_state;

void run_server() {
	struct sockaddr_un svaddr, claddr;
	int sfd, i, j;
	ssize_t numBytes;
	socklen_t len;
	char buf[BUF_SIZE];
	char outbuf[BUF_SIZE];

	memset(buf, 0, BUF_SIZE);
	memset(outbuf, 0, BUF_SIZE);

	client_state *states = (client_state *) malloc(NUM_MAX_CLIENTS * sizeof(client_state));

	for (i = 0; i < NUM_MAX_CLIENTS; i++) {
		memset(&(states[i]), 0, sizeof(client_state));
	}

	sfd = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (sfd == -1) {
		exit(1);
	}

	if (remove(SV_SOCK_PATH) == -1 && errno != ENOENT) {
		exit(1);
	}

	memset(&svaddr, 0, sizeof(struct sockaddr_un));
	svaddr.sun_family = AF_UNIX;
	strncpy(svaddr.sun_path, SV_SOCK_PATH, sizeof(svaddr.sun_path) - 1);

	if (bind(sfd, (struct sockaddr *) &svaddr, sizeof(struct sockaddr_un)) == -1) {
		exit(1);
	}

	while (1) {
		len = sizeof(struct sockaddr_un);
		numBytes = recvfrom(sfd, buf, BUF_SIZE, 0, (struct sockaddr *) &claddr, &len);

		if (numBytes == -1) {
			exit(1);
		}

		int type = *((int *) &buf[0]);

		if (type == 0) {
			int next_available_id = 0;
			while (next_available_id < NUM_MAX_CLIENTS) {
				if (!states[next_available_id].used) {
					break;
				} else {
					++next_available_id;
				}
			}

			if (next_available_id < NUM_MAX_CLIENTS) {
				client_state *cs = &(states[next_available_id]);
				cs->used = 1;
				cs->x_min = DBL_MAX;
				cs->x_max = -DBL_MAX;
				cs->y_min = DBL_MAX;
				cs->y_max = -DBL_MAX;
				char *fname = (char *) malloc(64);
				memset(fname, 0, 64);
				sprintf(fname, "/tmp/statsd_server_log_%d.sock", next_available_id);
				cs->log = fopen(fname, "a+");
				cs->logname = fname;

				char *gname = (char *) malloc(64);
				memset(gname, 0, 64);
				sprintf(gname, "/tmp/statsd_server_graph_%d.sock", next_available_id);
				cs->graphname = gname;

				((int *)outbuf)[0] = 1;
				((int *)outbuf)[1] = next_available_id;

				sendto(sfd, outbuf, BUF_SIZE, 0, (struct sockaddr *) &claddr, len);
				memset(outbuf, 0, BUF_SIZE);
			} else {
				((int *)outbuf)[0] = 6;
				strcpy(&(outbuf[4]), "no available id");

				sendto(sfd, outbuf, BUF_SIZE, 0, (struct sockaddr *) &claddr, len);
				memset(outbuf, 0, BUF_SIZE);
			}
		} else if (type == 2) {
			int id = *((int *) &buf[4]);
			double x = ((double *) &buf[8])[0];
			double y = ((double *) &buf[8])[1];

			if (states[id].used == 1) {
				client_state *cs = &(states[id]);

				cs->x_sum = cs->x_sum + x;
				cs->x_sumq = cs->x_sumq + x * x;
				cs->x_cnt++;
				cs->x_min = (x < cs->x_min) ? x : cs->x_min;
				cs->x_max = (x > cs->x_max) ? x : cs->y_max;

				cs->y_sum = cs->y_sum + y;
				cs->y_sumq = cs->y_sumq + y * y;
				cs->y_cnt++;
				cs->y_min = (y < cs->y_min) ? y : cs->y_min;
				cs->y_max = (y > cs->y_max) ? y : cs->y_max;

				fprintf(cs->log, "%f %f\n", x, y);

				((int *)outbuf)[0] = 7;
				strcpy(&(outbuf[4]), "success");

				sendto(sfd, outbuf, BUF_SIZE, 0, (struct sockaddr *) &claddr, len);
				memset(outbuf, 0, BUF_SIZE);
			} else {
				((int *)outbuf)[0] = 6;
				strcpy(&(outbuf[4]), "id is not bound");

				sendto(sfd, outbuf, BUF_SIZE, 0, (struct sockaddr *) &claddr, len);
				memset(outbuf, 0, BUF_SIZE);
			}
		} else if (type == 3) {
			int id = *((int *) &buf[4]);
			if (states[id].used == 1) {
				client_state *cs = &(states[id]);

				if (cs->x_cnt == 0) {
					((int *) outbuf)[0] = 6;
					strcpy(&(outbuf[4]), "no data fed yet");

					sendto(sfd, outbuf, BUF_SIZE, 0, (struct sockaddr *) &claddr, len);
					memset(outbuf, 0, BUF_SIZE);
				}

				double x_avg = cs->x_sum / cs->x_cnt;
				double y_avg = cs->y_sum / cs->y_cnt;
				double x_var = cs->x_sumq / cs->x_cnt - x_avg * x_avg;
				double y_var = cs->y_sumq / cs->y_cnt - y_avg * y_avg;
				double x_min = cs->x_min;
				double y_min = cs->y_min;
				double x_max = cs->x_max;
				double y_max = cs->y_max;

				fflush(cs->log);

				int pid = fork();

				if (pid < 0) {
					exit(1);
				}

				if (pid == 0) {
					freopen(cs->graphname, "a+", stdout);
					gnuplot_ctrl *gc = gnuplot_init();
					gnuplot_cmd(gc, "set terminal dumb");
					gnuplot_setstyle(gc, "lines");
					gnuplot_plot_atmpfile(gc, cs->logname, "");
					gnuplot_close(gc);
					fflush(stdout);
					exit(0);
				}

				int wstatus = 0;
				waitpid(pid, &wstatus, 0);

				// The graph is ready now.
				char *graph_buf = NULL;
				long graph_buf_len;
				FILE *graph_file = fopen(cs->graphname, "rb");

				if (graph_file) {
					fseek(graph_file, 0, SEEK_END);
					graph_buf_len = ftell(graph_file);
					fseek(graph_file, 0, SEEK_SET);
					graph_buf = (char *) malloc(graph_buf_len);
					if (graph_buf) {
						fread(graph_buf, 1, graph_buf_len, graph_file);
					}
				} else {
					((int *) outbuf)[0] = 6;
					strcpy(&(outbuf[4]), "server error");

					sendto(sfd, outbuf, BUF_SIZE, 0, (struct sockaddr *) &claddr, len);
					memset(outbuf, 0, BUF_SIZE);
				}

				if (graph_buf) {
					((int *)outbuf)[0] = 4;
					((int *)outbuf)[1] = id;
					((double *) (outbuf + 8))[0] = x_avg;
					((double *) (outbuf + 8))[1] = y_avg;
					((double *) (outbuf + 8))[2] = x_var;
					((double *) (outbuf + 8))[3] = y_var;
					((double *) (outbuf + 8))[4] = x_min;
					((double *) (outbuf + 8))[5] = y_min;
					((double *) (outbuf + 8))[6] = x_max;
					((double *) (outbuf + 8))[7] = y_max;
					strncpy(outbuf + 72, graph_buf, BUF_SIZE - 72 - 1);
					outbuf[sizeof(outbuf) - 1] = 0;
					sendto(sfd, outbuf, BUF_SIZE, 0, (struct sockaddr *) &claddr, len);
					memset(outbuf, 0, BUF_SIZE);
				} else {
					((int *) outbuf)[0] = 6;
					strcpy(&(outbuf[4]), "server error");

					sendto(sfd, outbuf, BUF_SIZE, 0, (struct sockaddr *) &claddr, len);
					memset(outbuf, 0, BUF_SIZE);
				}

			} else {
				((int *) outbuf)[0] = 6;
				strcpy(&(outbuf[4]), "id is not bound");

				sendto(sfd, outbuf, BUF_SIZE, 0, (struct sockaddr *) &claddr, len);
				memset(outbuf, 0, BUF_SIZE);
			}
		} else if (type == 5) {
			int id = *((int *) &buf[4]);

			if (states[id].used) {
				fclose(states[id].log);
				free(states[id].logname);
				free(states[id].graphname);
				memset(&(states[id]), 0, sizeof(client_state));

				((int *)outbuf)[0] = 7;
				strcpy(&(outbuf[4]), "success");

				sendto(sfd, outbuf, BUF_SIZE, 0, (struct sockaddr *) &claddr, len);
				memset(outbuf, 0, BUF_SIZE);
			} else {
				((int *) outbuf)[0] = 6;
				strcpy(&(outbuf[4]), "id is not bound");

				sendto(sfd, outbuf, BUF_SIZE, 0, (struct sockaddr *) &claddr, len);
				memset(outbuf, 0, BUF_SIZE);
			}
		} else {
			((int *) outbuf)[0] = 6;
			strcpy(&(outbuf[4]), "unknown type");

			sendto(sfd, outbuf, BUF_SIZE, 0, (struct sockaddr *) &claddr, len);
			memset(outbuf, 0, BUF_SIZE);
		}
	}
}

void daemonize() {
	pid_t pid, sid;

	pid = fork();
	if (pid < 0) {
		perror("fork error");
		exit(1);
	}

	if (pid > 0) {
		exit(0);
	}

	umask(0);

	sid = setsid();
	if (sid < 0) {
		perror("setsid error");
	}

	close(0);
	close(1);
	close(2);

	run_server();
}

int main() {
	daemonize();
	return 0;
}