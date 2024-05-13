/**
 * \file
 * \brief Low-level example of communication.
 * \author Adam Lackorzynski <adam@os.inf.tu-dresden.de>
 *
 * This example shows how two threads can exchange data using the L4 IPC
 * mechanism. One thread is sending an integer to the other thread which is
 * returning the square of the integer. Both values are printed.
 */
/*
 * (c) 2008-2009 Author(s)
 *     economic rights: Technische Universität Dresden (Germany)
 *
 * This file is part of TUD:OS and distributed under the terms of the
 * GNU General Public License 2.
 * Please see the COPYING-GPL-2 file for details.
 */
#include <bits/time.h>
#include <l4/sys/ipc.h>
#include <l4/sys/scheduler>
#include <l4/re/env>
#include <l4/re/util/cap_alloc>
#include <pthread-l4.h>
#include <unistd.h>
#include <stdio.h>
#include <time.h>

static pthread_t t2;

static void *thread1_fn(void *arg)
{
  l4_msgtag_t tag;
  int ipc_error;
  const unsigned BENCH_SIZE = 100000;
  unsigned long long tsc_sum, tsc_start;
  struct timespec tp, ts_total;
  ts_total.tv_nsec = 0;
  ts_total.tv_sec = 0;
  tsc_sum = 0;
  

  (void)arg;

  for (unsigned i = 0; i < BENCH_SIZE; ++i)
    {
      // Measure time once before running the IPC
      clock_gettime(CLOCK_MONOTONIC, &tp);
      tsc_start = __builtin_ia32_rdtsc ();
      // Do the IPC
      tag = l4_ipc_call(pthread_l4_cap(t2), l4_utcb(),
                        l4_msgtag(0, 0, 0, 0), L4_IPC_NEVER);
      // Meausre time again
      tsc_sum += __builtin_ia32_rdtsc () - tsc_start;
      ipc_error = l4_ipc_error(tag, l4_utcb());
      if (ipc_error) {
        // Report errors, if any occur. In this case we try to redo the current 
        // run
        fprintf(stderr, "thread1: IPC error: %x\n", ipc_error);
        --i;
      }
      else {
        // If we received a message, we read the time stamp the second thread 
        // sent to us
        if (tp.tv_nsec > (long) l4_utcb_mr()->mr[1])
        {
          ts_total.tv_sec += l4_utcb_mr()->mr[0] + 1 - tp.tv_sec;
          ts_total.tv_nsec += l4_utcb_mr()->mr[1] + 1000000000 - tp.tv_nsec;
        } else {
          ts_total.tv_sec += l4_utcb_mr()->mr[0] - tp.tv_sec;
          ts_total.tv_nsec += l4_utcb_mr()->mr[1] - tp.tv_nsec;
        } 
      }

      // Reset message registers to be safe.
      l4_utcb_mr()->mr[0] = 0;
      l4_utcb_mr()->mr[1] = 0;
    }

  // Calculate benchamrk results...
  auto seconds_as_ns = ts_total.tv_sec * 1000000000;
  printf("Total l4_ipc_call() time: %5lu.%09lus\n", ts_total.tv_sec, ts_total.tv_nsec);
  printf("AVG l4_ipc_call() time  : %5lu.%09lus\n",
     seconds_as_ns / 1000000000 / BENCH_SIZE,
     (ts_total.tv_nsec + seconds_as_ns % 1000000000) / BENCH_SIZE);
  printf("AVG IPC Roundtrip cycles: %15llu\n", tsc_sum / BENCH_SIZE);
  return NULL;
}

/* Thread2 is in the server role, i.e. it waits for requests from others and
 * sends back the receive time stamp. */
static void *thread2_fn(void *arg)
{
  l4_msgtag_t tag;
  l4_umword_t label;
  int ipc_error;
  (void)arg;

  /* Wait for requests from any thread. No timeout, i.e. wait forever. */
  tag = l4_ipc_wait(l4_utcb(), &label, L4_IPC_NEVER);
  while (1)
    {
      /* Check if we had any IPC failure, if yes, print the error code
       * and just wait again. */
      ipc_error = l4_ipc_error(tag, l4_utcb());
      if (ipc_error)
        {
          fprintf(stderr, "thread2: IPC error: %x\n", ipc_error);
          tag = l4_ipc_wait(l4_utcb(), &label, L4_IPC_NEVER);
          continue;
        }
      // Get the current time and send it to thread 1
      struct timespec tp;
      clock_gettime(CLOCK_MONOTONIC, &tp);
      l4_utcb_mr()->mr[0] = tp.tv_sec;
      l4_utcb_mr()->mr[1] = tp.tv_nsec;

      /* Send the reply and wait again for new messages.
       * The '2' in the msgtag indicated that we want to transfer 2 words in
       * the message registers (i.e. MR0) */
      tag = l4_ipc_reply_and_wait(l4_utcb(), l4_msgtag(0, 2, 0, 0),
                                  &label, L4_IPC_NEVER);
    }
  return NULL;
}

int main(void)
{
  // We will have two threads, one is already running the main function, the
  // other (thread2) will be created using pthread_create. Both will be run on
  // different CPUs.

  // Create the second thread
  if (pthread_create(&t2, NULL, thread2_fn, NULL))
  {
    fprintf(stderr, "Thread creation failed\n");
    return 1;
  }
  
  // Schedule the thread on the second CPU
  l4_sched_param_t sp = l4_sched_param(20);
  unsigned c = 1;
  sp.affinity = l4_sched_cpu_set(c, 0);
  if (l4_error(L4Re::Env::env()->scheduler()->run_thread(L4::Cap<L4::Thread>(pthread_l4_cap(t2)), sp)))
    printf("Error migrating thread %p to CPU: %02d\n", t2, c);
  printf("Migrated Thread %p -> CPU: %02d\n", t2, c);
  

  // Just run thread1 in the main thread
  thread1_fn(NULL);
  return 0;
}
