#define MAX_CHARS_PER_CMDLINE 2048
#define MAX_ARGS_IN_CMDLINE 256
#define MAX_DIRS_IN_PATH 256
#define MAX_ENTRIES_IN_SHELLPATH 100

#include <sys/types.h>

/** Modify the shell_paths global. Will run until all MAX_ENTRIES_IN_SHELLPATH elements
 * have been set or a NULL char* is found in newPaths. Returns an integer
 * indicating whether the operation was successful or not. */
int set_shell_path(char **newPaths);

/** Returns 1 if this is an absolute path, 0 otherwise */
int is_absolute_path(const char *path);

/** Determines whether an executable file with the name `filename` exists in the
 * directory named `dirname`.
 *
 * If so, returns a char* with the full path to the file. This pointer MUST
 * be freed by the calling function.
 *
 * If no such file exists in the directory, or if the file exists but is not
 * executable, this function returns NULL. */
char *exe_exists_in_dir(const char *dirname, const char *filename);

void print_error_msg();

enum CommandKind { Unknown, InvalidCmd, BlankCmd, External, Exit, Cd, Path };

/* A Command is a single action to take. Commands must exist as part of some
CommandLine structure, since they need to point into the char* commandBuffer.
Initialized Commands should have the following guarantees:
- `kind` is not Unknown
- If `kind` is *not* InvalidCmd, then:
  + numArgs >= 1
  + arguments is not null and has numArgs + 1 initialized entries, with the last
    one being a NULL pointer terminator.
  + exePath is not NULL and refers to a valid executable program

These guarantees should ensure that the command is ready to execute as-is with
the exception of the outputFile (which the user may not have write permissions
for). */

struct Command {
  enum CommandKind kind;
  int numArgs;
  char **arguments;
  char *exePath;
  char *outputFile;
};

struct CommandLine {
  int numCmds;
  struct Command *commands;
  char *commandBuffer;
};

typedef pid_t CmdRes_t;
#define COMMAND_OUTCOME_ERROR -1
#define COMMAND_OUTCOME_IGNORE -2

#define MAX_PATH_DIRS 256
#define STR_EQ(x, y) !strcmp(x, y)

void init_command_line(struct CommandLine *cmd);
void free_command_line(struct CommandLine *cmd);
void dump_command_line(struct CommandLine *cmd);
void parse_command_line(struct CommandLine *cmdLine, char *cmdBuf);
void exec_command_line(struct CommandLine *cmdLine);

CmdRes_t exec_external_command(struct Command *cmd);
CmdRes_t exec_internal_command(struct Command *cmd);
void exec_command_line(struct CommandLine *cmdLine);
char *get_path_to_cmd(const char *filename);

CmdRes_t mycd(struct Command *cmd);
CmdRes_t myexit(struct Command *cmd);
CmdRes_t mypath(struct Command *cmd);
void print_prompt(int is_batch_mode);
