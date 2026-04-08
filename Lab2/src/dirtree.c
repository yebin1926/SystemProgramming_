//--------------------------------------------------------------------------------------------------
// System Programming                         I/O Lab                                     Spring 2026
//
/// @file
/// @brief resursively traverse directory tree and list all entries
/// @author <편예빈>
/// @studid <2021-10421>
//--------------------------------------------------------------------------------------------------

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>
#include <stdarg.h>
#include <assert.h>
#include <grp.h>
#include <pwd.h>
/// @brief output control flags
#define F_DEPTH    0x1        ///< print directory tree
#define F_Filter   0x2        ///< pattern matching

/// @brief maximum numbers
#define MAX_DIR 64            ///< maximum number of supported directories
#define MAX_PATH_LEN 1024     ///< maximum length of a path
#define MAX_DEPTH 20          ///< maximum depth of directory tree (for -d option)
int max_depth = MAX_DEPTH;    ///< maximum depth of directory tree (for -d option)

/// @brief struct holding the summary
struct summary {
  unsigned int dirs;          ///< number of directories encountered
  unsigned int files;         ///< number of files
  unsigned int links;         ///< number of links
  unsigned int fifos;         ///< number of pipes
  unsigned int socks;         ///< number of sockets

  unsigned long long size;    ///< total size (in bytes)
  unsigned long long blocks;  ///< total number of blocks (512 byte blocks)
};

/// @brief print strings used in the output
const char *print_formats[8] = {
  "Name                                                        User:Group           Size    Blocks Type\n",
  "----------------------------------------------------------------------------------------------------\n",
  "%s  ERROR: %s\n", // you can use this format for error handling or utilize panic function
  "%-54s  No such file or directory\n",
  "%-54s  %8.8s:%-8.8s  %10llu  %8llu    %c\n",
  "Invalid pattern syntax",
  "Out of memory",
};

const char* pattern = NULL;  // pattern for filtering entries

/// @brief abort the program with EXIT_FAILURE and an optional error message
///
/// @param msg optional error message or NULL
/// @param format optional format string (printf format) or NULL
void panic(const char* msg, const char* format)
{
  if (msg) {
    if (format) fprintf(stderr, format, msg);
    else        fprintf(stderr, "%s\n", msg);
  }
  exit(EXIT_FAILURE);
}

/// @brief read next directory entry from open directory 'dir'. Ignores '.' and '..' entries
///
/// @param dir open DIR* stream
/// @retval entry on success
/// @retval NULL on error or if there are no more entries
struct dirent *get_next(DIR *dir)
{
  struct dirent *next;
  int ignore;

  do {
    errno = 0;
    next = readdir(dir);
    if (errno != 0) perror(NULL);
    ignore = next && ((strcmp(next->d_name, ".") == 0) || (strcmp(next->d_name, "..") == 0));
  } while (next && ignore);

  return next;
}

/// @brief qsort comparator to sort directory entries. Sorted by name, directories first.
///
/// @param a pointer to first entry
/// @param b pointer to second entry
/// @retval -1 if a<b
/// @retval 0  if a==b
/// @retval 1  if a>b
static int dirent_compare(const void *a, const void *b)
{
  struct dirent *e1 = (struct dirent*)a;
  struct dirent *e2 = (struct dirent*)b;

  if (e1->d_type != e2->d_type) {
    if (e1->d_type == DT_DIR) return -1;
    if (e2->d_type == DT_DIR) return 1;
  }

  return strcmp(e1->d_name, e2->d_name);
}

// TODO: Helper functions
static void summary_reset(struct summary *stats)
{
  // TODO: set every field in stats to 0.
  memset(stats, 0, sizeof(*stats));
}

static void summary_add(struct summary *dst, const struct summary *src)
{
  // TODO: combine two already-built summaries. add stats from src into dst.
  dst->blocks += src->blocks;
  dst->dirs += src->dirs;
  dst->fifos += src->fifos;
  dst->files += src->files;
  dst->links += src->links;
  dst->size += src->size;
  dst->socks += src->socks;
}

static char get_type_char(const struct stat *st)
{
  // TODO: return file type (directory, link, fifo, socket, block, file)
  if(S_ISDIR(st->st_mode)) return 'd';
  else if(S_ISLNK(st->st_mode)) return 'l';
  else if(S_ISFIFO(st->st_mode)) return 'f';
  else if(S_ISSOCK(st->st_mode)) return 's';
  else return ' ';

  return ' ';
}


static void summary_add_entry(struct summary *stats, const struct stat *st, char type_char)
{
  // TODO: add one visible entry to the summary. update the correct category count and accumulate size + blocks.
  switch(type_char){
    case 'd':
      stats->dirs++;
      break;
    case ' ':
      stats->files++;
      break;
    case 'l':
      stats->links++;
      break;
    case 'f':
      stats->fifos++;
      break;
    case 's':
      stats->socks++;
      break;
    default:
      break;
  }
  stats->size += st->st_size;
  stats->blocks += st->st_blocks;
}


static const char *pluralize(unsigned int count, const char *singular, const char *plural)
{
  // TODO: return singular when count == 1, otherwise plural.
  
  (void)count;
  (void)singular;
  return plural;
}

/// @brief join parent path and child name into dst
static int make_child_path(char *dst, size_t dstsz, const char *parent, const char *name)
{
  // TODO: build a child path safely without overflowing MAX_PATH_LEN.
  (void)dst;
  (void)dstsz;
  (void)parent;
  (void)name;
  return 0;
}

/// @brief build the left-column text with depth indentation
static void build_display_name(char *dst, size_t dstsz, const char *name, int depth)
{
  // TODO: prepend two spaces per depth level and append the basename.
  (void)dst;
  (void)dstsz;
  (void)name;
  (void)depth;
}

/// @brief truncate a left-aligned field with trailing dots if needed
static void format_truncated_left(char *dst, size_t dstsz, const char *src, size_t width)
{
  // TODO: if src length exceeds width, keep width-3 chars and append "...".
  // TODO: otherwise copy src as-is.
  (void)dst;
  (void)dstsz;
  (void)src;
  (void)width;
}

/// @brief get printable user name from uid
static const char *lookup_user(uid_t uid)
{
  // TODO: use getpwuid() and decide a fallback string if lookup fails.
  (void)uid;
  return "";
}

/// @brief get printable group name from gid
static const char *lookup_group(gid_t gid)
{
  // TODO: use getgrgid() and decide a fallback string if lookup fails.
  (void)gid;
  return "";
}

/// @brief validate -f pattern syntax before traversal
static int validate_pattern(const char *p)
{
  // TODO: reject empty patterns, '*' at the beginning, consecutive '*',
  // TODO: empty groups "()", and unmatched/misused parentheses.
  (void)p;
  return 0;
}

const char *find_close(const char *p)
{
  // TODO: return pointer to the matching ')' for the '(' at p, or NULL if invalid.
  (void)p;
  return NULL;
}

// TODO: Helper function for matching logic
static int submatch(const char *s, const char *p)
{
  // TODO: recursive anchored matcher for ?, x*, and (group)*.
  (void)s;
  (void)p;
  return 0;
}

static int match(const char *str, const char *pattern)
{
  // TODO: partial-match wrapper: try submatch() starting at every position in str.
  (void)str;
  (void)pattern;
  return 0;
}

/// @brief decide whether a basename matches the active filter
//unnecessary
static int entry_matches_filter(const char *name, unsigned int flags)
{
  // TODO: if filtering is disabled, always return 1; otherwise call match().
  (void)name;
  (void)flags;
  return 0;
}

/// @brief print one detailed output line for a visible entry
//unnecessary
static void print_entry_line(const char *display_name, const struct stat *st)
{
  // TODO: print name/user/group/size/blocks/type with the required widths.
  (void)display_name;
  (void)st;
}

/// @brief print a parent directory line without metadata
//unnecessary
static void print_parent_only_line(const char *display_name)
{
  // TODO: print only the left column when a non-matching directory has matching descendants.
  (void)display_name;
}

/// @brief print the root directory line shown right below the header
//unnecessary
static void print_root_line(const char *root_path)
{
  // TODO: print the root exactly as supplied on the command line.
  (void)root_path;
}

/// @brief print the missing-path error line for a root entry
//unnecessary
static void print_missing_path_line(void)
{
  // TODO: print "  ERROR: No such file or directory" under the root.
}

/// @brief print separator and per-directory summary line
//unnecessary
static void print_directory_summary(const struct summary *stats)
{
  // TODO: print the footer separator and grammatically correct one-line summary.
  (void)stats;
}

/// @brief print cumulative totals for multiple roots
static void print_aggregate_summary(const struct summary *total, int ndir)
{
  // TODO: print the final "Analyzed N directories:" block when ndir > 1.
  (void)total;
  (void)ndir;
}

/// @brief recursively process directory @a dn and print its tree
///
/// @param dn absolute or relative path string
/// @param pstr prefix string printed in front of each entry
/// @param stats pointer to statistics
/// @param flags output control flags (F_*)
void process_dir(const char *dn, const char *pstr, struct summary *stats, unsigned int flags)
{
  // TODO: open directory and handle failure

  // TODO: Read child entries using get_next(), store them in dynamic array, sort them using qsort() with dirent_compare()

  // TODO: For every entry:
  // TODO:   - stop traversing and printing beyond max_depth, according to -d
  // TODO:   - combine parent + child path and use that to call lstat()
  // TODO:   - determine the file type and whether the basename matches the filter
  // TODO:   - if filter is active:
  // TODO:       * print/count matching files
  // TODO:       * always recurse into directories
  // TODO:       * print non-matching directories only by name when they have matching descendants
  // TODO:   - if filter is inactive:
  // TODO:       * print/count every visible entry
  // TODO:   - update stats only for entries that the spec says are counted
  // TODO: recurse into subdirectories
  // TODO: close directory and free memory

  (void)dn;
  (void)pstr;
  (void)stats;
  (void)flags;
}

/// @brief print program syntax and an optional error message. Aborts the program with EXIT_FAILURE
///
/// @param argv0 command line argument 0 (executable)
/// @param error optional error (format) string (printf format) or NULL
/// @param ... parameter to the error format string
void syntax(const char *argv0, const char *error, ...)
{
  if (error) {
    va_list ap;

    va_start(ap, error);
    vfprintf(stderr, error, ap);
    va_end(ap);

    printf("\n\n");
  }

  assert(argv0 != NULL);

  fprintf(stderr, "Usage %s [-d depth] [-f pattern] [-h] [path...]\n"
                  "Recursively traverse directory tree and list all entries. If no path is given, the current directory\n"
                  "is analyzed.\n"
                  "\n"
                  "Options:\n"
                  " -d depth   | set maximum depth of directory traversal (1-%d)\n"
                  " -f pattern | filter entries using pattern (supports '?', '*', and '()')\n"
                  " -h         | print this help\n"
                  " path...    | list of space-separated paths (max %d). Default is the current directory.\n",
                  basename(argv0), MAX_DEPTH, MAX_DIR);

  exit(EXIT_FAILURE);
}

/// @brief program entry point
int main(int argc, char *argv[])
{
  const char CURDIR[] = ".";
  const char *directories[MAX_DIR];
  int   ndir = 0;

  struct summary tstat = { 0 }; // a structure to store the total statistics
  unsigned int flags = 0;

  for (int i = 1; i < argc; i++) {
    if (argv[i][0] == '-') {
      if (!strcmp(argv[i], "-d")) {
        flags |= F_DEPTH;
        if (++i < argc && argv[i][0] != '-') {
          max_depth = atoi(argv[i]);
          if (max_depth < 1 || max_depth > MAX_DEPTH) {
            syntax(argv[0], "Invalid depth value '%s'. Must be between 1 and %d.", argv[i], MAX_DEPTH);
          }
        }
        else {
          syntax(argv[0], "Missing depth value argument.");
        }
      }
      else if (!strcmp(argv[i], "-f")) {
        if (++i < argc && argv[i][0] != '-') {
          flags |= F_Filter;
          pattern = argv[i];
        }
        else {
          syntax(argv[0], "Missing filtering pattern argument.");
        }
      }
      else if (!strcmp(argv[i], "-h")) syntax(argv[0], NULL);
      else syntax(argv[0], "Unrecognized option '%s'.", argv[i]);
    }
    else {
      // anything else is recognized as a directory
      if (ndir < MAX_DIR) {
        directories[ndir++] = argv[i];
      }
      else {
        fprintf(stderr, "Warning: maximum number of directories exceeded, ignoring '%s'.\n", argv[i]);
      }
    }
  }

  // if no directory was specified, use the current directory
  if (ndir == 0) directories[ndir++] = CURDIR;

  // process each directory
  // TODO
  // Pseudo-code
  // - if -f is enabled, validate pattern before printing anything
  // - reset total statistics (tstat)
  // - loop over all root directories in 'directories'
  //   - reset per-directory statistics (dstat)
  //   - print header
  //   - print directory name (root)
  //   - if root does not exist:
  //   -   print the indented "ERROR: No such file or directory" line
  //   - else:
  //   -   call process_dir() with the proper initial prefix / depth state
  //   - print footer + per-directory summary using correct singular/plural words
  //   - add dstat to tstat
  // - if ndir > 1, print aggregate statistics block

  (void)tstat;
  return 0;
}
