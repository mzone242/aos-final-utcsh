/*
  utcsh - The UTCS Shell

  <Put your name and CS login ID here>
*/

/* To get getline() */
#define _GNU_SOURCE

/* Read the additional functions from util.h. They may be beneficial to you
in the future */
#include <assert.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "util.h"
/* Global variables */
extern char** environ;

/* The array for holding shell paths. Can be edited by the functions in util.c*/
char shell_paths[MAX_ENTRIES_IN_SHELLPATH][MAX_CHARS_PER_CMDLINE];
static char *default_shell_path[2] = {"/bin", NULL};
/* End Global Variables */

/* Struct Command definitions have been moved to util.h */

/* Here are the functions we recommend you implement */

char **tokenize_command_line(char *cmdline);
struct Command parse_command(char **tokens);
void eval(struct Command *cmd);
int try_exec_builtin(struct Command *cmd);
void exec_external_cmd(struct Command *cmd);

/* Main REPL: read, evaluate, and print. This function should remain relatively
   short: if it grows beyond 60 lines, you're doing too much in main() and
   should try to move some of that work into other functions. */
int main(int argc, char **argv) {
  int is_batch_mode = 0;
  FILE *input;
  if (argc == 1) {
    input = stdin;
  } else if (argc == 2) {
    input = fopen(argv[1], "r");
    if (!input) {
      print_error_msg();
      return 1;
    }
    is_batch_mode = 1;
  } else {
    print_error_msg();
    return 1;
  }

  int commands_run = 0;
  set_shell_path(default_shell_path);

  size_t bufsz = 0;
  char *buf = NULL;
  struct CommandLine cl;

  print_prompt(is_batch_mode);
  while (getline(&buf, &bufsz, input) >= 0) {
    parse_command_line(&cl, buf);

    exec_command_line(&cl);

    free_command_line(&cl);
    buf = NULL;
    ++commands_run;
    print_prompt(is_batch_mode);
  }
  free(buf);

  if (commands_run == 0) {
    print_error_msg();
    return 1;
  }
  return 0;
}

CmdRes_t exec_external_command(struct Command *cmd) {
  assert(cmd->kind == External && "Trying to exec builtin with posix_spawn!");

  // int output_pipe[2];
  // int err = pipe(output_pipe);
  // if (err < 0) {
  //   fprintf(stderr, "Error pipe, errno: %s\n", strerror(err));
  //   exit(1);
  // }
  // int fd = output_pipe[0];
  pid_t child_pid;

  posix_spawn_file_actions_t action;
  int err = posix_spawn_file_actions_init(&action);
  if (err != 0) {
    fprintf(stderr, "Error posix_spawn_file_actions_init, errno: %s\n", strerror(err));
    exit(1);
  }
  // err = posix_spawn_file_actions_addclose(&action, output_pipe[0]);
  // if (err != 0) {
  //   fprintf(stderr, "Error posix_spawn_file_actions_addclose, errno: %s\n", strerror(err));
  //   exit(1);
  // }

  posix_spawnattr_t attr;
  err = posix_spawnattr_init(&attr);
  if (err != 0) {
    fprintf(stderr, "Error posix_spawnattr_init, errno: %s\n", strerror(err));
    exit(1);
  }

  // utcsh does not handle signals

  // Open /dev/null over stdin
  err = posix_spawn_file_actions_addopen(&action, 0, "/dev/null", O_RDONLY, 0);
  if (err != 0) {
    fprintf(stderr, "Error posix_spawn_file_actions_addopen, errno: %s\n", strerror(err));
    exit(1);
  }

  if (cmd->outputFile) {
    int fd = open(cmd->outputFile, O_TRUNC | O_CREAT | O_WRONLY, 0644);
    if (fd == -1) {
      print_error_msg();
    }
    err = posix_spawn_file_actions_adddup2(&action, fd, 1);
    if (err != 0) {
      fprintf(stderr, "Error posix_spawn_file_actions_adddup2, errno: %s\n", strerror(err));
      exit(1);
    }
    err = posix_spawn_file_actions_adddup2(&action, fd, 2);
    if (err != 0) {
      fprintf(stderr, "Error posix_spawn_file_actions_adddup2, errno: %s\n", strerror(err));
      exit(1);
    }
  }

  err = posix_spawn(&child_pid, cmd->exePath, &action, NULL, cmd->arguments, environ);
  if (err != 0) {
    print_error_msg();
    // fprintf(stderr, "Error posix_spawn, errno: %s\n", strerror(err));
    // exit(1);
  }

  return child_pid;

  

  // pid_t cpid = fork();
  // if (cpid == 0) {
  //   if (cmd->outputFile) {
  //     int fd = open(cmd->outputFile, O_TRUNC | O_CREAT | O_WRONLY, 0644);
  //     if (fd == -1) {
  //       print_error_msg();
  //     }
  //     int duperr1 = dup2(fd, 1);
  //     int duperr2 = dup2(fd, 2);

  //     if (duperr1 == -1 || duperr2 == -1) {
  //       print_error_msg();
  //     }
  //   }

  //   execv(cmd->exePath, cmd->arguments);
  //   print_error_msg();
  //   exit(1);
  // } else {
  //   return cpid;
  // }
}

CmdRes_t exec_internal_command(struct Command *cmd) {
  switch (cmd->kind) {
  case Exit:
    return myexit(cmd);
  case Cd:
    return mycd(cmd);
  case Path:
    return mypath(cmd);
  default:
    assert(0 && "Asked to exec unknown internal command!");
  }
}

void exec_command_line(struct CommandLine *cmdLine) {
  pid_t pids[cmdLine->numCmds];
  int i;
  for (i = 0; i < cmdLine->numCmds; ++i) {
    struct Command *cmd = &cmdLine->commands[i];
    if (cmd->kind == External) {
      pids[i] = exec_external_command(cmd);
    } else if (cmd->kind == InvalidCmd) {
      pids[i] = COMMAND_OUTCOME_ERROR;
    } else if (cmd->kind == BlankCmd) {
      pids[i] = COMMAND_OUTCOME_IGNORE;
    } else {
      pids[i] = exec_internal_command(cmd);
    }
  }

  for (i = 0; i < cmdLine->numCmds; ++i) {
    if (pids[i] == COMMAND_OUTCOME_ERROR) {
      print_error_msg();
    } else if (pids[i] == COMMAND_OUTCOME_IGNORE) {
      continue;
    } else {
      int dummy;
      waitpid(pids[i], &dummy, 0);
    }
  }
}
