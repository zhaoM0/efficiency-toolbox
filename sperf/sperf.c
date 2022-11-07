#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <regex.h>
#include <time.h>

// #define LOCAL_MACHINE
#define BUFSIZE       512
#define MAXSYSCALL    512
#define n_rule          2

#ifdef LOCAL_MACHINE
  #define debug(...) printf(__VA_ARGS__)
#else
  #define debug(...) 
#endif

enum { MATCH_SUCCESS = 0, MATCH_FAILURE, MATCH_EXIST };

void show_strace_out();

/* statistic message. */
static struct pattern {
  char syscall_name[32];
  double       use_time;
  size_t      call_time;
} stat_mes[MAXSYSCALL];

static size_t total = 0;

/******************** regex expr setting **********************/
static const char *rules[n_rule] = { "^\\w+", "<[0-9.]+>" };
static regex_t re[n_rule];

void init_regex() {
  int ret, i;
  char error_msg[128];
  for (i = 0; i < n_rule; i++) {
    ret = regcomp(&re[i], rules[i], REG_EXTENDED);
    if (ret != 0) {
      regerror(ret, &re[i], error_msg, 128);
      printf("regex compilation failed: %s\n%s\n", error_msg, rules[i]);
    }
  }
}

int match_regex(char *lbuf, int n) {
  regmatch_t pmatch;
  size_t match_idx = 0;
  char bigram[2][32];           /* [0]: syscall name, [1]: use time */

  debug("[LINE][%3d]: %s", n, lbuf);
   
  for (int offset = 0; offset < n; offset++) {       /* match regex */
    int status = regexec(&re[match_idx], lbuf + offset, 1, &pmatch, 0);

    if (status == 0 && pmatch.rm_so == 0) {
      char *match_str_start = lbuf + offset;
      int match_str_len = pmatch.rm_eo; 

      strncpy(bigram[match_idx], match_str_start, match_str_len);
      bigram[match_idx][match_str_len] = '\0';

      match_idx += 1;
      offset    += match_str_len - 1;
    }
    if (match_idx == 2) break;
  }

  if (match_idx == 1) return MATCH_EXIST;
  debug("match bigram (%s, %s)\n", bigram[0], bigram[1]);

  /* insert record */
  bigram[1][strlen(bigram[1]) - 1] = '\0';

  for (int i = 0; i < total; i++) {
    if (strcmp(bigram[0], stat_mes[i].syscall_name) == 0) {
      stat_mes[i].use_time  += atof(bigram[1] + 1);
      stat_mes[i].call_time += 1;
      return MATCH_SUCCESS;
    }
  }
  /* without syscall record, add a new node */
  strcpy(stat_mes[total].syscall_name, bigram[0]);
  stat_mes[total].call_time  = 1;
  stat_mes[total++].use_time = atof(bigram[1] + 1);
  
  return MATCH_SUCCESS;
}

int cmp(const void *x, const void *y) {
  double x_time = ((struct pattern *)x)->use_time;
  double y_time = ((struct pattern *)y)->use_time;
  if ((x_time - y_time) < 0) 
    return 1;
  else if ((x_time - y_time) == 0)
    return 0;
  else 
    return -1;
}

static void show_stat() {
  // sort 
  qsort(stat_mes, total, sizeof(stat_mes[0]), cmp);

  double cost_time = 0;
  for (int i = 0; i < total; i++) {
    cost_time += stat_mes[i].use_time;
  }

  static size_t flag = 0;
  static size_t pre_total = 0;
  if (flag++ != 0) {
    printf("\033[%zuA", pre_total + 2);
  }

  // show
  printf("Syscall \t\tCost(s)\t\tCall\tPercent(%%)\n");
  printf("===========================================================\n");
  for (int i = 0; i < total; i++) {
    printf("[%-16s]\t%-12lf\t%-8zu%-8.2lf\n", \
                                    stat_mes[i].syscall_name, \
                                    stat_mes[i].use_time, \
                                    stat_mes[i].call_time, \
                                    (stat_mes[i].use_time / cost_time) * 100);
  }
  pre_total = total;
}

void show_args(int argc, char *argv[]) {
  if (argc) {
    printf("[%d] arg is %s\n", argc, *argv);
    show_args(argc - 1, argv + 1);
  }
}


int main(int argc, char *argv[]) {
  int pipefd[2];      /* 0 is read end and 1 is write end */
  pid_t pid;

  if (pipe(pipefd) == -1) {   /* create a pipe like shell */
    perror("pipe");
    exit(EXIT_FAILURE);
  }
  debug("hold file descriptor: (%d, %d)\n", pipefd[0], pipefd[1]);
  
  if ((pid = fork()) < 0 ) {  
    perror("fork");
    exit(EXIT_FAILURE);
  } 

  if (pid == 0) {                        /* child process */
    char *exec_argv[argc + 2];
    char *exec_envp[] = { "PATH=/bin", NULL, };

    exec_argv[0] = "strace";
    exec_argv[1] = "-T";
    for (int idx = 1; idx < argc; idx++)
      exec_argv[idx + 1] = argv[idx];
    exec_argv[argc + 1] = NULL;

    // show_args(argc + 2, exec_argv); 

    close(pipefd[0]);                      /* redirection */
    dup2(pipefd[1], STDERR_FILENO);
    close(pipefd[1]);

    execve("/bin/strace", exec_argv, exec_envp);
    
  } else {                              /* parent process */ 
    close(pipefd[1]); 
    dup2(pipefd[0], STDIN_FILENO);
    close(pipefd[0]);

    char buf[BUFSIZ];
    int n, ret;
    clock_t before;

    init_regex();             /* compile regex expression */

// refresh: 
//     before = clock();
    
    while((n = read(0, buf, BUFSIZ)) > 0) {
      // write(STDOUT_FILENO, buf, n);
      while (*(buf + n - 1) != '\n' && n > 0) {
        n += read(0, buf + n, BUFSIZ - n);
      }
      *(buf + n) = '\0';

      match_regex(buf, n);     /* parse */ 

      // clock_t difference = clock() - before;
      // int mesc = difference * 1000 / CLOCKS_PER_SEC;
      // if (mesc > 1) { 
      //   show_stat();
      //   goto refresh;
      // }
    }

    show_stat();
  } 
  
  return 0;
}

// int main(int argc, char *argv[]) {
//   char *exec_argv[] = { "strace", "ls", NULL, };
//   char *exec_envp[] = { "PATH=/bin", NULL, };
//   execve("strace",          exec_argv, exec_envp);
//   execve("/bin/strace",     exec_argv, exec_envp);
//   execve("/usr/bin/strace", exec_argv, exec_envp);
//   perror(argv[0]);
//   exit(EXIT_FAILURE);
// }