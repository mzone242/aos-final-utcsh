#define _DEFAULT_SOURCE
#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "util.h"

#define STR_EQ(x, y) !strcmp(x, y)

extern char shell_paths[MAX_ENTRIES_IN_SHELLPATH][MAX_CHARS_PER_CMDLINE];

/* Should the UTCSH internal functions dump verbose output? */
static int utcsh_internal_verbose = 0;

void maybe_print_error() {
  if (utcsh_internal_verbose) {
    char *err = strerror(errno);
    printf("[UTCSH INTERNAL ERROR]: %s\n", err);
  }
}

static void clear_shell_paths() {
  int i;
  for (i = 0; i < MAX_ENTRIES_IN_SHELLPATH; ++i) {
    memset(shell_paths[i], 0, MAX_CHARS_PER_CMDLINE * sizeof(char));
  }
}

int set_shell_path(char **newPaths) {
  if (!newPaths) {
    return 0; /* Assume this was an error. To clear the shell paths, pass in a
                 pointer-to-nullpointer, not a nullpointer directly. */
  }
  clear_shell_paths();
  int i;
  for (i = 0; i < MAX_ENTRIES_IN_SHELLPATH && newPaths[i]; ++i) {
    if (strlen(newPaths[i]) + 1 > MAX_CHARS_PER_CMDLINE) {
      return 0; /* This path is too long. */
    }
    strcpy(shell_paths[i], newPaths[i]);
  }
  return 1;
}

int is_absolute_path(const char *path) {
  if (!path) {
    return 0;
  }
  return *path == '/';
}

/* Join dirname and basename with a '/' in the caller-provided buffer `buf`.
   We use a caller-provided buffer here so that callers can use either stack
   or heap allocated arrays to view the result (depending on the needs) */
static void joinpath(const char *dirname, const char *basename, char *buf) {
  assert(dirname && "Got NULL directory name in joinpath.");
  assert(basename && "Got NULL filename in joinpath.");
  assert(buf && "Got NULL output in joinpath");
  size_t dlen = strlen(dirname);

  strcpy(buf, dirname);
  strcpy(buf + dlen + 1, basename);
  buf[dlen] = '/';
}

void Closedir(DIR *dirp) {
  if (closedir(dirp) == -1 && utcsh_internal_verbose) {
    printf("[UTCSH_INTERNAL]: Error closing directory.\n");
  }
}

char *exe_exists_in_dir(const char *dirname, const char *filename) {
  if (!dirname || !filename) {
    return NULL;
  }
  DIR *dir;
  struct dirent *dent;
  dir = opendir(dirname);
  if (!dir) {
    maybe_print_error();
    return NULL;
  }

  errno = 0; /* To distinguish EOS from error, see man 3 readdir */
  while ((dent = readdir(dir))) {
    if (STR_EQ(dent->d_name, filename)) {
      size_t buflen = strlen(dirname) + strlen(filename) + 2;
      char *buf = malloc(buflen * sizeof(char));
      if (!buf) {
        maybe_print_error();
        return NULL;
      }
      joinpath(dirname, filename, buf);
      int exec_forbidden = access(buf, X_OK);
      if (!exec_forbidden) {
        Closedir(dir);
        return buf;
      } else {
        switch (errno) {
        case EACCES:
        case ENOENT:
        case ENOTDIR:
          errno = 0;
          break; /* These are benign faults */
        default:
          maybe_print_error(); /* User might want to know about these */
          errno = 0;
        }
        free(buf);
      }
    }
  }
  /* We have exited the loop. Why? If errno is nonzero, it's an error. */
  if (errno == EBADF) {
    maybe_print_error();
  }
  Closedir(dir);
  return NULL;
}

void print_error_msg() {
  char error_message[23] = "An error has occurred\n";
  int nchars_out = write(STDERR_FILENO, error_message, strlen(error_message));
  if (nchars_out != 22) {
    exit(2);
  }
}

/*********************
 * PARSING FUNCTIONS *
 *********************/

void skip_space(char **ptr) {
  while (isspace(**ptr)) {
    (*ptr)++;
  }
}

int is_consumed(char *buf) {
  if (!buf) {
    return 1;
  }
  skip_space(&buf);
  if (STR_EQ(buf, "")) {
    return 1;
  }
  return 0;
}

void init_command(struct Command *cmd, char *cmdBuf) {
  cmd->kind = Unknown;
  cmd->exePath = NULL;
  cmd->outputFile = NULL;

  int maxNumArgs = 64;
  cmd->arguments = malloc(sizeof(char *) * maxNumArgs);
  if(!cmd->arguments){
      exit(2);
  }

  int currentArg = 0;

  char *argBuf = strsep(&cmdBuf, ">");
  char *redirBuf = cmdBuf;

  while (!is_consumed(argBuf)) {
    skip_space(&argBuf);
    if(currentArg >= maxNumArgs){
        cmd->kind = InvalidCmd;
        break;
    }
    cmd->arguments[currentArg] = strsep(&argBuf, " \t\n");
    currentArg++;
  }

  if (redirBuf) {
    if (is_consumed(redirBuf)) {
      cmd->kind = InvalidCmd;
    }
    skip_space(&redirBuf);
    cmd->outputFile = strsep(&redirBuf, " \t\n");
    if (!is_consumed(redirBuf)) {
      cmd->kind = InvalidCmd;
    }
  }

  if (currentArg == 0 && !cmd->outputFile) {
    cmd->kind = BlankCmd;
  }

  if (currentArg == 0 && cmd->outputFile) {
    cmd->kind = InvalidCmd;
  }

  cmd->numArgs = currentArg;
  cmd->arguments[cmd->numArgs] = NULL;
  if (cmd->kind == InvalidCmd || cmd->kind == BlankCmd) {
    return;
  } else if (STR_EQ(cmd->arguments[0], "exit")) {
    cmd->kind = Exit;
  } else if (STR_EQ(cmd->arguments[0], "cd")) {
    cmd->kind = Cd;
  } else if (STR_EQ(cmd->arguments[0], "path")) {
    cmd->kind = Path;
  } else {
    cmd->kind = External;
    cmd->exePath = get_path_to_cmd(cmd->arguments[0]);
    if (!cmd->exePath) {
      cmd->kind = InvalidCmd;
    }
  }

  assert(cmd->kind != Unknown);
}

void parse_command_line(struct CommandLine *cmdLine, char *cmdBuf) {
  int numCommands = 1;
  char *p;
  for (p = cmdBuf; *p; ++p) {
    if (*p == '&') {
      ++numCommands;
    }
  }

  char *cmdbfs[numCommands];
  char *tokPtr = cmdBuf;
  int i;
  for (i = 0; i < numCommands; i++) {
    cmdbfs[i] = strsep(&tokPtr, "&");
  }

  struct Command *cmds = malloc(sizeof(struct Command) * numCommands);
  if(cmds == NULL){
      exit(2);
  }
  for (i = 0; i < numCommands; ++i) {
    init_command(cmds + i, cmdbfs[i]);
  }

  cmdLine->commands = cmds;
  cmdLine->commandBuffer = cmdBuf;
  cmdLine->numCmds = numCommands;
}

void free_command(struct Command *cmd) {
  free(cmd->arguments);
  cmd->arguments = NULL;
  if (cmd->kind == External) {
    free(cmd->exePath);
    cmd->exePath = NULL;
  }
  cmd->outputFile = NULL;
}

void free_command_line(struct CommandLine *cmdLine) {
  int i;
  for (i = 0; i < cmdLine->numCmds; ++i) {
    free_command(&cmdLine->commands[i]);
  }
  free(cmdLine->commands);
  free(cmdLine->commandBuffer);
}

CmdRes_t mycd(struct Command *cmd) {
  if (cmd->numArgs != 2) {
    return COMMAND_OUTCOME_ERROR;
  }
  if (chdir(cmd->arguments[1]) == -1) {
    return COMMAND_OUTCOME_ERROR;
  }
  return COMMAND_OUTCOME_IGNORE;
}

CmdRes_t myexit(struct Command *cmd) {
  if (cmd->numArgs != 1) {
    return COMMAND_OUTCOME_ERROR;
  }
  exit(0);
}

CmdRes_t mypath(struct Command *cmd) {
  set_shell_path(cmd->arguments);
  return COMMAND_OUTCOME_IGNORE;
}

char *get_path_to_cmd(const char *filename) {
  if (!filename) {
    return NULL;
  }

  char *fqPath = NULL;
  if (is_absolute_path(filename)) {
    fqPath = malloc(strlen(filename) + 1);
    if(!fqPath){
      exit(2);
    }
    strcpy(fqPath, filename);
    return fqPath;
  }
  int i = 0;
  while (strlen(shell_paths[i]) && i < MAX_PATH_DIRS) {
    if ((fqPath = exe_exists_in_dir(shell_paths[i], filename))) {
      break;
    }
    ++i;
  }

  return fqPath;
}

void print_prompt(int is_batch_mode) {
  if (!is_batch_mode) {
    printf("utcsh> ");
  }
}
