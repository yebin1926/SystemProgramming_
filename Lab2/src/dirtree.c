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
  "%s  ERROR: %s\n", // you can use this format for error handling or utilize panic function. For when you want to print an in-output tree error line and continue processing.
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
// when the program should print an error and then immediately exit.
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
  if(count == 1){
    return singular;
  }
  return plural;
}

static int make_child_path(char *dst, size_t dstsz, const char *parent, const char *name)
{
  int n;

  if (parent[strlen(parent) - 1] == '/')
    n = snprintf(dst, dstsz, "%s%s", parent, name);
  else
    n = snprintf(dst, dstsz, "%s/%s", parent, name);

  if (n < 0 || (size_t)n >= dstsz) return -1;
  return 0;
}


static void format_truncated_left(char *dst, size_t dstsz, const char *src, size_t width)
{
  size_t len;

  if (dstsz == 0) return;

  len = strlen(src);

  if (len <= width) {
    snprintf(dst, dstsz, "%s", src);
  } else if (width > 3) {
    snprintf(dst, dstsz, "%.*s...", (int)(width - 3), src);
  } else {
    snprintf(dst, dstsz, "%.*s", (int)width, src);
  }
}

static void build_display_name(char *dst, size_t dstsz, const char *name, int depth)
{
  char temp[MAX_PATH_LEN];
  snprintf(temp, sizeof(temp), "%*s%s", depth * 2, "", name);
  format_truncated_left(dst, dstsz, temp, 54);
}


static const char *lookup_user(uid_t uid)
{
  // TODO: use getpwuid() and decide a fallback string if lookup fails.
  struct passwd *pw = getpwuid(uid); 
  const char *user = pw ? pw->pw_name : "?";
  return user;
}

static const char *lookup_group(gid_t gid)
{
  // TODO: use getgrgid() and decide a fallback string if lookup fails.
  struct group  *gr = getgrgid(gid);
  const char *group = gr ? gr->gr_name : "?";
  return group;
}

const char *find_close(const char *p)
{
  // TODO: return pointer to the matching ')' for the '(' at p, or NULL if invalid.
  int depth = 1;                        // start with 1 because we're seeing one '(' (although we're not using nested brackets)
  for (p = p + 1; *p; p++) {            // traverse through string
      if (*p == '(') depth++;           // increase depth count when coming across (, so that we find outermost )
      else if (*p == ')') {
          depth--;
          if (depth == 0) return p;   // if all ( are closed with ), return that pointer
      }
  }
  return NULL;
}

static int validate_pattern(const char *p)
{
  if (p == NULL || *p == '\0') return 0;

  for (; *p; p++) {
    if (*p == '*') {
      if (p == pattern || *(p - 1) == '*' || *(p - 1) == '(') return 0;
      //if * is first char, or **, or (*), then invalid
    }
    else if (*p == '(') {
      const char *close = find_close(p);

      if (close == NULL) return 0;        //check unmatched (
      if (close == p + 1) return 0;       //check empty ()

      p = close;                          //skip to matching )
    }
    else if (*p == ')') {
      return 0;                           //check stray )
    }
  }

  return 1;
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

/// @brief print a parent directory line without metadata
//unnecessary
static void print_parent_only_line(const char *display_name)
{
  // TODO: print only the left column when a non-matching directory has matching descendants.
  (void)display_name;
}

// TODO: print the root directory line shown right below the header
static void print_root_line(const char *root_path)
{
  printf("%s\n", root_path);
}

// TODO: print the missing-path error line for a root entry
static void print_missing_path_line(void)
{
  printf(print_formats[2], "", "No such file or directory");
}

static void print_permission_denied_line(void)
{
  fprintf(stderr, print_formats[2], "", "Permission denied");
}

static void print_entry_line(const char *display_name, const struct stat *st)
{
  const char *user = lookup_user(st->st_uid);
  const char *group = lookup_group(st->st_gid);
  char typech = get_type_char(st);

  printf(print_formats[4],
         display_name,
         user,
         group,
         (unsigned long long)st->st_size,
         (unsigned long long)st->st_blocks,
         typech);
}

// TODO: print separator and per-directory summary line
static void print_directory_summary(const struct summary *stats)
{
  char left[256];
  char summary_col[69];
  const char *s_files  = pluralize(stats->files, "", "s");
  const char *s_links  = pluralize(stats->links, "", "s");
  const char *s_pipes  = pluralize(stats->fifos, "", "s");
  const char *s_socks  = pluralize(stats->socks, "", "s");
  const char *dir_word = pluralize(stats->dirs, "directory", "directories");

  snprintf(left, sizeof(left),
          "%u file%s, %u %s, %u link%s, %u pipe%s, and %u socket%s",
          stats->files, s_files,
          stats->dirs, dir_word,
          stats->links, s_links,
          stats->fifos, s_pipes,
          stats->socks, s_socks);
  
  format_truncated_left(summary_col, sizeof(summary_col), left, 68);

  printf("%s", print_formats[1]);
  printf("%-68s%14llu%9llu\n",
        summary_col,
        (unsigned long long)stats->size,
        (unsigned long long)stats->blocks);
  printf("\n");
}

static void print_aggregate_summary(const struct summary *total, int ndir)
{
  // TODO: print the final total "Analyzed N directories:" block when ndir > 1.
  if (ndir > 1) {
    printf("Analyzed %d directories:\n", ndir);
    printf("  total # of files:%24u\n", total->files);
    printf("  total # of directories:%18u\n", total->dirs);
    printf("  total # of links:%24u\n", total->links);
    printf("  total # of pipes:%24u\n", total->fifos);
    printf("  total # of sockets:%22u\n", total->socks);
    printf("  total # of entries:%22u\n",
          total->files + total->dirs + total->links + total->fifos + total->socks);
    printf("  total file size:%25llu\n", total->size);
    printf("  total # of blocks:%22llu\n", total->blocks);
  }
}


/// @brief recursively process directory @a dn and print its tree
///
/// @param dn absolute or relative path string
/// @param pstr prefix string printed in front of each entry
/// @param stats pointer to statistics
/// @param flags output control flags (F_*)
static int process_dir(const char *dn, int depth, const char *pstr, struct summary *stats, unsigned int flags)
{
  // TODO: open directory and handle failure
  DIR *dir = opendir(dn);                  //open directory
  if(dir == NULL){
    if (errno == EACCES) print_permission_denied_line();
    return -1;
  }           //return if directory doesn't exist

  struct dirent *list_directories = NULL;   //list of directories for that depth, for later sorting
  int cap = 0;                              //cap: count of files in that depth
  struct dirent *e;

  // TODO: Read child entries using get_next(), store them in dynamic array, sort them using qsort() with dirent_compare()
  while((e = get_next(dir)) != NULL){       //for each file in that depth, store file into list_directories then sort
    cap++;
    struct dirent *tmp = realloc(list_directories, cap * sizeof(struct dirent)); //reallocate size of array if another file is found
    if(tmp == NULL) {
      closedir(dir);
      free(list_directories);
      panic(print_formats[6], NULL);
    }
    list_directories = tmp;
    list_directories[cap-1] = *e;
  }
  qsort(list_directories, cap, sizeof(struct dirent), dirent_compare); //sort directories in that depth first showing directories then alphabetical

  // ------ NO F FILTER ------
  for (int i = 0; i < cap; i++) { //For every entry
    //TODO: build child path 
    const char *name = list_directories[i].d_name;
    char full_path[MAX_PATH_LEN];
    if (make_child_path(full_path, sizeof full_path, dn, name) == -1) {
      closedir(dir);
      free(list_directories);
      return -1;
    }

    //TODO: call lstat and get info
    struct stat st;
    if (lstat(full_path, &st) == -1) { perror("lstat"); continue; }  //get lstat of path, and increment the directory's stats
    // stats->size   += st.st_size;
    // stats->blocks += st.st_blocks;

    //TODO: get type of file
    char typech = get_type_char(&st);

    //TODO: update stats
    summary_add_entry(stats, &st, typech);

    //TODO: apply indentation and ellipses if needed
    char namecol[256];
    build_display_name(namecol, sizeof(namecol), name, depth);

    //print
    print_entry_line(namecol, &st);

    //TODO: if not reached -d depth, keep printing children
    if (S_ISDIR(st.st_mode) && depth < max_depth) {
      (void)process_dir(full_path, depth + 1, pstr, stats, flags); // keep printing children
    }

  }
  closedir(dir);
  free(list_directories);
  return 1;
  
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

  for (int j = 0; j < ndir; j++) {
    if (directories[j]){
      struct summary individual_summary;
      struct stat root_st;
      summary_reset(&individual_summary);

      //process each directory
      printf("%s%s", print_formats[0], print_formats[1]);
      print_root_line(directories[j]);

      //TODO: missing root error handling
      if (lstat(directories[j], &root_st) == -1) {
        if (errno == ENOENT) {
          print_missing_path_line();
          print_directory_summary(&individual_summary);
          summary_add(&tstat, &individual_summary);
          continue;
        }

        print_directory_summary(&individual_summary);
        summary_add(&tstat, &individual_summary);
        continue;
      }


      process_dir(directories[j], 1, pattern, &individual_summary, flags);

      //TODO: print individual summary statistics
      print_directory_summary(&individual_summary);

      //TODO: update tstat
      summary_add(&tstat, &individual_summary);
    }
  }

  //TODO: print tstat if more than one directory was traversed
  print_aggregate_summary(&tstat, ndir);

  (void)tstat;
  return 0;
}
