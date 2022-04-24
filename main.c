#include <assert.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CACHE_LINE_SZ 64

#define CPU_SOCKS_CNT 64
#define CPU_CORES_CNT 16
#define CPU_CPUS_CNT (CPU_CORES_CNT * CPU_SOCKS_CNT * 2)

#define MAX(a, b) ((b) < (a) ? (a) : (b))

static int cpu_map[CPU_SOCKS_CNT][CPU_CORES_CNT][CPU_CPUS_CNT];
static int cpu_cnts[CPU_SOCKS_CNT][CPU_CORES_CNT] = {};

typedef double real;
static_assert(sizeof(real) <= sizeof(uintptr_t),
              "real must fit into function integer register");

struct worker_state {
  pthread_t id;
  real begin;
  real end;
} __attribute__((aligned(CACHE_LINE_SZ)));

#define DX 1e-7
static const real domain_sz = 10;

static void *
worker(void *state);

static inline real
comp_int_sum_over_range(real begin, real end);

static inline real
integrand(real x);

int
main(int argc, const char *const *argv) {
  errno = 0;
  --argc;
  ++argv;

  if (argc != 1) {
    printf("USAGE: para-int-comp <number of threads>\n");
    return EXIT_SUCCESS;
  }

  long n_workers = strtol(*argv, NULL, 10);
  if (errno != 0) {
    printf("CLIENT ERROR: expected usage: para-int-comp <number of threads>\n");
    return EXIT_FAILURE;
  }

  FILE *lscpu = popen("lscpu -p=socket,core,cpu -y", "r");
  if (lscpu == NULL) {
    perror("popen failed");
    return EXIT_FAILURE;
  }

  char *cpu_info_line = NULL;
  size_t cpu_info_line_len = 0;
  ssize_t chars_read = 0;
  for (size_t i = 0; i < 4 && chars_read != -1; ++i) {
    chars_read = getline(&cpu_info_line, &cpu_info_line_len, lscpu);
    if (chars_read == -1 && errno != 0) {
      perror("getline failed");
      return EXIT_FAILURE;
    }
  }
  assert(chars_read != -1);

  for (size_t i = 0; i < CPU_SOCKS_CNT; ++i) {
    for (size_t j = 0; j < CPU_CORES_CNT; ++j) {
      for (size_t k = 0; k < CPU_CPUS_CNT; ++k) {
        cpu_map[i][j][k] = -1;
      }
    }
  }

  while (true) {
    chars_read = getline(&cpu_info_line, &cpu_info_line_len, lscpu);
    if (chars_read == -1) {
      if (errno != 0) {
        perror("getline failed");
        return EXIT_FAILURE;
      }
      break;
    }
    char *tok = strtok(cpu_info_line, ",");
    assert(tok != NULL);
    long sock = strtol(tok, NULL, 10);
    assert(errno == 0);
    assert(0 <= sock && sock <= CPU_SOCKS_CNT);

    tok = strtok(NULL, ",");
    assert(tok != NULL);
    long core = strtol(tok, NULL, 10);
    assert(errno == 0);
    assert(0 <= core && core <= CPU_CORES_CNT);

    tok = strtok(NULL, "\n");
    assert(tok != NULL);
    long cpu = strtol(tok, NULL, 10);
    assert(errno == 0);
    assert(0 <= cpu && cpu <= CPU_CPUS_CNT);

    cpu_map[sock][core][cpu_cnts[sock][core]++] = (int)cpu;
  }
  assert(cpu_cnts[0][0] > 0);
  int rc = pclose(lscpu);
  if (rc != 0) {
    perror("pclose failed");
    return EXIT_FAILURE;
  }

  int cpus[CPU_CPUS_CNT];
  size_t cpus_cnt = 0;
  for (size_t i = 0; i < CPU_SOCKS_CNT; ++i) {
    size_t prev_cpus_cnt;

    do {
      prev_cpus_cnt = cpus_cnt;
      for (size_t j = 0; j < CPU_CORES_CNT; ++j) {
        if (cpu_cnts[i][j] > 0) {
          assert(cpus_cnt < CPU_CPUS_CNT);
          cpus[cpus_cnt++] = cpu_map[i][j][--cpu_cnts[i][j]];
        }
      }
    } while (prev_cpus_cnt != cpus_cnt);
  }

  struct worker_state *worker_states =
      aligned_alloc(CACHE_LINE_SZ,
                    MAX(n_workers, cpus_cnt) * sizeof(*worker_states));
  if (worker_states == NULL) {
    perror("aligned_alloc failed");
    return EXIT_FAILURE;
  }

  real part_sz = domain_sz / (real)n_workers;
  assert(part_sz > 0);
  for (size_t i = 0; i < MAX(n_workers, cpus_cnt); ++i) {
    worker_states[i].begin = part_sz * (real)i;
    worker_states[i].end = part_sz * (real)(i + 1);
  }
  worker_states[n_workers - 1].end = domain_sz;

  cpu_set_t cpu_set;
  CPU_ZERO(&cpu_set);
  pthread_attr_t attr;
  rc = pthread_attr_init(&attr);
#if 0
  if (rc != 0) {
    errno = rc;
    perror("pthread_attr_init failed");
    return EXIT_FAILURE;
  }
#endif

  for (size_t i = 0; i < MAX(n_workers, cpus_cnt); ++i) {
    CPU_SET(cpus[i % cpus_cnt], &cpu_set);
    rc = pthread_attr_setaffinity_np(&attr, CPU_SETSIZE, &cpu_set);
#if 0
    if (rc != 0) {
      errno = rc;
      perror("pthread_attr_setaffinity_np failed");
      return EXIT_FAILURE;
    }
#endif
    CPU_CLR(cpus[i % cpus_cnt], &cpu_set);

    rc = pthread_create(&worker_states[i].id, &attr, worker, &worker_states[i]);
#if 0
    if (rc != 0) {
      errno = rc;
      perror("pthread_create failed");
      return EXIT_FAILURE;
    }
#endif
  }

  real int_sum = 0;
  for (size_t i = 0; i < n_workers; ++i) {
    real partial_int_sum;
    rc = pthread_join(worker_states[i].id, (void **)&partial_int_sum);
#if 0
    if (rc != 0) {
      errno = rc;
      perror("pthread_join failed");
      return EXIT_FAILURE;
    }
#endif
    int_sum += partial_int_sum;
  }
}

void *
worker(void *state) {
  //for (volatile size_t i = 0; i < 1 << 10; ++i) ;

  struct worker_state *worker_state = state;
  real int_sum =
      comp_int_sum_over_range(worker_state->begin, worker_state->end);
  return (void *)*(uintptr_t *)&int_sum;
}

real
comp_int_sum_over_range(real begin, real end) {
  real int_sum = 0;
  for (real x = begin; x < end; x += DX) {
    int_sum += integrand(x) * DX;
  }
  return int_sum;
}

real
integrand(real x) {
  return cos(pow(x, 5)*sin(atan(x)));
#if 0
  real sum = 0;
  for (size_t i = 0; i < 60; ++i) {
    sum += powl(x, i);
  }
  return sum;
#endif
}
