# snodelist

A tool for working with Slurm hostlists.  Rather than relying on 'scontrol show hostnames' to expand a Slurm compact host list to a newline-delimited list, this tool allows the user to:

- choose the delimiter between hosts
- enable culling of repeat host names
- display either the compact or expanded forms

The tool also contains a "machinefile" mode to turn the `SLURM_JOB_NODELIST` and `SLURM_TASKS_PER_NODE` environment variables into an arbitrary-format listing a'la MPI machine files:

```
$ echo $SLURM_JOB_NODELIST
n[000-003]
$ echo $SLURM_TASKS_PER_NODE
1,4(x2),8
$ snodelist -m -f '%h slots=%c maxslots=16'
n000 slots=1 maxslots=16
n001 slots=4 maxslots=16
n002 slots=4 maxslots=16
n003 slots=8 maxslots=16
```

The program links against the Slurm library to directly use the `hostlist` API; thus, all parsing is done exactly as Slurm would do it.

The available command line options can be summarized using the `--help` flag:

```
$ snodelist  -h
usage:

  snodelist {options} {<host expression> {<host expression> ..}}

 options:

  -h/--help                      show this information

  EXPAND / COMPRESS MODES

    -e/--expand                  output as individual names (default mode)
      -d/--delimiter <str>       use <str> between each hostname in expanded mode
                                 (default:  a newline character)

    -c/--compress                output in compressed (compact) form

    -i/--include-env{=<varname>} include a host list present in the environment
                                 variable <varname>; omitting the <varname> defaults
                                 to using SLURM_JOB_NODELIST
    -u/--unique                  remove any duplicate names (for expand and compress
                                 modes)

    NOTE:  In the expand/compress modes, if no host lists are explicitly added then
           SLURM_JOB_NODELIST is checked by default.

  MACHINEFILE MODE

    -m/--machinefile             generate a MPI-style machine file using the
                                 SLURM_JOB_NODELIST and SLURM_TASKS_PER_NODE
                                 environment variables
      -f/--format=<line-format>  apply the given <line-format> to each host in the
                                 list; the <line-format> can include the following
                                 tokens that are filled-in for each host:

                                   %%       literal percent sign
                                   %h      host name
                                   %c      rank count
                                   %C      optional rank count (omitted if 1)
                                   %[:]c   rank count with preceding colon
                                   %[:]C   optional rank count with preceding colon

                                 the colon in the latter two tokens can be any string
                                 of punctuation in the set [-_:;.,/\|] or whitespace
      -n/--no-repeats            if the <line-format> lacks a count token, do not
                                 repeat the line once for each task on the host

```

