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
#include <libgen.h>

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

static int match_group_once(const char *s, const char *group_start, const char *group_end){
  int count = 0;
  while(group_start != group_end){
    if (*s == '\0') return -1;
    if(*s != *group_start && *group_start != '?') return -1;
    s++;
    group_start++;
    count++;
  }
  return count;
}


// TODO: Tests whether the pattern matches the START of the string
static int submatch(const char *s, const char *p)
{
  while (*p != '\0') {
    if (*s == '\0') {
      // if search keyword is "", and if pattern can be skipped (like (abc)* or x* repeated), treat as match. Else, return 0
      while (*p) { //case: x*
        if (*p == '(') { // case: (abc)*
          const char *close = find_close(p);
          if (close && *(close + 1) == '*')
            p = close + 2; // skip (group)*
          else
            return 0;
        }
        else if (*(p + 1) == '*') {
          p += 2;
        }
        else {
          return 0;
        }
      }
      return 1;
    }

    else if (*p == '(') { // if it could be a group
      //endless repetition here 
      const char *p_closed = find_close(p); // find pointer to closing braket ), if none return 0
      if (p_closed == NULL) return 0;

      int len = (int)(p_closed - (p + 1));  // get size of substring of group
      const char *after = p_closed + 1; // get first char after ')'

      if (*after == '*') {  // this group should be checked for repetition
        const char *ts = s;
  
        while(1){ // ts = string, after = the * sign after pattern, tp: pattern
          // TODO: Check 0 repetition
          if(submatch(ts, after + 1)) return 1; //if the rest of the string is the same without the pattern, return 1
          // TODO: Check whether one full abc matches at s
          int advance = match_group_once(ts, p+1, p_closed);
          if(advance < 0) return 0;
          // TODO: If yes, advance s by len(pattern) and try rest again
          ts += advance;
        }

      }
      else {
        int k = 0;  // compare inner literally
        const char *ts = s;
        const char *tp = p + 1;

        while (k < len && *ts && *tp && (*tp == '?' || *ts == *tp)) {
          k++;
          ts++;
          tp++;
        }
        if (k != len) return 0; // inner didn't match once

        // advance both: we consumed the group once
        s = ts; // move input past inner
        p = after;  // move pattern past ')'
        continue; // continue the while-loop in submatch
      }
    }

    else if (*(p + 1) == '*') { //if next char is *, save current *p to test repetitions of it    
      char c = *p;  // repeat this literal
      const char *rest = p + 2;  // pattern after the 'x*'

      if (submatch(s, rest)) return 1;  // try zero copies, if it works return 1

      while (*s == c) { // try one-or-more copies
        s++;
        if (submatch(s, rest)) return 1;
      }
      return 0;
    }

    else if (*p != '?' && *s != *p) { // if it's not a star case: must match literally or be '?'. If not, return 0
      return 0;
    }

    else {
      s++;
      p++;
    }
  }

  return 1;
}


static int match(const char *str, const char *pattern)
{ // TODO: PARTIAL-match wrapper: try submatch() starting at every position in str.
  if (str == NULL || pattern == NULL) return 0;

  do {
    if (submatch(str, pattern)) return 1;
  } while (*str++);

  return 0;
}

static int entry_matches_filter(const char *name, unsigned int flags)
{
  // TODO: if filtering is disabled, always return 1; otherwise call match().
  if ((flags & F_Filter) == 0) return 1;
  return match(name, pattern);
}

static int subtree_has_match(const char *dn, int depth, unsigned int flags)
{
  DIR *dir;
  struct dirent *list_directories = NULL;
  struct dirent *e;
  int cap = 0;
  int found = 0;

  dir = opendir(dn);
  if (dir == NULL) return 0;

  while ((e = get_next(dir)) != NULL) {
    struct dirent *tmp = realloc(list_directories, (cap + 1) * sizeof(struct dirent));

    if (tmp == NULL) {
      closedir(dir);
      free(list_directories);
      panic(print_formats[6], NULL);
    }

    list_directories = tmp;
    list_directories[cap++] = *e;
  }

  qsort(list_directories, cap, sizeof(struct dirent), dirent_compare);

  for (int i = 0; i < cap && !found; i++) {
    const char *name = list_directories[i].d_name;
    char full_path[MAX_PATH_LEN];
    struct stat st;

    if (make_child_path(full_path, sizeof(full_path), dn, name) == -1) continue;
    if (lstat(full_path, &st) == -1) continue;

    if (entry_matches_filter(name, flags)) {
      found = 1;
    }
    else if (S_ISDIR(st.st_mode) && depth < max_depth) {
      found = subtree_has_match(full_path, depth + 1, flags);
    }
  }

  closedir(dir);
  free(list_directories);
  return found;
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

static void print_permission_denied_line(int depth)
{
  char indent[64];

  snprintf(indent, sizeof(indent), "%*s", depth * 2, "");
  fprintf(stderr, print_formats[2], indent, "Permission denied");
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
    if (errno == EACCES) print_permission_denied_line(depth);
    return 0;
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
  if (!(flags & F_Filter)){
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
      if (lstat(full_path, &st) == -1) { perror("lstat"); continue; }  //get lstat of path

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
  }

    // ------ WITH F FILTER ------
  int any_match_in_this_dir = 0;

  for (int i = 0; i < cap; i++) {
    const char *name = list_directories[i].d_name;
    char full_path[MAX_PATH_LEN];
    struct stat st;
    int self_matches;
    int child_has_match = 0;
    int is_dir;
    char namecol[256];

    if (make_child_path(full_path, sizeof(full_path), dn, name) == -1) {
      closedir(dir);
      free(list_directories);
      return -1;
    }

    if (lstat(full_path, &st) == -1) {
      if (errno == EACCES) print_permission_denied_line(depth);
      continue;
    }

    is_dir = S_ISDIR(st.st_mode);
    self_matches = entry_matches_filter(name, flags);

    if (is_dir) { //if it is a directory
      if (!self_matches && depth < max_depth) {
        child_has_match = subtree_has_match(full_path, depth + 1, flags);
      }

      if (self_matches || child_has_match) { //if either one is a match, we have to print the parent file name so get formatting
        build_display_name(namecol, sizeof(namecol), name, depth);

        if (self_matches) { //if this one is a match, we have to print and append its stats too
          print_entry_line(namecol, &st);
          summary_add_entry(stats, &st, get_type_char(&st));
        }
        else {
          printf("%s\n", namecol); //if only child is a match, print only the formatted file name
        }

        any_match_in_this_dir = 1;
      }

      if (depth < max_depth) {
        process_dir(full_path, depth + 1, pstr, stats, flags);
      }
    } else { //if it's not a directory
      if (self_matches) {
        build_display_name(namecol, sizeof(namecol), name, depth);
        print_entry_line(namecol, &st);
        summary_add_entry(stats, &st, get_type_char(&st));
        any_match_in_this_dir = 1;
      }
    }
  }

  closedir(dir);
  free(list_directories);
  return any_match_in_this_dir;
}

/// @brief print program syntax and an optional error message. Aborts the program with EXIT_FAILURE
///
/// @param argv0 command line argument 0 (executable)
/// @param error optional error (format) string (printf format) or NULL
/// @param ... parameter to the error format string
void syntax(const char *argv0, const char *error, ...)
{
  int exit_code = EXIT_FAILURE;

  if (error) {
    va_list ap;

    va_start(ap, error);
    vfprintf(stderr, error, ap);
    va_end(ap);

    printf("\n\n");
  }
  else {
    exit_code = EXIT_SUCCESS;
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
                  basename((char *)argv0), MAX_DEPTH, MAX_DIR);

  exit(exit_code);
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

  if ((flags & F_Filter) && !validate_pattern(pattern)) {
    printf("%s\n", print_formats[5]);
    return EXIT_FAILURE;
  }

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
