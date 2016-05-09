#include "get_path.h"
#include "which.h"
#include "where.h"
#include "cd.h"
#include "wildcard.h"
#include "signal.h"
#include "print.h"
#include "enviornment.h"
#include "parse.h"
#include "list.h"
#include "exitkillfree.h"
#define PROMPTMAX 32
#define MAXARGS 10

int sh( int argc, char **argv, char **envp);
static void *watchmail(void *param);
static void *watchuser(void *param);
static void *warnload(void *param);
int get_load(double *loads);

struct watchuser_list{
  char *node;			/* a dir in the path */
  struct watchuser_list *next;		/* pointer to next node */
  struct watchuser_list *prev; /*pointer to previous node*/
  char *user;
  int watch;
};
void add_user(char* command);
void delete_user(struct watchuser_list *p, char *user_name);


struct watchmail_list{
	char *node;
	struct watchmail_list *next;	//pointer to next node//
	struct watchmail_list *prev;	//pointer to previous node//
};
void add_mail(char *file_name);
void delete_mail(struct watchmail_list *p, char *name);

int redirection(char **args, char *cur_dir, int noclobber);

void source(int pfd[], char **cmd, char *symbol);
void dest(int pfd[], char **cmd, char *symbol);
void pipemain(char **args);

void sig_child_handler(int signal);
