/*
 *      This file is part of frosted.
 *
 *      frosted is free software: you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License version 2, as
 *      published by the Free Software Foundation.
 *
 *
 *      frosted is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with frosted.  If not, see <http://www.gnu.org/licenses/>.
 *
 *      Authors: brabo
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>

#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 4
#endif

extern int sys_clock_gettime(int clock_id, struct timespec *tp);

#ifndef APP_UPTIME_MODULE
int main(int argc, char *args[])
#else
int icebox_uptime(int argc, char *args[])
#endif
{
	struct timespec ts;
	struct tm *tm;
	time_t now, up_hours, up_mins;
	long days = 0;

	sys_clock_gettime(CLOCK_MONOTONIC, &ts);
	now = ts.tv_sec;

	days = now / 86400;
	up_hours = (now % 86400) / 3600;
	up_mins = (now % 3600) / 60;

	sys_clock_gettime(CLOCK_REALTIME, &ts);
	tm = localtime(&ts.tv_sec);
	if (tm) {
		printf(" %02d:%02d:%02d up %ld days, %02ld:%02ld, 1 user\r\n",
		       tm->tm_hour, tm->tm_min, tm->tm_sec,
		       days, up_hours, up_mins);
	} else {
		printf(" up %ld days, %02ld:%02ld, 1 user\r\n",
		       days, up_hours, up_mins);
	}

	exit(0);
}
