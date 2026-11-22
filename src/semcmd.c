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
 * `sem.GLOBAL_NAMESPACE.uid.namespace.lockname`.
 *
 * The namespace is always present ("default" if none is given), and neither the
 * uid, the namespace, nor the lock name can contain a dot. Hence each file name
 * corresponds to exactly one (uid, namespace, lock name) triple.
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

/*! Maximum length of a semaphore name: glibc prepends "sem." to build the file
 * name in SHM_DIR, which must not exceed NAME_MAX. */
#define SEM_NAME_MAX (NAME_MAX - 4)

/*! Namespace used when SEMCMD_NAMESPACE is unset or empty. */
#define DEFAULT_NAMESPACE "default"

/**
 * @ingroup grp_semdir
 * @brief Get the namespace requested by the user.
 *
 * @return The value of SEMCMD_NAMESPACE, or NULL if it is unset or empty.
 */
static const char *user_namespace(void)
{
	const char *namespace = getenv("SEMCMD_NAMESPACE");
	return (namespace && *namespace) ? namespace : NULL;
}

/**
 * @ingroup grp_semdir
 * @brief Get the namespace of the locks.
 *
 * @return The namespace requested by the user, or DEFAULT_NAMESPACE if none.
 */
static const char *get_namespace(void)
{
	const char *namespace = user_namespace();
	return namespace ? namespace : DEFAULT_NAMESPACE;
}

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
 *
 * @var sync_cmd::keep_going
 * The 'keep_going' member tells the basic functions to return errors instead
 * of exiting, so that list and clean can go on with the other locks.
 */
struct sync_cmd {
	char *bin; // name of the binary for error messages
	int force_flag; // force flag for destroy and clean
	int init_value; // initial value of the semaphore
	char *cmd; // command to execute
	char *shm_file; // name of the shared memory file
	char *prefix; // prefix for the lock name
	char *name; // name of the lock
	int keep_going; // return errors instead of exiting (list and clean)
};

#define SHORT_USAGE 0
#define LONG_USAGE 1

/**
 * @ingroup grp_parsing
 * @brief Get the name of the program from its path.
 *
 * @param path The path of the program (argv[0]).
 * @return The part of the path after the last '/'.
 */
static const char *prog_name(const char *path)
{
	const char *slash = strrchr(path, '/');
	return slash ? slash + 1 : path;
}

/**
 * @ingroup grp_parsing
 * @brief Display the usage of the program.
 *
 * This function displays the usage of the program. It takes a file pointer, the
 * path of the program, an optional message, and a flag indicating whether to
 * display the long version of the usage.
 *
 * @param file_ptr The file pointer to which the usage is written.
 * @param path The path of the program.
 * @param msg An optional message to display.
 * @param long_version A flag indicating whether to display the long version of
 * the usage.
 */
static void usage(FILE *file_ptr, const char *path, const char *msg, int long_version)
{
	const char *basename = prog_name(path);

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
			"- 'list' displays the status of all your locks;\n"
			"- 'clean' destroys all your locks (see --force option);\n"
			"If SEMCMD_NAMESPACE is set, commands only apply to the locks of this namespace.\n");
		fprintf(file_ptr, "\nAvailable options:\n");
		fprintf(file_ptr, "  -h, --help\tDisplay this help and exit.\n");
		fprintf(file_ptr, "  -V, --version\tDisplay the version number and exit.\n");
		fprintf(file_ptr,
			"  -f, --force\tForce the destruction (destroy/clean) of the lock if in use.\n\t"
			"        Note: behavior is undefined for the process holding the lock.\n");
		fprintf(file_ptr,
			"  -l, --locked\tCreates the lock in the LOCKED state to set up a barrier.\n");

		unsigned long uid = (unsigned long)getuid();
		struct passwd *passwd_entry = getpwuid(getuid());
		fprintf(file_ptr, "\nEach lock is a POSIX named semaphore stored as:\n");
		fprintf(file_ptr, "  %s/sem.%s.<uid>.<namespace>.<lock_name>\n", SHM_DIR, global_namespace);
		fprintf(file_ptr, "where <uid> is your user ID (%lu", uid);
		if (passwd_entry)
			fprintf(file_ptr, " for '%s'", passwd_entry->pw_name);
		fprintf(file_ptr, ", see cmd 'id'), e.g.:\n");
		fprintf(file_ptr, "  %s/sem.%s.%lu.%s.mylock\n", SHM_DIR, global_namespace, uid,
			get_namespace());
	} else {
		fprintf(file_ptr,
			"       A complete description of the command is available with the help option: '%s -h/--help'\n",
			basename);
	}
}

/**
 * @ingroup grp_parsing
 * @brief Check that a lock name or a namespace is valid.
 *
 * Lock names and namespaces are dot-separated components of the semaphore file
 * name. To keep these names unambiguous and free of glob metacharacters, they
 * must not be empty and can only contain letters, digits, hyphens, and
 * underscores. On error, a message is displayed and the program exits.
 *
 * @param name The name to check.
 * @param what A description of the name for error messages.
 */
static void check_name(const char *name, const char *what)
{
	if (*name == '\0') {
		fprintf(stderr, "Error: the %s must not be empty.\n", what);
		exit(USAGE_ERR);
	}

	for (const char *p = name; *p; p++) {
		char c = *p;
		if (!((c >= 'a' && c <= 'z') ||
			(c >= 'A' && c <= 'Z') ||
			(c >= '0' && c <= '9') ||
			c == '-' || c == '_')) {
			fprintf(stderr, "Error: invalid character '%c' in %s '%s'.\n", c, what, name);
			fprintf(stderr, "A %s can only contain letters, digits, hyphens, and underscores.\n", what);
			exit(USAGE_ERR);
		}
	}
}

/**
 * @ingroup grp_parsing
 * @brief Check that a string is a command of semcmd.
 *
 * @param name The string to check.
 * @return 1 if it is a command, 0 otherwise.
 */
static int is_command(const char *name)
{
	static const char *const commands[] = { "create", "lock", "unlock", "destroy",
						 "infos",  "list", "clean" };

	for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++)
		if (!strcmp(name, commands[i]))
			return 1;
	return 0;
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
	cmd->keep_going = 0;

	struct option longopts[] = { { "help", no_argument, &help_flag, 1 },
				     { "version", no_argument, &version_flag, 1 },
				     { "force", no_argument, &cmd->force_flag, 1 },
				     { "locked", no_argument, &cmd->init_value, 0 },
				     { 0 } };

	while (1) {
		int opt = getopt_long(argc, argv, "hVfl", longopts, 0);

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
		fprintf(stdout, "%s - version %s\n", prog_name(argv[0]), VERSION);
		exit(0);
	}

	// Check the command, before its arguments and options
	if (argc - optind == 0) {
		usage(stderr, argv[0], "The command is missing.", SHORT_USAGE);
		exit(USAGE_ERR);
	}
	if (!is_command(argv[optind])) {
		usage(stderr, argv[0], "The command is not valid.", SHORT_USAGE);
		exit(USAGE_ERR);
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
		default:
			usage(stderr, argv[0], "Too many arguments.", SHORT_USAGE);
			exit(USAGE_ERR);
	}

	// Populate the sync_cmd structure
	cmd->bin = argv[0];
	cmd->cmd = argv[optind];
	cmd->name = argv[optind + 1];

	// Check that the options apply to the command
	if (!cmd->init_value && strcmp(cmd->cmd, "create")) {
		usage(stderr, argv[0], "The option --locked/-l only applies to the command 'create'.",
		      SHORT_USAGE);
		exit(USAGE_ERR);
	}
	if (cmd->force_flag && strcmp(cmd->cmd, "destroy") && strcmp(cmd->cmd, "clean")) {
		usage(stderr, argv[0],
		      "The option --force/-f only applies to the commands 'destroy' and 'clean'.",
		      SHORT_USAGE);
		exit(USAGE_ERR);
	}

	unsigned long uid = (unsigned long)getuid();
	const char *namespace = get_namespace();
	check_name(namespace, "namespace (SEMCMD_NAMESPACE)");

	int size = snprintf(NULL, 0, "%s.%lu.%s", global_namespace, uid, namespace);
	// The prefix must leave room for a dot and a one-character lock name
	if (size + 2 > SEM_NAME_MAX) {
		fprintf(stderr,
			"Error: the namespace (SEMCMD_NAMESPACE) is too long. It must not exceed %d characters.\n",
			SEM_NAME_MAX - 2 - (size - (int)strlen(namespace)));
		exit(USAGE_ERR);
	}
	cmd->prefix = (char *)malloc(size + 1);
	if (!cmd->prefix) {
		fprintf(stderr, "Error: out of memory.\n");
		exit(ALLOC_ERR);
	}
	snprintf(cmd->prefix, size + 1, "%s.%lu.%s", global_namespace, uid, namespace);

	if (cmd->name != NULL) {
		check_name(cmd->name, "lock name");

		size = snprintf(NULL, 0, "%s.%s", cmd->prefix, cmd->name);
		if (size > SEM_NAME_MAX) {
			fprintf(stderr,
				"Error: the lock name is too long. It must not exceed %d characters.\n",
				SEM_NAME_MAX - 1 - (int)strlen(cmd->prefix));
			exit(USAGE_ERR);
		}
		cmd->shm_file = (char *)malloc(size + 1);
		if (!cmd->shm_file) {
			fprintf(stderr, "Error: out of memory.\n");
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
 * On failure, these functions display an error message and exit with a
 * specific code, except in list and clean (see fail()).
 *
 ******************************************************************************/

/**
 * @ingroup grp_basic
 * @brief Handling the failure of an operation on a lock
 *
 * For a command on a single lock, the program exits with the given code. For
 * list and clean, which loop over all locks (keep_going is set), the code is
 * returned so that the loop can go on with the other locks.
 *
 * @param cmd The sync_cmd structure of the command.
 * @param code The error code.
 * @return The error code (only if keep_going is set).
 */
static int fail(const struct sync_cmd *cmd, int code)
{
	if (!cmd->keep_going)
		exit(code);
	return code;
}

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
	    SEM_FAILED) {
		if (errno == EEXIST) {
			fprintf(stderr, "Error: cannot create lock '%s': it already exists.\n",
				cmd->name);
			fprintf(stderr, "To recreate it, first destroy it with: '%s destroy %s'\n",
				cmd->bin, cmd->name);
		} else {
			fprintf(stderr, "Error: cannot create lock '%s': %s.\n", cmd->name,
				strerror(errno));
		}
		exit(CREATE_ERR);
	}
	return my_sem;
}

/**
 * @ingroup grp_basic
 * @brief Checking whether a semaphore file belongs to another user
 *
 * Since SHM_DIR is writable by everyone, another user may create a file with
 * the name of one of your locks: such a file must not be used.
 *
 * @param path The path of the semaphore file.
 * @param uid Filled with the owner of the file if it belongs to another user.
 * @return 1 if the file belongs to another user, 0 otherwise (including if it
 * does not exist).
 */
static int is_foreign(const char *path, unsigned long *uid)
{
	struct stat st;

	if (stat(path, &st) != 0 || st.st_uid == getuid())
		return 0;
	*uid = (unsigned long)st.st_uid;
	return 1;
}

/**
 * @ingroup grp_basic
 * @brief Opening a named semaphore
 *
 * This function opens a named semaphore for reading and writing. It returns an
 * error message if the semaphore does not exist.
 *
 * A file that belongs to another user is refused (see is_foreign()).
 *
 * @param cmd The sync_cmd structure containing the name of the semaphore.
 * @return A pointer to the `sem_t` structure corresponding to the opened
 * semaphore, or SEM_FAILED on error (only if keep_going is set).
 */
static sem_t *open_sync(const struct sync_cmd *cmd)
{
	char path[sizeof(SHM_DIR "/sem.") + SEM_NAME_MAX];
	unsigned long uid;

	snprintf(path, sizeof(path), "%s/sem.%s", SHM_DIR, cmd->shm_file);
	if (is_foreign(path, &uid)) {
		fprintf(stderr, "Error: lock '%s' belongs to another user (uid %lu): see '%s'.\n",
			cmd->name, uid, path);
		fail(cmd, OPEN_ERR);
		return SEM_FAILED;
	}

	sem_t *my_sem = NULL;
	if ((my_sem = sem_open(cmd->shm_file, O_RDWR, 0, 0)) == SEM_FAILED) {
		if (errno == ENOENT) {
			fprintf(stderr, "Error: lock '%s' does not exist.\n", cmd->name);
			fprintf(stderr, "To use it, first create it with: '%s create %s'\n",
				cmd->bin, cmd->name);
		} else {
			fprintf(stderr, "Error: cannot open lock '%s': %s.\n", cmd->name,
				strerror(errno));
		}
		fail(cmd, OPEN_ERR);
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
 * @return 0 on success, an error code otherwise (only if keep_going is set).
 */
static int close_sync(sem_t *my_sem, const struct sync_cmd *cmd)
{
	if (sem_close(my_sem) != 0) {
		fprintf(stderr, "Error: cannot close lock '%s': %s.\n", cmd->name, strerror(errno));
		return fail(cmd, CLOSE_ERR);
	}
	return 0;
}

/**
 * @ingroup grp_basic
 * @brief Reading the value of a named semaphore
 *
 * This function reads the value of a named semaphore. It also verifies that the
 * value of the semaphore is not greater than 1. Indeed, by default, semcmd does
 * not allow unlocking a semaphore that is not locked.
 *
 * @param my_sem A pointer to the `sem_t` structure corresponding to the opened
 * semaphore.
 * @param cmd The sync_cmd structure containing the name of the semaphore.
 * @param value Filled with the value of the semaphore.
 * @return 0 on success, an error code otherwise (only if keep_going is set).
 */
static int read_sync(sem_t *my_sem, const struct sync_cmd *cmd, int *value)
{
	if (sem_getvalue(my_sem, value) != 0) {
		fprintf(stderr, "Error: cannot read the value of lock '%s': %s.\n", cmd->name,
			strerror(errno));
		return fail(cmd, READ_ERR);
	} else if (*value > 1) {
		fprintf(stderr,
			"Error: lock '%s' is corrupted: its value is %d, while it should be 0 or 1.\n",
			cmd->name, *value);
		fprintf(stderr, "To remove it, use: '%s destroy --force %s'\n", cmd->bin, cmd->name);
		return fail(cmd, BADVAL_ERR);
	}
	return 0;
}

/**
 * @ingroup grp_basic
 * @brief Iterating over all semcmd locks of the user
 *
 * This function iterates over all user's semcmd locks. It takes three functions
 * as arguments: the first is called before the loop, the second for each lock
 * found, and the third if no lock is found.
 *
 * Files that belong to other users are ignored with a warning (see
 * is_foreign()), so that they are neither counted nor used. An error on a lock
 * does not stop the loop (see fail()): the error is displayed, and the loop
 * goes on with the other locks.
 *
 * @param first The function to call before the loop. It takes the number of
 * locks found as an argument.
 * @param action The function to call for each lock found. It takes a sync_cmd
 * structure as an argument and returns 0 on success, an error code otherwise.
 * @param nothing The function to call if no lock is found.
 * @param cmd The sync_cmd structure containing the prefix of `semcmd` semaphore
 * files.
 * @return 0 if the action succeeded for all locks, the error code of the last
 * failure otherwise.
 */
static int foreach_sync(void (*first)(int num), int (*action)(const struct sync_cmd *),
			void (*nothing)(void), struct sync_cmd *cmd)
{
	int result = 0;
	glob_t gstruct;

	cmd->keep_going = 1;

	int size = snprintf(NULL, 0, "%s/sem.%s.*", SHM_DIR, cmd->prefix);
	char *pattern = (char *)malloc(size + 1);
	if (!pattern) {
		fprintf(stderr, "Error: out of memory.\n");
		exit(ALLOC_ERR);
	}
	snprintf(pattern, size + 1, "%s/sem.%s.*", SHM_DIR, cmd->prefix);

	int glob_result = glob(pattern, GLOB_ERR, NULL, &gstruct);
	if (glob_result != 0 && glob_result != GLOB_NOMATCH) {
		fprintf(stderr, "Error: cannot search for locks in '%s'.\n", SHM_DIR);
		exit(SEARCH_ERR);
	}

	// Ignore the files of other users, so that they are neither counted nor used
	char **paths = malloc((gstruct.gl_pathc + 1) * sizeof(*paths));
	if (!paths) {
		fprintf(stderr, "Error: out of memory.\n");
		exit(ALLOC_ERR);
	}
	int num = 0;
	for (size_t i = 0; i < gstruct.gl_pathc; i++) {
		unsigned long uid;
		if (is_foreign(gstruct.gl_pathv[i], &uid))
			fprintf(stderr, "Warning: ignoring '%s', which belongs to another user (uid %lu).\n",
				gstruct.gl_pathv[i], uid);
		else
			paths[num++] = gstruct.gl_pathv[i];
	}

	if (num) {
		if (first)
			first(num);
		for (int i = 0; i < num; i++) {
			cmd->shm_file = paths[i] + sizeof(SHM_DIR) + 4;
			cmd->name = paths[i] + strlen(pattern) - 1;
			// When stdout is redirected, it is fully buffered: flush it so that
			// an error on this lock (stderr) appears after the previous output
			fflush(stdout);
			int err = action(cmd);
			if (err)
				result = err;
		}
	} else {
		nothing();
	}

	free(paths);
	globfree(&gstruct);
	free(pattern);
	return result;
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
 * displayed. If the `--force` option is used, the lock is destroyed even if in
 * use, and its value is not even read, so that a corrupted lock can also be
 * destroyed.
 *
 * @param cmd The sync_cmd structure containing the name of the semaphore.
 * @return 0 if the lock is destroyed, an error code otherwise.
 */
static int destroy(const struct sync_cmd *cmd)
{
	int value = 1, err = 0;
	sem_t *my_sem = open_sync(cmd);

	if (my_sem == SEM_FAILED)
		return OPEN_ERR;
	if (!cmd->force_flag)
		err = read_sync(my_sem, cmd, &value);
	if (close_sync(my_sem, cmd) && !err)
		err = CLOSE_ERR;
	if (err)
		return err;

	if (value < 1) {
		fprintf(stderr, "Error: cannot destroy lock '%s': it is currently locked.\n",
			cmd->name);
		fprintf(stderr, "To resolve the issue, you can:\n");
		fprintf(stderr, "\t- release the lock with: '%s unlock %s'\n", cmd->bin, cmd->name);
		fprintf(stderr, "\t- force its destruction with: '%s destroy --force %s'\n",
			cmd->bin, cmd->name);
		fprintf(stderr, "\t- force the destruction of all your locks with: '%s clean --force'\n",
			cmd->bin);
		return UNLINK_ERR;
	}

	if (sem_unlink(cmd->shm_file) != 0) {
		if (errno == ENOENT)
			fprintf(stderr, "Error: cannot destroy lock '%s': it does not exist.\n",
				cmd->name);
		else
			fprintf(stderr, "Error: cannot destroy lock '%s': %s.\n", cmd->name,
				strerror(errno));
		return fail(cmd, UNLINK_ERR);
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
		fprintf(stderr, "Error: cannot acquire lock '%s': %s.\n", cmd->name, strerror(errno));
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
	int value;
	sem_t *my_sem = open_sync(cmd);

	read_sync(my_sem, cmd, &value);
	if (value > 0) {
		fprintf(stderr, "Error: cannot release lock '%s': it is not locked.\n", cmd->name);
		exit(UNLOCK_ERR);
	}

	if (sem_post(my_sem) != 0) {
		fprintf(stderr, "Error: cannot release lock '%s': %s.\n", cmd->name, strerror(errno));
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
 * This function cannot display the number of processes waiting for the lock:
 * when the semaphore is locked, POSIX allows sem_getvalue() to return either 0
 * or minus the number of waiting processes, and Linux always returns 0.
 *
 * @param cmd The sync_cmd structure containing the name of the semaphore.
 * @param indent An optional string to indent the output.
 * @return 0 on success, an error code otherwise.
 */
static int infos(const struct sync_cmd *cmd, const char *indent)
{
	int value, err;
	sem_t *my_sem = open_sync(cmd);

	if (my_sem == SEM_FAILED)
		return OPEN_ERR;
	err = read_sync(my_sem, cmd, &value);
	if (!err)
		fprintf(stdout, "%s%s lock is %s.\n", indent ? indent : "", cmd->name,
			value > 0 ? "UNLOCKED" : "LOCKED");
	if (close_sync(my_sem, cmd) && !err)
		err = CLOSE_ERR;
	return err;
}

/**
 * @ingroup grp_cmd
 * @brief Display where the locks of `list` and `clean` are searched.
 *
 * The namespace is only mentioned when SEMCMD_NAMESPACE is set, so that
 * beginners who do not use namespaces never see it.
 */
static void print_scope(void)
{
	const char *namespace = user_namespace();

	if (namespace)
		fprintf(stdout, " in namespace '%s'", namespace);
	else
		fprintf(stdout, " in the system");
}

/**
 * @ingroup grp_cmd
 * @brief Destroy one lock for `clean` (callback of foreach_sync).
 *
 * @param cmd The sync_cmd structure containing the name of the semaphore.
 * @return 0 if the lock is destroyed, an error code otherwise.
 */
static int clean_action(const struct sync_cmd *cmd)
{
	int err = destroy(cmd);

	if (!err)
		fprintf(stdout, "Lock %s has been deleted.\n", cmd->name);
	return err;
}

/**
 * @ingroup grp_cmd
 * @brief Display the `clean` message when there is no lock (callback of
 * foreach_sync).
 */
static void clean_nothing(void)
{
	fprintf(stdout, "You currently have no locks to delete");
	print_scope();
	fprintf(stdout, ".\n");
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
 * @return 0 if all locks are destroyed, the error code of the last failure
 * otherwise.
 */
static int clean(struct sync_cmd *cmd)
{
	return foreach_sync(NULL, clean_action, clean_nothing, cmd);
}

/**
 * @ingroup grp_cmd
 * @brief Display the `list` header (callback of foreach_sync).
 *
 * @param num The number of locks found.
 */
static void list_first(int num)
{
	fprintf(stdout, "You have created %d lock%s", num, num >= 2 ? "s" : "");
	print_scope();
	fprintf(stdout, ":\n");
}

/**
 * @ingroup grp_cmd
 * @brief Display the status of one lock for `list` (callback of foreach_sync).
 *
 * @param cmd The sync_cmd structure containing the name of the semaphore.
 * @return 0 on success, an error code otherwise.
 */
static int list_action(const struct sync_cmd *cmd)
{
	return infos(cmd, "  - ");
}

/**
 * @ingroup grp_cmd
 * @brief Display the `list` message when there is no lock (callback of
 * foreach_sync).
 */
static void list_nothing(void)
{
	fprintf(stdout, "Currently, you have no locks");
	print_scope();
	fprintf(stdout, ".\n");
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
 * @return 0 on success, the error code of the last failure otherwise.
 */
static int list(struct sync_cmd *cmd)
{
	return foreach_sync(list_first, list_action, list_nothing, cmd);
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
		return infos(&user_cmd, NULL);
	else if (!strcmp(user_cmd.cmd, "destroy"))
		return destroy(&user_cmd);
	else if (!strcmp(user_cmd.cmd, "list"))
		return list(&user_cmd);
	else if (!strcmp(user_cmd.cmd, "clean"))
		return clean(&user_cmd);

	return 0;
}

/* vim: set tabstop=4 shiftwidth=4 noexpandtab: */

