/**
 * @file semcmd.c
 * @brief Command-line tool offering a simple API for synchronizing processes
 * 
 * 
 * `semcmd` is a powerful yet simple command-line tool for process synchronization
 * that brings the robustness of POSIX semaphores to shell scripting. Whether you're
 * coordinating parallel tasks, protecting critical sections, or implementing
 * synchronization barriers, `semcmd` provides an intuitive interface for managing
 * locks directly from the command line.
 *
 * Built on named POSIX semaphores, `semcmd` enables developers to create, acquire,
 * release, and manage locks with simple commands like `create`, `lock`, `unlock`,
 * and `destroy`. Its automatic namespace isolation ensures that different users and
 * projects never interfere with each other, making it safe for multi-user
 * environments.
 *
 * @copyright This file contains the code for the `semcmd` program, which is
 * released under the GNU General Public License version 3 (GPL-3.0). For more
 * information about the GPL license, please visit:
 * https://www.gnu.org/licenses/gpl-3.0.en.html
 *
 * @author Julien Sopena (@ClaquetteSocket) <github@claquettesocket.fr>
 */

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <glob.h>
#include <limits.h>
#include <pwd.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// Define error codes for different operations
#define USAGE_ERR 1
#define CREATE_ERR 2
#define OPEN_ERR 3
#define CLOSE_ERR 4
#define READ_ERR 5
#define UNLINK_ERR 6
#define LOCK_ERR 7
#define UNLOCK_ERR 8
#define BADVAL_ERR 9
#define SEARCH_ERR 10
#define UNDEF_ERR 11
#define ALLOC_ERR 12
#define LOGIN_ERR 13

#define ALIGNED __attribute__((aligned(64)))

/**
 * @brief The version number of the program.
 */
static const char *VERSION = "1.00";

/*******************************************************************************
 * @defgroup grp_semdir Namespace for named semaphores
 * @brief Functions for managing POSIX named semaphores and creating a
 * namespace.
 *
 * This code relies on the use of POSIX named semaphores from the pthread
 * library. Each semaphore corresponds to a file in a virtual filesystem,
 * normally mounted under `/dev/shm` with names of the form `sem.somename`.
 *
 * To avoid conflicts with other programs or other users, we add a prefix to the
 * semaphore file name. Thus, the semaphore file name is of the form
 * `sem.PREFIX.username.lockname`.
 *
 * @{
 ******************************************************************************/

/*! Macro allowing the directory of named semaphores to be redefined at
 * compilation. */
#ifndef SHM_DIR
#define SHM_DIR "/dev/shm"
#endif

/*! Macro defining the namespace for named semaphores ("semcmd" by default). */
#ifndef GLOBAL_NAMESPACE
#define GLOBAL_NAMESPACE "semcmd"
#endif
static const char *global_namespace = GLOBAL_NAMESPACE;

/** @} */ // end of grp_semdir

/*******************************************************************************
 * @defgroup grp_parsing Commands to perform on the lock
 * @brief Functions for parsing command-line arguments and populating a sync_cmd
 * structure.
 *
 * This part of the code defines a structure to store information about the
 * command to be executed and provides functions to populate it from the
 * command-line arguments. It also provides functions for displaying usage
 * information in case of errors.
 *
 ******************************************************************************/

/**
 * @ingroup grp_parsing
 * @struct sync_cmd
 * @brief Represents a synchronization command.
 *
 * This structure is used to store information about a synchronization command.
 * It contains all the necessary strings to search for and open shared memory
 * files, and to display information and error messages.
 *
 * @var sync_cmd::bin
 * The 'bin' member contains the name of the binary for error messages.
 *
 * @var sync_cmd::force_flag
 * The 'force_flag' member contains the force flag for destroy and clean
 * commands.
 *
 * @var sync_cmd::init_value
 * The 'init_value' member contains the initial value of the semaphore.
 *
 * @var sync_cmd::cmd
 * The 'cmd' member contains the command to execute.
 *
 * @var sync_cmd::shm_file
 * The 'shm_file' member contains the name of the shared memory file.
 *
 * @var sync_cmd::prefix
 * The 'prefix' member contains the prefix for the lock file name.
 *
 * @var sync_cmd::name
 * The 'name' member contains the name of the lock.
 */
struct sync_cmd {
	char *bin; // name of the binary for error messages
	int force_flag; // force flag for destroy and clean
	int init_value; // initial value of the semaphore
	char *cmd; // command to execute
	char *shm_file; // name of the shared memory file
	char *prefix; // prefix for the lock name
	char *name; // name of the lock
} ALIGNED;

#define SHORT_USAGE 0
#define LONG_USAGE 1

/**
 * @ingroup grp_parsing
 * @brief Display the usage of the program.
 *
 * This function displays the usage of the program. It takes a file pointer, the
 * path of the program, an optional message, and a flag indicating whether to
 * display the long version of the usage.
 *
 * @param fp The file pointer to which the usage is written.
 * @param path The path of the program.
 * @param msg An optional message to display.
 * @param long_version A flag indicating whether to display the long version of
 * the usage.
 */
static void usage(FILE *file_ptr, const char *path, const char *msg, int long_version)
{
	const char *basename = strrchr(path, '/');
	basename = basename ? basename + 1 : path;

	if (msg)
		fprintf(file_ptr, "%s\n", msg);

	fprintf(file_ptr,
		"usage: %s [OPTIONS] <clean|list> | <create|lock|unlock|destroy|infos> <lock_name>\n",
		basename);
	if (long_version) {
		fprintf(file_ptr,
			"\nAvailable commands:\n"
			"- 'create' creates a lock with the specified name; by default, the lock is UNLOCKED;\n"
			"- 'lock' acquires the lock with the specified name;\n"
			"- 'unlock' releases the lock with the specified name;\n"
			"- 'destroy' deletes the lock with the specified name (see --force option);\n"
			"- 'infos' displays the status of the lock with the specified name;\n"
			"- 'list' displays the status of all locks you created;\n"
			"- 'clean' destroys all locks you created (see --force option);\n");
		fprintf(file_ptr, "\nAvailable options:\n");
		fprintf(file_ptr, "  -h, --help\tDisplay this help and exit.\n");
		fprintf(file_ptr, "  -V, --version\tDisplay the version number and exit.\n");
		fprintf(file_ptr,
			"  -f, --force\tForce the destruction (destroy/clean) of the lock if in use.\n\t"
			"        Note: behavior is undefined for the process holding the lock.\n");
		fprintf(file_ptr,
			"  -l, --locked\tCreates the lock in the LOCK state to set up a barrier.\n");
	} else {
		fprintf(file_ptr,
			"       A complete description of the command is available with the help option: '%s -h/--help'\n",
			basename);
	}
}

/**
 * @ingroup grp_parsing
 * @brief Parse the command line arguments and populate a sync_cmd structure.
 *
 * This function parses (using the getopt library) the command line arguments
 * and populates a sync_cmd structure with the information needed to execute the
 * command. It will, among other things, create a character string that
 * corresponds to the prefix of all named semaphores of the user (for searching
 * in /dev/shm) and another string that corresponds to the name of the file on
 * which the command will be applied.
 *
 * @param argc The number of command line arguments.
 * @param argv An array of strings containing the command line arguments.
 * @param cmd The sync_cmd structure to populate.
 */
static void args_parse(int argc, char *argv[], struct sync_cmd *cmd)
{
	// default value
	int help_flag = 0;
	int version_flag = 0;
	cmd->force_flag = 0;
	cmd->init_value = 1;

	struct option longopts[] = { { "help", no_argument, &help_flag, 1 },
				     { "version", no_argument, &version_flag, 1 },
				     { "force", no_argument, &cmd->force_flag, 1 },
				     { "locked", no_argument, &cmd->init_value, 0 },
				     { 0 } };

	while (1) {
		int opt = getopt_long(argc, argv, "hVflp:", longopts, 0);

		if (opt == -1) {
			/* a return value of -1 indicates that there are no more
			 * options */
			break;
		}

		switch (opt) {
			case 'h':
				help_flag = 1;
				break;
			case 'V':
				version_flag = 1;
				break;
			case 'f':
				cmd->force_flag = 1;
				break;
			case 'l':
				cmd->init_value = 0;
				break;
			case '?':
				usage(stderr, argv[0], NULL, SHORT_USAGE);
			exit(USAGE_ERR);
			default:
				break;
		}
	}

	if (help_flag) {
		usage(stdout, argv[0], NULL, LONG_USAGE);
		exit(0);
	}

	if (version_flag) {
		fprintf(stdout, "%s - version %s\n", argv[0], VERSION);
		exit(0);
	}

	// Check the number of arguments
	switch (argc - optind) {
		case 1:
			if (!(strcmp(argv[optind], "create") && strcmp(argv[optind], "lock") &&
			      strcmp(argv[optind], "unlock") && strcmp(argv[optind], "destroy") &&
			      strcmp(argv[optind], "infos"))) {
				usage(stderr, argv[0],
				      "The command 'create', 'lock', 'unlock', 'destroy' or 'infos' requires a lock name.",
				      SHORT_USAGE);
				exit(USAGE_ERR);
			}
			break;
		case 2:
			if (!strcmp(argv[optind], "clean") || !strcmp(argv[optind], "list")) {
				usage(stderr, argv[0],
				      "The command 'clean' or 'list' does not require a lock name.",
				      SHORT_USAGE);
				exit(USAGE_ERR);
			}
			break;
		case 0:
			usage(stderr, argv[0], "The command is missing.", SHORT_USAGE);
			exit(USAGE_ERR);
		default:
			usage(stderr, argv[0], "Too many arguments.", SHORT_USAGE);
			exit(USAGE_ERR);
	}

	// Populate the sync_cmd structure
	cmd->bin = argv[0];
	cmd->cmd = argv[optind];
	cmd->name = argv[optind + 1];

	struct passwd *passwd_entry = getpwuid(getuid());
	if (passwd_entry == NULL) {
		perror("getpwuid");
		exit(LOGIN_ERR);
	}
	char *login = passwd_entry->pw_name;
	
	if (getenv("SEMCMD_NAMESPACE") != NULL) {
		char *namespace = getenv("SEMCMD_NAMESPACE");
		int size = snprintf(NULL, 0, "%s.%s.%s", global_namespace, namespace, login);
		cmd->prefix = (char *)malloc(size + 1);
		if (!cmd->prefix) {
			fprintf(stderr, "Memory allocation failed for prefix\n");
			exit(ALLOC_ERR);
		}
		snprintf((char *)cmd->prefix, size + 1, "%s.%s.%s", global_namespace, namespace, login);
	} else {
		int size = snprintf(NULL, 0, "%s.%s", global_namespace, login);
		cmd->prefix = (char *)malloc(size + 1);
		if (!cmd->prefix) {
			fprintf(stderr, "Memory allocation failed for prefix\n");
			exit(ALLOC_ERR);
		}
		snprintf((char *)cmd->prefix, size + 1, "%s.%s", global_namespace, login);
	}

	if (cmd->name != NULL) {

		for (const char *p = cmd->name; *p; p++) {
			char c = *p;
			if (!((c >= 'a' && c <= 'z') ||
				(c >= 'A' && c <= 'Z') ||
				(c >= '0' && c <= '9') ||
				c == '-' || c == '_')) {
				fprintf(stderr, "Error: Invalid character '%c' in lock name '%s'.\n", c, cmd->name);
				fprintf(stderr, "Lock names can only contain letters, digits, hyphens, and underscores.\n");
				exit(USAGE_ERR);
			}
		}

		int size = snprintf(NULL, 0, "%s.%s", cmd->prefix, cmd->name);
		if (size > NAME_MAX) {
			fprintf(stderr,
				"Error: the lock name is too long. With the prefix, it must not exceed %d characters.\n",
				NAME_MAX);
			exit(USAGE_ERR);
		}
		cmd->shm_file = (char *)malloc(size + 1);
		if (!cmd->shm_file) {
			fprintf(stderr, "Memory allocation failed for shm_file\n");
			exit(ALLOC_ERR);
		}
		snprintf(cmd->shm_file, size + 1, "%s.%s", cmd->prefix, cmd->name);
	}
}

/*******************************************************************************
 * @defgroup grp_basic Basic functions for manipulating named semaphores
 * @brief Functions for opening, closing, and reading POSIX named semaphore
 * files.
 *
 * This part of the code provides functions for creating, opening, closing, and
 * reading POSIX named semaphore files. It also contains a function that iterates
 * over all user's semcmd locks.
 *
 * These functions will be used in the following part to implement the different
 * commands offered by `semcmd`.
 *
 ******************************************************************************/

/**
 * @ingroup grp_basic
 * @brief Creating and opening a named semaphore
 *
 * This function creates a named semaphore and opens it for reading and writing.
 * It returns an error if the semaphore already exists.
 *
 * @param cmd The sync_cmd structure containing the name of the semaphore.
 * @return A pointer to the initialized `sem_t` structure.
 */
static sem_t *init_sync(const struct sync_cmd *cmd)
{
	sem_t *my_sem = NULL;
	if ((my_sem = sem_open(cmd->shm_file, O_RDWR | O_CREAT | O_EXCL, 0600, cmd->init_value)) ==
	    NULL) {
		if (errno == EEXIST) {
			fprintf(stderr, "Error to create lock '%s' because it already exists.\n",
				cmd->name);
			fprintf(stderr,
				"Before recreating it, you need to delete it using the command: '%s destroy %s'\n",
				cmd->bin, cmd->name);
		} else {
			fprintf(stderr, "Error in %s %s: ", cmd->cmd, cmd->name);
			perror("sem_open, ");
		}
		exit(CREATE_ERR);
	}
	return my_sem;
}

/**
 * @ingroup grp_basic
 * @brief Opening a named semaphore
 *
 * This function opens a named semaphore for reading and writing. It returns an
 * error message if the semaphore does not exist.
 *
 * @param cmd The sync_cmd structure containing the name of the semaphore.
 * @return A pointer to the `sem_t` structure corresponding to the opened
 * semaphore.
 */
static sem_t *open_sync(const struct sync_cmd *cmd)
{
	sem_t *my_sem = NULL;
	if ((my_sem = sem_open(cmd->shm_file, O_RDWR, 0, 0)) == NULL) {
		if (errno == ENOENT) {
			fprintf(stderr, "Error to %s lock '%s' because it does not exist.\n",
				cmd->cmd, cmd->name);
			fprintf(stderr,
				"Before using it, you need to create it using the command: '%s create %s'\n",
				cmd->bin, cmd->name);
		} else {
			fprintf(stderr, "Error in %s %s: ", cmd->cmd, cmd->name);
			perror("sem_open, ");
		}
		exit(OPEN_ERR);
	}
	return my_sem;
}

/**
 * @ingroup grp_basic
 * @brief Closing a named semaphore
 *
 * This function closes a named semaphore. It returns an error if the semaphore
 * does not exist.
 *
 * @param my_sem A pointer to the `sem_t` structure corresponding to the opened
 * semaphore.
 * @param cmd The sync_cmd structure containing the name of the semaphore.
 */
static void close_sync(sem_t *my_sem, const struct sync_cmd *cmd)
{
	if (sem_close(my_sem) != 0) {
		fprintf(stderr, "Error in %s %s: ", cmd->cmd, cmd->name);
		perror("sem_close, ");
		exit(CLOSE_ERR);
	}
}

/**
 * @ingroup grp_basic
 * @brief Reading the value of a named semaphore
 *
 * This function reads the value of a named semaphore. It returns an error if
 * the semaphore does not exist. It also verifies that the value of the
 * semaphore is not greater than 1. Indeed, by default, semcmd does not allow
 * unlocking a semaphore that is not locked.
 *
 * @param my_sem A pointer to the `sem_t` structure corresponding to the opened
 * semaphore.
 * @param cmd The sync_cmd structure containing the name of the semaphore.
 * @return The value of the semaphore.
 */
static int read_sync(sem_t *my_sem, const struct sync_cmd *cmd)
{
	int current_val;
	if (sem_getvalue(my_sem, &current_val) != 0) {
		fprintf(stderr, "Error reading information from %s: ", cmd->name);
		perror(NULL);
		exit(READ_ERR);
	} else if (current_val > 1) {
		fprintf(stderr,
			"Error in lock value %s, which is equal to %d while it should not be greater than 1\n",
			cmd->name, current_val);
		exit(BADVAL_ERR);
	}
	return current_val;
}

/**
 * @ingroup grp_basic
 * @brief Iterating over all semcmd locks of the user
 *
 * This function iterates over all user's semcmd locks. It takes three functions
 * as arguments: the first is called before the loop, the second for each lock
 * found, and the third if no lock is found.
 *
 * @param first The function to call before the loop. It takes the number of
 * locks found as an argument.
 * @param action The function to call for each lock found. It takes a sync_cmd
 * structure as an argument.
 * @param nothing The function to call if no lock is found.
 * @param cmd The sync_cmd structure containing the prefix of `semcmd` semaphore
 * files.
 */
static void foreach_sync(void (*first)(int num), void (*action)(const struct sync_cmd *),
			 void (*nothing)(), struct sync_cmd *cmd)
{
	glob_t gstruct;

	int size = snprintf(NULL, 0, "%s/sem.%s.*", SHM_DIR, cmd->prefix);
	char *pattern = (char *)malloc(size + 1);
	if (!pattern) {
		fprintf(stderr, "Memory allocation failed for pattern\n");
		exit(ALLOC_ERR);
	}
	snprintf(pattern, size + 1, "%s/sem.%s.*", SHM_DIR, cmd->prefix);

	int glob_result = glob(pattern, GLOB_ERR, NULL, &gstruct);
	if (glob_result != 0 && glob_result != GLOB_NOMATCH) {
		fprintf(stderr, "Error accessing the search in '/dev/shm'\n");
		exit(SEARCH_ERR);
	}

	if (gstruct.gl_pathc) {
		if (first)
			first(gstruct.gl_pathc);
		for (int i = 0; i < (int)gstruct.gl_pathc; i++) {
			cmd->shm_file = gstruct.gl_pathv[i] + sizeof(SHM_DIR) + 4;
			cmd->name = gstruct.gl_pathv[i] + strlen(pattern) - 1;
			action(cmd);
		}
	} else {
		nothing();
	}

	globfree(&gstruct);
	free(pattern);
}

/*******************************************************************************
 * @defgroup grp_cmd Functions implementing all functionalities of semcmd
 * @brief Functions implementing all functionalities of semcmd: create, destroy,
 * lock, unlock, infos, and clean.
 *
 * This set of functions implements all functionalities of semcmd. These
 * functions will be called by `main` after parsing the command line.
 *
 ******************************************************************************/

/**
 * @ingroup grp_cmd
 * @brief Creating a `semcmd` lock.
 *
 * This function creates a `semcmd` lock with the specified name and an initial
 * value given in the sync_cmd structure. If the lock already exists, an error
 * message is displayed.
 *
 * @param cmd The sync_cmd structure containing the name of the semaphore.
 */
static void create(const struct sync_cmd *cmd)
{
	sem_t *my_sem = init_sync(cmd);
	close_sync(my_sem, cmd);
}

/**
 * @ingroup grp_cmd
 * @brief Destroying a `semcmd` lock.
 *
 * This function destroys a `semcmd` lock with the specified name in the
 * `sync_cmd` structure. If the lock is currently in use, an error message is
 * displayed. If the
 * `--force` option is used, the lock is destroyed even if in use.
 *
 * @param cmd The sync_cmd structure containing the name of the semaphore.
 * @return 0 if the lock is destroyed, 1 if the lock is in use.
 */
static int destroy(const struct sync_cmd *cmd)
{
	sem_t *my_sem = open_sync(cmd);
	int current_value = read_sync(my_sem, cmd);
	close_sync(my_sem, cmd);

	if (current_value < 1 && !cmd->force_flag) {
		fprintf(stderr, "Unable to delete the lock '%s' as it is currently in use.\n",
			cmd->name);
		fprintf(stderr, "To resolve the issue, you can:\n");
		fprintf(stderr, "\t- release the lock with the command: '%s unlock %s'\n", cmd->bin,
			cmd->name);
		fprintf(stderr,
			"\t- use destroy with the --force/-f option: '%s destroy %s --force'\n",
			cmd->bin, cmd->name);
		fprintf(stderr, "\t- use clean with the --force/-f option: '%s clean --force'\n",
			cmd->bin);
		return 1;
	}

	if (sem_unlink(cmd->shm_file) != 0) {
		fprintf(stderr, "Error deleting lock '%s'", cmd->name);
		if (errno == ENOENT)
			fprintf(stderr, "because it does not exist.\n");
		else
			perror(": sem_unlink, ");
		exit(UNLINK_ERR);
	}
	return 0;
}

/**
 * @ingroup grp_cmd
 * @brief Locking a `semcmd` lock.
 *
 * This function locks a `semcmd` lock with the specified name in the `sync_cmd`
 * structure. If the lock is already locked, the process is blocked until the
 * lock is released.
 *
 * @param cmd The sync_cmd structure containing the name of the semaphore.
 */
static void lock(const struct sync_cmd *cmd)
{
	sem_t *my_sem = open_sync(cmd);

	if (sem_wait(my_sem) != 0) {
		fprintf(stderr, "Error to lock %s: ", cmd->name);
		perror(NULL);
		exit(LOCK_ERR);
	}

	close_sync(my_sem, cmd);
}

/**
 * @ingroup grp_cmd
 * @brief Unlocking a `semcmd` lock.
 *
 * This function unlocks a `semcmd` lock with the specified name in the
 * `sync_cmd` structure. If the lock is not locked, an error message is
 * displayed.
 *
 * @param cmd The sync_cmd structure containing the name of the semaphore.
 */
static void unlock(const struct sync_cmd *cmd)
{
	sem_t *my_sem = open_sync(cmd);

	if (read_sync(my_sem, cmd) > 0) {
		fprintf(stderr, "Error to unlock %s because it is not locked.\n", cmd->name);
		exit(UNLOCK_ERR);
	}

	if (sem_post(my_sem) != 0) {
		fprintf(stderr, "Error to unlock %s: ", cmd->name);
		perror(NULL);
		exit(UNLOCK_ERR);
	}

	close_sync(my_sem, cmd);
}

/**
 * @ingroup grp_cmd
 * @brief Display the status of a `semcmd` lock.
 *
 * This function displays the status of a `semcmd` lock with the specified name
 * in the `sync_cmd` structure. It displays whether the lock is locked or
 * unlocked.
 *
 * Unfortunately, due to the lack of a standard implementation and the choice of
 * Linux implementation, this function is not able to display the number of
 * processes waiting for the lock. We are working to solve this issue.
 *
 * @param cmd The sync_cmd structure containing the name of the semaphore.
 * @param indent An optional string to indent the output.
 */
static void infos(const struct sync_cmd *cmd, const char *indent)
{
	sem_t *my_sem = open_sync(cmd);
	int value = read_sync(my_sem, cmd);

	fprintf(stdout, "%s%s lock is %s", indent ? indent : "", cmd->name,
		value > 0 ? "UNLOCK" : "LOCK");
	// :-( Linux implements the simple version of the standard: returns 0
	// even if processes are waiting if (value == 0)
	//	fprintf(stdout, " and no process is waiting for it.\n");
	// else if (value < 0)
	//	fprintf(stdout, " and is being waited for by %d processes.\n",
	// value * -1);
	// else
	fprintf(stdout, ".\n");

	close_sync(my_sem, cmd);
}

static void clean_action(const struct sync_cmd *cmd)
{
	if (!destroy(cmd))
		fprintf(stdout, "Lock %s has been deleted.\n", cmd->name);
}
static void clean_nothing()
{
	fprintf(stdout, "You currently have no locks to delete in the system.\n");
}

/**
 * @ingroup grp_cmd
 * @brief Destroy all `semcmd` locks created by the user.
 *
 * This function destroys all `semcmd` locks created by the user. If a lock is
 * currently in use, an error message is displayed. If the `--force` option is
 * used, the lock is destroyed even if it is in use.
 *
 * @param cmd The sync_cmd structure containing the prefix to identify `semcmd`
 * semaphore files.
 */
static void clean(struct sync_cmd *cmd)
{
	foreach_sync(NULL, clean_action, clean_nothing, cmd);
}

static void list_first(int num)
{
	fprintf(stdout, "In this system, you have created %d lock%s:\n", num, num >= 2 ? "s" : "");
}
static void list_action(const struct sync_cmd *cmd)
{
	infos(cmd, "  - ");
}
static void list_nothing()
{
	fprintf(stdout, "Currently, you have no locks in the system.\n");
}

/**
 * @ingroup grp_cmd
 * @brief Display the status of all `semcmd` locks created by the user.
 *
 * This function displays the status of all `semcmd` locks created by the user.
 * It displays whether the lock is locked or unlocked.
 *
 * @param cmd The sync_cmd structure containing the prefix to identify `semcmd`
 * semaphore files.
 */
static void list(struct sync_cmd *cmd)
{
	foreach_sync(list_first, list_action, list_nothing, cmd);
}

/**
 * @brief The main function of the program.
 *
 * This function is the entry point of the program. It starts by parsing the
 * command line arguments, then calls the corresponding function based on the
 * command passed as an argument.
 *
 * @param argc The number of command line arguments.
 * @param argv An array of strings containing the command line arguments.
 *
 * @return The exit status of the program.
 */
int main(int argc, char **argv)
{
	struct sync_cmd user_cmd;
	args_parse(argc, argv, &user_cmd);

	if (!strcmp(user_cmd.cmd, "create"))
		create(&user_cmd);
	else if (!strcmp(user_cmd.cmd, "lock"))
		lock(&user_cmd);
	else if (!strcmp(user_cmd.cmd, "unlock"))
		unlock(&user_cmd);
	else if (!strcmp(user_cmd.cmd, "infos"))
		infos(&user_cmd, NULL);
	else if (!strcmp(user_cmd.cmd, "destroy"))
		destroy(&user_cmd);
	else if (!strcmp(user_cmd.cmd, "list"))
		list(&user_cmd);
	else if (!strcmp(user_cmd.cmd, "clean"))
		clean(&user_cmd);
	else {
		usage(stderr, argv[0], "The command is not valid.", SHORT_USAGE);
		return USAGE_ERR;
	}

	return 0;
}

/* vim: set tabstop=4 shiftwidth=4 noexpandtab: */

