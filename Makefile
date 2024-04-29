CC = gcc
CFLAGS = -Wall -Wextra

SHELLNAME = utcsh
CFLAGS_REL = -O3 -g
CFLAGS_DEB = -Og -g3 -fno-omit-frame-pointer
CFLAGS_SAN = -fsanitize=undefined -fsanitize=address -fno-omit-frame-pointer -g

TESTDIR=tests
TESTSCRIPT=$(TESTDIR)/run-tests.py

SRCS = utcsh.c util.c
HEADERS = util.h
FILES = $(SRCS) $(HEADERS)

$(SHELLNAME): $(FILES)
	$(CC) $(CFLAGS) $(CFLAGS_REL) $(SRCS) -o $(SHELLNAME)

debug: $(FILES)
	$(CC) $(CFLAGS) $(CFLAGS_DEB) $(SRCS) -o $(SHELLNAME)

asan: $(FILES)
	$(CC) $(CFLAGS) $(CFLAGS_SAN) $(SRCS) -o $(SHELLNAME)

################################
# Prepare your work for upload #
################################

FILENAME = turnin.tar

turnin.tar: clean
	tar cvf $(FILENAME) `find . -type f | grep -v \.git | grep -v \.tar$$ | grep -v \.tar\.gz$$ | grep -v \.swp$$ | grep -v ~$$`
	gzip $(FILENAME)

turnin: turnin.tar
	@echo "================="
	@echo "Created $(FILENAME).gz for submission.  Please upload to Canvas."
	@echo "Before uploading, please verify:"
	@echo "     - Your README is correctly filled out."
	@echo "     - Your pair programming log is in the project directory."
	@echo "If either of those items are not done, please update your submission and run the make turnin command again."
	@ls -al $(FILENAME).gz

#########################
# Various utility rules #
#########################

clean:
	rm -f $(SHELLNAME) *.o *~
	rm -f .utcsh.grade.json readme.html shellspec.html
	rm -rf tests-out

# Checks that the test scripts have valid executable permissions and fix them if not.
validtestperms: $(TESTSCRIPT)
	@test -x $(TESTSCRIPT)  && { echo "Testscript permissions appear correct!"; } || \
{ echo "Testscript does not have executable permissions. Please run \`chmod u+x $(TESTSCRIPT)\` and try again.";\
echo "Also verify that *all* files in \`tests/test-utils\` have executable permissions before continuing,";\
echo "or future tests could silently break."; exit 1; }

fixtestscriptperms:
	@chmod u+x $(TESTSCRIPT)
	@chmod u+x tests/test-utils/*
	@chmod u+x tests/test-utils/p2a-test/*

.PHONY: clean fixtestscriptperms

##############
# Test Cases #
##############

check: $(SHELLNAME) validtestperms
	@echo "Running all tests..."
	$(TESTSCRIPT) -kv

testcase: $(SHELLNAME) validtestperms
	$(TESTSCRIPT) -vt $(id)

describe: $(SHELLNAME)
	$(TESTSCRIPT) -d $(id)

grade: $(SHELLNAME) validtestperms
	$(TESTSCRIPT) --compute-score
