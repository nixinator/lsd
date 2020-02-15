# SPDX-License-Identifier: GPL-3.0-or-later
#
# this file is part of LIBRESTACK
#
# Copyright (c) 2012-2020 Brett Sheffield <bacs@librecast.net>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program (see the file COPYING in the distribution).
# If not, see <http://www.gnu.org/licenses/>.

PROGRAM = lsd
export PROGRAM

PREFIX = /usr/local
export PREFIX

LIB_PATH = $(PREFIX)/lib
export LIB_PATH

BIN_PATH = $(PREFIX)/sbin
export BIN_PATH

DB_PATH = /var/cache/lsd/
export DB_PATH

CFLAGS += -Wall -Werror -Wextra -g
export CFLAGS

.PHONY: all clean src

all:	src

clean:
	@$(MAKE) -C src $@

install:
	@$(MAKE) -C src $@

src:
	@$(MAKE) -C src all

