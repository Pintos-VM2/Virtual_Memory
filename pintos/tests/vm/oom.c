/* Repeatedly forks processes until the system runs out of memory.
   Ensures that fork() fails cleanly and that resource usage stays
   consistent across repetitions. */

#include <debug.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <syscall.h>
#include <random.h>
#include "tests/lib.h"
#include "tests/main.h"

static const int EXPECTED_DEPTH_TO_PASS = 10;
static const int EXPECTED_REPETITIONS = 10;

static int make_children (void);
static void consume_some_resources (void);
static int NO_INLINE consume_some_resources_and_die (void);

/* Open a number of files (and fail to close them).
   The kernel must free any kernel resources associated
   with these file descriptors. */
static void
consume_some_resources (void)
{
  int fd, fdmax = 126;

  /* Open as many files as we can, up to fdmax.
	 Depending on how file descriptors are allocated inside
	 the kernel, open() may fail if the kernel is low on memory.
	 A low-memory condition in open() should not lead to the
	 termination of the process.  */
  for (fd = 0; fd < fdmax; fd++) {
#ifdef EXTRA2
	  if (fd != 0 && (random_ulong () & 1)) {
		if (dup2(random_ulong () % fd, fd+fdmax) == -1)
			break;
		else
			if (open (test_name) == -1)
			  break;
	  }
#else
		if (open (test_name) == -1)
		  break;
#endif
  }
}

/* Consume some resources, then terminate this process
   in some abnormal way.  */
static int NO_INLINE
consume_some_resources_and_die (void)
{
  consume_some_resources ();
  int *KERN_BASE = (int *)0x8004000000;

  switch (random_ulong () % 5) {
	case 0:
	  *(int *) NULL = 42;
    break;

	case 1:
	  return *(int *) NULL;

	case 2:
	  return *KERN_BASE;

	case 3:
	  *KERN_BASE = 42;
    break;

	case 4:
	  open ((char *)KERN_BASE);
	  exit (-1);
    break;

	default:
	  NOT_REACHED ();
  }
  return 0;
}

static int
make_children (void) {
  int i = 0;
  int pid;
  char child_name[128];
  for (; ; random_init (i), i++) {
    if (i > EXPECTED_DEPTH_TO_PASS/2) {
      snprintf (child_name, sizeof child_name, "%s_%d_%s", "child", i, "X");
      pid = fork(child_name);
      if (pid > 0 && wait (pid) != -1) {
        fail ("crashed child should return -1.");
      } else if (pid == 0) {
        consume_some_resources_and_die();
        fail ("Unreachable");
      }
    }

    snprintf (child_name, sizeof child_name, "%s_%d_%s", "child", i, "O");
    pid = fork(child_name);
    if (pid < 0) {
      exit (i);
    } else if (pid == 0) {
      consume_some_resources();
    } else {
      break;
    }
  }

  int depth = wait (pid);
  if (depth < 0)
	  fail ("Should return > 0.");

  if (i == 0)
	  return depth;
  else
	  exit (depth);
}

void
test_main (void) {
  int first_run_depth = make_children ();
  CHECK (first_run_depth >= EXPECTED_DEPTH_TO_PASS,
         "Spawned at least %d children.", EXPECTED_DEPTH_TO_PASS);

  for (int i = 0; i < EXPECTED_REPETITIONS; i++) {
    int current_run_depth = make_children();
    if (current_run_depth < first_run_depth) {
      fail ("should have forked at least %d times, but %d times forked",
            first_run_depth, current_run_depth);
    }
  }

  msg ("success. Program forked %d iterations.", EXPECTED_REPETITIONS);
}
