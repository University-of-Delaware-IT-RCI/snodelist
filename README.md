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

  ./snodelist {options} {<host expression> {<host expression> ..}}

 options:

  -h/--help                        show this information

  EXPAND / COMPRESS MODES

    -e/--expand                    output as individual names (default mode)
      -d/--delimiter <str>         use <str> between each hostname in expanded mode
                                   (default:  a newline character)

    -c/--compress                  output in compressed (compact) form

    -i/--include-env{=<varname>}   include a host list present in the environment
                                   variable <varname>; omitting the <varname> defaults
                                   to using SLURM_JOB_NODELIST (can be used multiple times)
    -l/--nodelist=<file>           read node expressions from the given <file>; use a dash
                                   (-) to read from stdin (can be used multiple times)
    -X/--exclude-env=<varname>     remove all hosts present in the environment variable
                                   <varname> from the final node list
    -x--exclude=<host expression>  remove hosts from the final node list
    -u/--unique                    remove any duplicate names (for expand and compress
                                   modes)

    NOTE:  In the expand/compress modes, if no host lists are explicitly added then
           SLURM_JOB_NODELIST is checked by default.

  MACHINEFILE MODE

    -m/--machinefile               generate a MPI-style machine file using the
                                   SLURM_JOB_NODELIST and SLURM_TASKS_PER_NODE
                                   environment variables
      -f/--format=<line-format>    apply the given <line-format> to each host in the
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
      -n/--no-repeats              if the <line-format> lacks a count token, do not
                                   repeat the line once for each task on the host

```

## Building the program

The CMake build system is used to compile and link the utility.  There is one optional (well, somewhat) variable that must be defined for CMake:

```bash
[prompt]$ mkdir build-2025.01.07 ; cd build-2025.01.07
[prompt]$ cmake -DSLURM_PREFIX=/path/where/slurm/is/installed ..
```

If the `SLURM_PREFIX` is not provided, it defaults to `/usr/local`.


If your build of Slurm used a different compiler toolchain, make sure to indicate the correct C compiler:

```bash
[prompt]$ CC=/path/to/C/compiler cmake -DSLURM_PREFIX=/path/where/slurm/is/installed ..
```

For the sake of installation of the utility via CMake, the base install path should be provided:

```bash
[prompt]$ CC=/path/to/C/compiler cmake -DSLURM_PREFIX=/path/where/slurm/is/installed \
          -DCMAKE_INSTALL_PREFIX=/path/wherein/to/install ..
```

If no install path is provided, it defaults to `/usr/local`.  The build can then be accomplished using `make`

```bash
[prompt]$ make
[ 50%] Building C object CMakeFiles/snodelist.dir/snodelist.c.o
[100%] Linking C executable snodelist
[100%] Built target snodelist
```

and installation can be effected (possibly with `sudo` if the destination is not writable by you)

```bash
[prompt]$ make install
[100%] Built target snodelist
Install the project...
-- Install configuration: ""
-- Installing: /path/wherein/to/install/bin/snodelist
-- Set non-toolchain portion of runtime path of "/path/wherein/to/install/bin/snodelist" to "/path/where/slurm/is/installed/lib"
```

Note the `bin` directory is appended to the install path for the location at which the utility is installed.
