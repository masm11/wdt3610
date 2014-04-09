/*
 * Watchdog utility for MicroPC EES-3610.
 * Copyright (C) 2006 Yuuki Harano
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdio.h>
#include <sys/fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>

#define DEV "/dev/watchdog"

static int fd = -1;
static volatile int terminate = 0;

static void write1(int c)
{
    char buf[2] = { c, 0 };
    write(fd, buf, 1);
}

static void sig_term(int sig)
{
    terminate = 1;
}

int main(void)
{
    if ((fd = open(DEV, O_WRONLY)) == -1) {
	perror(DEV);
	exit(1);
    }
    
    if (daemon(0, 0) == -1) {
	perror("daemon");
	exit(1);
    }
    
    signal(SIGTERM, sig_term);
    
    while (!terminate) {
	write1('1');
	sleep(1);
    }
    write1('V');
    
    return 0;
}
