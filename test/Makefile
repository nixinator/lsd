# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2020 Brett Sheffield <bacs@librecast.net>

SHELL := /bin/bash
CFLAGS += -Wall -g
NOTOBJS := ../src/lsd.o ../src/echo.o # ../src/http.o
OBJS := test.o ../modules/http.o ../modules/websocket.o ../modules/librecast.o $(filter-out $(NOTOBJS), $(wildcard ../src/*.o))
LDFLAGS := -llibrecast -llsdb -llcdb -ldl -pthread -llmdb -lsodium -lwolfssl
BOLD := "\\e[0m\\e[2m"
RESET := "\\e[0m"
PASS = "\\e[0m\\e[32mOK\\e[0m" # end bold, green text
FAIL = "\\e[0m\\e[31mFAIL\\e[0m" # end bold, red text
LEAK = "\\e[0m\\e[31mFAIL \| LEAK\\e[0m\\n\\tSee $@.valgrind"
LASTLOG := lastlog.log
LOGFILE := $(shell mktemp "lsdb-test-XXXXXXXX.log")
TIMESTAMP = $(shell date +"%Y-%m-%d %H:%M:%S")
VALGRIND = valgrind --leak-check=full --error-exitcode=2 --errors-for-leak-kinds=all --track-origins=yes --suppressions=valgrind.suppressions --log-file=$@.valgrind

.PHONY: test clean realclean build result check

check: MEMCHECK = $(VALGRIND)
check: FAIL = $(LEAK)
check: test

sanitize: CFLAGS += -fsanitize=address -fno-omit-frame-pointer
sanitize: test

test: clean build $(shell echo ????-????.c | sed 's/\.c/\.test/g') result

src modules:
	$(MAKE) -C ../$@

build: src modules
	@echo -e "$(TIMESTAMP) - starting tests" >> $(LOGFILE)
	@echo -e "\nRunning tests"

# link with any libraries needed for specific tests
#0000-0003.test: LDFLAGS += -pthread

%.test: %.c $(OBJS)
	@$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo -ne "\e[2m" $* " "
	@echo -ne "\n== $@" >> $(LOGFILE)
	@$(MEMCHECK) ./$@ 2>> $(LOGFILE) && echo -e " $(PASS)" || echo -e " $(FAIL)"
	@ln -sf $(LOGFILE) $(LASTLOG)
	@$(eval tests_run=$(shell echo $$(($(tests_run)+1))))

%.check: MEMCHECK = $(VALGRIND)
%.check: FAIL = $(LEAK)
%.check: %.test
	@echo "check completed"
	@echo -e "    logfile:   " $(BOLD) $(LOGFILE) / $(LASTLOG) $(RESET)
	@echo -e "    valgrind:  " $(BOLD) "$^.valgrind" $(RESET)

%.debug: MEMCHECK = gdb
%.debug: %.test
	@echo "exiting debugger"

test.o: test.h

result:
	@echo -e "\n$(TIMESTAMP) - tests done" >> $(LOGFILE)
	@echo -e "$(tests_run) tests run\nlogfile: $(LOGFILE)\n"

clean:
	rm -f *.test *.o
	@rm -f $(LOGFILE) $(LASTLOG)

realclean: clean
	rm -f lsdb-test-????????.log* ????-????.test.valgrind
	rm -rf ????-????.tmp.* *core*
