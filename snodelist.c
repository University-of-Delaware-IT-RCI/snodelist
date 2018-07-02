/*
 * snodelist
 *
 * Author:  Dr. Jeffrey Frey, University of Delaware
 * Date:    2017-12-01
 *
 * Build a Slurm host list from arguments on the command line or
 * the SLURM_JOB_NODELIST environment variable and display as
 * compressed or expanded lists.
 *
 * Note that the expansion has historically been suggested using
 * scontrol:
 *
 *   $ scontrol show hostnames 'n[000-002,005-008],g[100-102]'
 *
 * This tool allows for display in BOTH directions -- expanded
 * or compressed form -- with options for removal of duplicate
 * names, alternate delimiter in expanded form, etc.
 *
 * This program links to the Slurm library to directly use the
 * hostlist API (rather than implementing the parsing itself).
 *
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include "slurm/slurm.h"

//

typedef enum {
  snodelist_mode_expand       = 0,
  snodelist_mode_compress     = 1,
  snodelist_mode_machinefile  = 2,
  //
  snodelist_mode_default = snodelist_mode_expand
} snodelist_mode;

static const char*  snodelist_mode_strings[] = {
                        "expand",
                        "compress",
                        "machinefile",
                        NULL
                      };

//

static const char   *snodelist_default_delimiter = "\n";

//

static struct option snodelist_opts[] = {
                        { "help",         no_argument,        NULL, 'h' },
                        { "expand",       no_argument,        NULL, 'e' },
                        { "compress",     no_argument,        NULL, 'c' },
                        { "include-env",  optional_argument,  NULL, 'i' },
                        { "nodelist",     required_argument,  NULL, 'l' },
                        { "unique",       no_argument,        NULL, 'u' },
                        { "delimiter",    required_argument,  NULL, 'd' },
                        { "machinefile",  no_argument,        NULL, 'm' },
                        { "format",       required_argument,  NULL, 'f' },
                        { "no-repeats",   no_argument,        NULL, 'n' },
                        { NULL,           0,                  NULL,  0  }
                      };

static const char   *snodelist_opts_string = "heci:l:ud:mf:n";

//

void
usage(
  const char    *exe
)
{
  printf(
      "usage:\n"
      "\n"
      "  %s {options} {<host expression> {<host expression> ..}}\n"
      "\n"
      " options:\n"
      "\n"
      "  -h/--help                      show this information\n"
      "\n"
      "  EXPAND / COMPRESS MODES\n"
      "\n"
      "    -e/--expand                  output as individual names (default mode)\n"
      "      -d/--delimiter <str>       use <str> between each hostname in expanded mode\n"
      "                                 (default:  a newline character)\n"
      "\n"
      "    -c/--compress                output in compressed (compact) form\n"
      "\n"
      "    -i/--include-env{=<varname>} include a host list present in the environment\n"
      "                                 variable <varname>; omitting the <varname> defaults\n"
      "                                 to using SLURM_JOB_NODELIST (can be used multiple times)\n"
      "    -l/--nodelist=<file>         read node expressions from the given <file>; use a dash\n"
      "                                 (-) to read from stdin (can be used multiple times)\n"
      "    -u/--unique                  remove any duplicate names (for expand and compress\n"
      "                                 modes)\n"
      "\n"
      "    NOTE:  In the expand/compress modes, if no host lists are explicitly added then\n"
      "           SLURM_JOB_NODELIST is checked by default.\n"
      "\n"
      "  MACHINEFILE MODE\n"
      "\n"
      "    -m/--machinefile             generate a MPI-style machine file using the\n"
      "                                 SLURM_JOB_NODELIST and SLURM_TASKS_PER_NODE\n"
      "                                 environment variables\n"
      "      -f/--format=<line-format>  apply the given <line-format> to each host in the\n"
      "                                 list; the <line-format> can include the following\n"
      "                                 tokens that are filled-in for each host:\n"
      "\n"
      "                                   %%%%       literal percent sign\n"
      "                                   %%h      host name\n"
      "                                   %%c      rank count\n"
      "                                   %%C      optional rank count (omitted if 1)\n"
      "                                   %%[:]c   rank count with preceding colon\n"
      "                                   %%[:]C   optional rank count with preceding colon\n"
      "\n"
      "                                 the colon in the latter two tokens can be any string\n"
      "                                 of punctuation in the set [-_:;.,/\\|] or whitespace\n"
      "      -n/--no-repeats            if the <line-format> lacks a count token, do not\n"
      "                                 repeat the line once for each task on the host\n"
      "\n"
      ,
      exe
    );
}

//

typedef struct {
  const char      *task_count_str;
  const char      *cur_ptr;
  int             value;
  int             count;
} task_count_t;

void
task_count_init(
  task_count_t    *tc,
  const char      *task_count_str
)
{
  tc->task_count_str = tc->cur_ptr = task_count_str;
  tc->value = -1;
  tc->count = 0;
}

int
task_count_next(
  task_count_t    *tc
)
{
  if ( tc->count == 0 ) {
    if ( *(tc->cur_ptr) ) {
      char          *end_ptr = NULL;
      long          c = strtol(tc->cur_ptr, &end_ptr, 10);

      if ( end_ptr > tc->cur_ptr ) {
        tc->value = c;
        if ( *end_ptr == '(' ) {
          end_ptr++;
          if ( *end_ptr == 'x' ) {
            char    *alt_end_ptr = NULL;

            end_ptr++;
            c = strtol(end_ptr, &alt_end_ptr, 10);
            if ( (c > 0) && (alt_end_ptr > end_ptr) ) {
              tc->count = c;
              end_ptr = alt_end_ptr;
              if ( (*end_ptr == ')') && ((*(end_ptr + 1) == ',') || (*(end_ptr + 1) == '\0')) ) {
                end_ptr++;
                if ( *end_ptr ) end_ptr++;
              } else {
                fprintf(stderr, "ERROR:  unexpected character at offset %d: %s\n",
                          (end_ptr - tc->task_count_str), tc->task_count_str);
                return -1;
              }
            } else {
              fprintf(stderr, "ERROR:  invalid repeat count at offset %d: %s\n",
                        (end_ptr - tc->task_count_str), tc->task_count_str);
              return -1;
            }
          } else {
            fprintf(stderr, "ERROR:  invalid repeat specification at offset %d: %s\n",
                      (end_ptr - tc->task_count_str), tc->task_count_str);
            return -1;
          }
        } else if ( *end_ptr == ',' ) {
          tc->count = 1;
          end_ptr++;
        } else if ( *end_ptr == '\0' ) {
          tc->count = 1;
        } else {
          fprintf(stderr, "ERROR:  unexpected character at offset %d: %s\n",
                    (end_ptr - tc->task_count_str), tc->task_count_str);
          return -1;
        }
      } else {
        fprintf(stderr, "ERROR:  invalid integer value at offset %d: %s\n",
                  (tc->cur_ptr - tc->task_count_str), tc->task_count_str);
        return -1;
      }
      tc->cur_ptr = end_ptr;
    } else {
      return -1;
    }
  }
  tc->count--;
  return tc->value;
}

//

void
add_from_env(
  hostlist_t    the_hostlist,
  const char    *env_var_name
)
{
  char          *env_var_value = getenv(env_var_name);

  if ( env_var_value ) slurm_hostlist_push(the_hostlist, env_var_value);
}

//

bool
add_from_file(
  hostlist_t    the_hostlist,
  const char    *file
)
{
  bool          rc = false;
  FILE          *fptr = NULL;
  
  if ( *file == '-' && *(file+1) == '\0' ) {
    fptr = stdin;
  } else {
    fptr = fopen(file, "r");
  }
  if ( fptr ) {
    char      *line = NULL;
    size_t    line_len = 0;
    
    rc = true;
    while ( ! feof(fptr) ) {
      if ( getline(&line, &line_len, fptr) > 0 ) {
        char  *p = line;
        
        while ( *p ) {
          char  *s;
          
          // Drop leading whitespace:
          while ( *p && isspace(*p) ) p++;
          // End of the line or a comment character, exit this loop:
          if ( ! *p || (*p == '#') ) break;
          s = p;
          // Get past the next expression:
          while ( *p && ! isspace(*p) ) p++;
          if ( p > s ) {
            if ( *p ) {
              *p = '\0';
              p++;
            }
            slurm_hostlist_push(the_hostlist, s);
          }
        }
      }
    }
    if ( line ) free(line);
    if ( fptr != stdin ) fclose(fptr);
  } else {
    fprintf(stderr, "ERROR:  unable to open nodelist: %s\n", file);
  }
  return rc;
}

//

void
print_machinefile(
  hostlist_t    the_hostlist,
  task_count_t  *tc,
  const char    *format,
  bool          no_repeats
)
{
  bool          has_count = false;

  if ( strstr(format, "%c") || strstr(format, "%C") ) {
    has_count = true;
  } else {
    const char  *s = format;

    while ( (s = strstr(s, "%[")) ) {
      s += 2;
      while ( *s && (*s != ']') ) s++;
      if ( *s == ']' ) {
        s++;
        if ( (*s == 'c') || (*s == 'C') ) {
          has_count = true;
          break;
        }
      }
    }
  }

  while ( true ) {
    const char  *node_name = slurm_hostlist_shift(the_hostlist);
    const char  *format_ptr = format;
    int         task_count = -1;

    if ( ! node_name ) break;

    task_count = task_count_next(tc);
    if ( task_count <= 0 ) break;

    if ( has_count || no_repeats ) {
      while ( *format_ptr ) {
        switch ( *format_ptr ) {

          case '%': {
            format_ptr++;
            switch ( *format_ptr ) {

              case '%':
                fputc('%', stdout);
                format_ptr++;
                break;

              case 'h':
                fprintf(stdout, "%s", node_name);
                format_ptr++;
                break;

              case 'C':
                if ( task_count <= 1 ) {
                  format_ptr++;
                  break;
                }
              case 'c':
                fprintf(stdout, "%d", task_count);
                format_ptr++;
                break;

              case '[': {
                const char    *delim = ++format_ptr;
                int           delim_len = 0;

                while ( *format_ptr && (*format_ptr != ']') ) delim_len++, format_ptr++;
                if ( *format_ptr == ']' ) {
                  format_ptr++;
                  switch ( *format_ptr ) {

                    case 'C':
                      if ( task_count <= 1 ) {
                        format_ptr++;
                        break;
                      }
                    case 'c':
                      while ( delim_len-- ) fputc(*delim++, stdout);
                      fprintf(stdout, "%d", task_count);
                      format_ptr++;
                      break;

                    default:
                      format_ptr++;
                    case '\0':
                      break;
                  }
                } else {
                  fprintf(stderr, "ERROR:  invalid delimiter in format specification: %s\n", delim);
                  exit(EINVAL);
                }
                break;
              }

              case '\0':
                break;

              default:
                format_ptr++;
                break;

            }
            break;
          }

          default:
            fputc(*format_ptr, stdout);
            format_ptr++;
            break;
        }
      }
      fputc('\n', stdout);
    } else {
      while ( task_count-- ) {
        format_ptr = format;
        while ( *format_ptr ) {
          switch ( *format_ptr ) {

            case '%': {
              format_ptr++;
              switch ( *format_ptr ) {

                case '%':
                  fputc('%', stdout);
                  format_ptr++;
                  break;

                case 'h':
                  fprintf(stdout, "%s", node_name);
                  format_ptr++;
                  break;

                case '\0':
                  break;

                default:
                  format_ptr++;
                  break;

              }
              break;
            }

            default:
              fputc(*format_ptr, stdout);
              format_ptr++;
              break;
          }
        }
        fputc('\n', stdout);
      }
    }
  }
}

//

int
main(
  int           argc,
  char * const  argv[]
)
{
  int               optc;
  snodelist_mode    mode = snodelist_mode_default;
  bool              do_uniq = false;
  bool              did_include_an_env_var = false;
  bool              no_repeats = false;
  const char        *delimiter = snodelist_default_delimiter;
  const char        *machinefile_format = "%h%[:]C";
  hostlist_t        hostlist = slurm_hostlist_create("");

  while ( (optc = getopt_long(argc, argv, snodelist_opts_string, snodelist_opts, NULL)) != -1 ) {
    switch ( optc ) {

      case 'h':
        usage(argv[0]);
        exit(0);

      case 'e':
        mode = snodelist_mode_expand;
        break;

      case 'c':
        mode = snodelist_mode_compress;
        break;

      case 'i': {
        const char    *env_var_name;

         if ( optc == ':' ) {
          env_var_name = "SLURM_JOB_NODELIST";
        } else if ( optarg && *optarg ) {
          env_var_name = optarg;
        } else {
          fprintf(stderr, "ERROR:  invalid variable name provided with -i/--include-env option\n");
          exit(EINVAL);
        }
        add_from_env(hostlist, env_var_name);
        break;
      }

      case 'l':
        if ( optarg && *optarg ) {
          if ( ! add_from_file(hostlist, optarg) ) {
            exit(EINVAL);
          }
        } else {
          fprintf(stderr, "ERROR:  invalid file path provided with -f/--nodelist option\n");
          exit(EINVAL);
        }
        break;

      case 'u':
        do_uniq = true;
        break;

      case 'd':
        if ( ! optarg ) {
          fprintf(stderr, "ERROR:  no delimiter string provided with -d/--delimiter option\n");
          exit(EINVAL);
        }
        delimiter = optarg;
        break;

      case 'm':
        mode = snodelist_mode_machinefile;
        break;

      case 'f':
        machinefile_format = optarg;
        break;

      case 'n':
        no_repeats = true;
        break;

    }
  }

  if ( mode == snodelist_mode_machinefile ) {
    const char        *node_list, *task_count_list;
    task_count_t      tc;

    slurm_hostlist_destroy(hostlist); hostlist = NULL;

    node_list = getenv("SLURM_JOB_NODELIST");
    if ( ! node_list || ! *node_list ) {
      fprintf(stderr, "ERROR:  no SLURM_JOB_NODELIST in environment\n");
      exit(EINVAL);
    }

    task_count_list = getenv("SLURM_TASKS_PER_NODE");
    if ( ! task_count_list || ! *task_count_list ) {
      fprintf(stderr, "ERROR:  no SLURM_TASKS_PER_NODE in environment\n");
      exit(EINVAL);
    }
    task_count_init(&tc, task_count_list);

    hostlist = slurm_hostlist_create(node_list);
    if ( slurm_hostlist_count(hostlist) > 0 ) {
      print_machinefile(hostlist, &tc, machinefile_format, no_repeats);
    }
  } else {
    if ( optind == argc && ! did_include_an_env_var ) add_from_env(hostlist, "SLURM_JOB_NODELIST");

    while ( optind < argc ) {
      slurm_hostlist_push(hostlist, argv[optind]);
      optind++;
    }

    if ( slurm_hostlist_count(hostlist) > 0 ) {
      if ( do_uniq ) slurm_hostlist_uniq(hostlist);

      switch ( mode ) {

        case snodelist_mode_expand: {
          char      *outNode;
          bool      showDelim = false;

          while ( (outNode = slurm_hostlist_shift(hostlist)) ) {
            printf("%s%s", (showDelim ? delimiter : ""), outNode);
            showDelim = true;
            free((void*)outNode);
          }
          fputc('\n', stdout);
          break;
        }

        case snodelist_mode_compress: {
          char      *outList = slurm_hostlist_ranged_string_malloc(hostlist);

          if ( outList ) {
            printf("%s\n", outList);
            free((void*)outList);
          }
          break;
        }

      }
    }
  }
  slurm_hostlist_destroy(hostlist);

  return 0;
}
