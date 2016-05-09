#include <stdio.h>
#include <time.h>
#include <string.h>
#include <strings.h>
#include <limits.h>
#include <unistd.h>
#include <stdlib.h>
#include <pwd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>
#include <errno.h>
#include <glob.h>
#include <pthread.h>
#include <fcntl.h>
#include "sh.h"
#include "mylib.h"
#include "mythreads.h"
#include <sys/param.h>
#ifdef HAVE_KSTAT
#include <kstat.h>
#endif
#include <utmpx.h>
#define max_buf_size 1024
#define tok_buf_size 64
#define MAX_SIZE 2048
#define command_list_size 16


float warnload_num = 0.0;
struct watchuser_list *watchuser_head = NULL; //initialize head node for watchuser list
struct watchuser_list *watchuser_tail = NULL; //initialize tail node for watchuser list
struct watchmail_list *watchmail_head = NULL; //initialize head node for watchuser list
struct watchmail_list *watchmail_tail = NULL; //initialize tail node for watchuser list

pthread_mutex_t watchuser_lock;


int sh( int argc, char **argv, char **envp ){
  //initialize everything
  char *prompt = calloc(PROMPTMAX, sizeof(MAX_SIZE));   
  char *commandline = calloc(MAX_CANON, sizeof(char));
  char *command, *arg, *commandpath, *p, *pwd, *owd;
  char **args = calloc(MAXARGS, sizeof(char*));
  char **args_wild = calloc(max_buf_size, sizeof(char*));
  int uid, i, status, argsct, go = 1;
  struct passwd *password_entry;
  char *homedir;
  struct pathelement *pathlist;
  char * prev_enviornment = malloc(sizeof(prev_enviornment)); 
  int noclobber = 0;
  
  //list of built in commands for the shell
  const char *command_list[command_list_size] = {"which", "where", "exit", "cd", "kill", "pid", "printenv", "setenv", "prompt", "history", "list", "alias", "pwd", "warnload", "watchuser", "watchmail"};

  uid = getuid();
  password_entry = getpwuid(uid); //get passwd info 
  homedir = password_entry->pw_dir;	//Home directory to start out with

  if ( (pwd = getcwd(NULL, PATH_MAX+1)) == NULL )
  {
    perror("getcwd");
    exit(2);
  }
  owd = calloc(strlen(pwd) + 1, sizeof(char));
  memcpy(owd, pwd, strlen(pwd));
  prompt[0] = ' '; prompt[1] = '\0';

  // Put PATH into a linked list
  pathlist = get_path();

  //initialize struct for alias linked list
  struct alias_list *alias = NULL; 

  //checks if a warnload thread has already been created 
  int warnload_thread_check = 0;

  //checks if watchuser thread has already been created
  int watchuser_thread_check = 0;

  pthread_mutex_unlock(&watchuser_lock);

  //register child sig handler with sigaction

  struct sigaction s;

  s.sa_handler = &sig_child_handler;

  sigemptyset(&s.sa_mask);

  s.sa_flags = SA_RESTART | SA_NOCLDSTOP;

  if(sigaction(SIGCHLD, &s, 0) == -1){

    perror(0);

    exit(1);

  }



  while ( go ){
    //check to see if calling pipe function
    int found_pipe = 0;

    //resetting args to all NULL
    int j = 0;
    while(args[j] != NULL){ 
      args[j] = NULL;
      j++;
    }
    //handle ctrl Z and C signals
    signal(SIGINT, sigintHandler);
    signal(SIGTSTP, signalSTPHandler);

    //initialize command_line varible
    char *command_line = calloc(MAX_CANON, sizeof(char));
    char *command_line_temp = calloc(MAX_CANON, sizeof(char));

    //initialize and get current working directoy
    char cwd1[max_buf_size];
    getcwd(cwd1, sizeof(cwd1));
    printf("%s [%s]>", prompt, cwd1); //print the updating current working directory and prompt for shell

    //fgets below handles input into shell. The if statmatent tells the user that ctrl-D is not allowed to be used to exit.
    if (fgets(command_line, MAX_CANON, stdin) == NULL) {
      printf("\nTo quit, please use the exit command: exit.\n");
    }

    //handles when just enter is hit
    if(strcmp(command_line, "\n") == 0){
      continue;
    }
    int arg_count;
    rmv_new_line(command_line); //format command line
    //add_history(command_line); //add command to history
    if((strstr(command_line, "|") != NULL) || (strstr(command_line, "|&") != NULL)){
      found_pipe = 1;
    }
    arg_count = parse(command_line, args); //parse command line into args

/* 

Start of the checks for build in commands.

*/  
    //checks if the argmument is exit. If it is, exit from the shell
    if (strcmp(args[0], "exit") == 0) {
      if (arg_count > 1){
        fprintf(stderr, "Incorrect number of arguments for %s function.\n", args[0]);
      }
      else{
        printf("Executing built-in %s\n", args[0]);
        shell_exit();
      }
    }

    //checks if the argmument is cd. If it is, change the directory. 
    else if (strcmp(args[0], "cd") == 0){
      char cwd[max_buf_size];
      //get current directory to add args to
      getcwd(cwd,sizeof(cwd));
      if (arg_count == 1){
        printf("Executing built-in %s\n", args[0]);
        strcpy(prev_enviornment, cwd);
        cd(getenv("HOME"), prev_enviornment);
      }
      else if(arg_count == 2){
        printf("Executing built-in %s\n", args[0]);
        char *temp = malloc(1024);
        strcpy(temp, prev_enviornment);
        if (chdir(args[1]) != -1 || args[1] == "-"){
          strcpy(prev_enviornment, cwd);
        }
        cd(args[1], temp);
      }
      else{
        fprintf(stderr, "Incorrect number of arguments for %s function.\n", args[0]);
      }
    }

    //checks if the argmument is kill. If it is, kill the current working process.
    else if (strcmp(args[0], "kill") == 0){
      printf("Executing built-in %s\n", args[0]);
      if (arg_count == 1){
        fprintf(stderr,"Please enter what to kill.\n");
      }
      else {
        kill_process(args, arg_count);
      }
    }

    //checks if the argmument is pid. If it is, prints the pid number.
    else if (strcmp(args[0], "pid") == 0){
      if(arg_count > 1){
        fprintf(stderr, "Incorrect number of arguments for %s function.\n", args[0]);
      }
      else{
        printf("Executing built-in %s\n", args[0]);
        printpid();
      }
    }

    //checks if the argmument is list. If it is, list the files in the directory, or a given directory.
    else if (strcmp(args[0], "list") == 0){
      printf("Executing built-in %s\n", args[0]);
      if (arg_count == 1){
        char cwd[max_buf_size];
        getcwd(cwd, sizeof(cwd));
        printf("%s: \n", cwd);
        list(cwd);
      }
      else{
        printf("Executing built-in %s\n", args[0]);
        int i;
        for(i = 1; i < arg_count; i++){
          printf("%s: \n", args[i]);
          list(args[i]);
        }
      }
    }

    //checks if the argmument is pwd. If it is, print the working directory.
    else if(strcmp("pwd", args[0]) == 0){
      if (arg_count == 1){
        printf("Executing built-in %s\n", args[0]);
        printwd();
      }
      else{
        fprintf(stderr, "Incorrect number of arguments for %s function.\n", args[0]);
      }
    }

    /*

    WARNLOAD

    */

    else if(strcmp("warnload", args[0]) == 0){
      printf("Execuiting built-in %s\n", args[0]);
      pthread_t tid1;
      float a;
      if (arg_count == 2) {
         a = atof(args[1]);   //convert warnload value from string to float
         warnload_num = a;
         if (a == 0.0) {
          warnload_thread_check = 0;  //reset warnload check so we can create a new thread
         }
         else{
             if (warnload_thread_check == 0){
              pthread_create(&tid1, NULL, warnload, "Warnload Thread");   //create thread that runs warnload function
              warnload_thread_check = 1;    //reset warnload so we cant create anymore threads
             }
         }
      }
      else if (arg_count < 2){
        fprintf(stderr, "Please enter a valid warnload value.\n");
      }
      else {
        fprintf(stderr, "error: Too many arguments\n");
      }
   }
    /*

    WATCHUSER

    */

    else if(strcmp("watchuser", args[0]) == 0){
    	printf("Executing built-in %s\n", args[0]);
    	pthread_t tid1;
    	if (arg_count == 2){
    		if (watchuser_thread_check == 0){
    			pthread_create(&tid1, NULL, watchuser, "Watchuser Thread");
          watchuser_thread_check = 1;
    		}
        pthread_mutex_lock(&watchuser_lock);
    		add_user(args[1]);
        pthread_mutex_unlock(&watchuser_lock);
    	}
    	else if (arg_count == 3){
	     if (strcmp(args[2], "off") == 0){
         pthread_mutex_lock(&watchuser_lock);
	       delete_user(watchuser_head, args[1]);
         pthread_mutex_unlock(&watchuser_lock);
       }
	  else
	    fprintf(stderr, "Error: Invalid command\n");
    	}
    	else if (arg_count > 3){
    		fprintf(stderr, "Error: Too many arguments\n");
    	}
    	else {
    		struct watchuser_list *head1 = watchuser_tail;
    		while(head1!=NULL){
			    printf("%s\n", head1->node);
			    head1=head1->prev;
			  }
    	}
    }

    /*

    WATCHMAIL

    */

    else if(strcmp("watchmail", args[0]) == 0){
    	printf("Executing built-in %s\n", args[0]);
    	if (arg_count == 1){
    		fprintf(stderr, "Error: Not enough arguments.\n");
    	}
    	else if (arg_count == 2){
    		//open a file and see if it exits
    		if (access(args[1], F_OK) != -1){
    			//file does exist
          add_mail(args[1]);
    			pthread_t tid3;
    			pthread_create(&tid3, NULL, watchmail, args[1]);
    		}
        else{
          printf("Can't access file.\n");
        }
    	}
    	else if (arg_count == 3){
        //check to see if trying to stop watching mail
    		if (strcmp(args[2], "off") == 0){
          //delete from linked list
    			delete_mail(watchmail_head, args[1]);
    		}
    		else{
    			fprintf(stderr, "Error: Third argument must be off.\n");
    		}

    	}
    	else{
    		fprintf(stderr, "Error: too many arguments.\n");
    	}
    }

    else if(strcmp(args[0],"noclobber") == 0){
      if(arg_count > 1){
        fprintf(stderr, "Error: too many arguments.\n");
      }
      //reset noclobber to opposite value than before
      if(noclobber == 1){
        noclobber = 0;
      }
      else{
        noclobber = 1;
      }
      printf("noclobber = %d\n", noclobber);
    }
    //Check for support
    //Run when the argument is not a built in command.
    //This forks the process and creates a child. This allows the process to be run on the terminal through the shell.
    else{
      pid_t pid;
      char* p = args[0];
      if (access(args[0], F_OK) == -1){
        p = which(args[0], pathlist);
      }
      //doesn't fork if process isn't found by which
      if (strcmp(p,"") == 0){
        printf("\n");
      }
      //check to see if a piping symbol was found and run piping command
      else if(found_pipe == 1){
        pipemain(args);
      }
      //process is found by which and we can fork
      else if (strcmp(p,"") != 0){
        pid = fork();
        int status = 0;
        int background = 0;

        /*CHECKING FOR &*/
        int ampCheck = 0;
        if(strcmp("&", args[arg_count - 1]) == 0){
        	ampCheck = 1;  //found &
        }
        //found & and reset args
        if (ampCheck == 1){
        	background = 1;
        	args[arg_count - 1] = NULL;
        	arg_count -= 1;
        }
        if (pid == 0){
          //checking for redirection
          int redir = redirection(args,cwd1,noclobber);
          if(redir == 0){
            kill(getpid(),SIGKILL);
          }
          //if redirection is found
          else{
            //reset args as appropriate so we can exec
            char *cur_arg = args[0];
            int i = 0;
            while (cur_arg != NULL){
              if ((strcmp(cur_arg, ">") == 0) || (strcmp(cur_arg, ">&") == 0) || (strcmp(cur_arg, ">>") == 0) || (strcmp(cur_arg, ">>&") == 0) || (strcmp(cur_arg, "<") == 0)){
                args[i] = NULL;
                args[i+1] = NULL;
                break;
              }
              else{
                i++;
                cur_arg = args[i];
              }
            }
          }
          //run child background process
          if (background) {
          	fclose(stdin);
          	fopen("/dev/null", "r"); //open empty stdin
          	execvp(*args, args);   //exec based on args that was changed when & found
          	fprintf(stderr, "unknown command: %s\n", args[0]);
          	exit(1);
          }
          else {
            //exec if anything else
          	execvp(*args, args);
          	exit(1);
          }
        }
        else {
    			if (background) {
    				printf("starting background job %d\n", pid);			
    			} 
    			else { 
    				waitpid(pid, &status, 0);
    			} 
        }
      }
    }    
  } 
}

static void *watchuser(void *param){
	const char *name = param;
	struct utmpx *up;
	while(1){
    sleep(30);
	  setutxent();
	  while(up = getutxent() ){
	    if (up->ut_type == USER_PROCESS){
        pthread_mutex_lock(&watchuser_lock);
	    	struct watchuser_list *temp = watchuser_tail;
	    	while(temp !=NULL){
	      	if (strcmp(temp->node, up->ut_user) == 0){
					 printf("%s had logged on %s from %s\n", up->ut_user, up->ut_line, up->ut_host);
	    		}
	    	  temp=temp->prev;
	    	}
        pthread_mutex_unlock(&watchuser_lock);
	    }
	  }
	}
}

//adds new file to watch to the tail of the watchmail list 
void add_mail(char *file_name){
  //set up temporary name for new node
  struct watchmail_list *new_node = malloc(sizeof(struct watchmail_list));
  new_node->node = malloc(1024);
  //put file name as the name of the node
  strcpy(new_node->node, file_name);
  //set next and prev appropriately to add to tail
  new_node->next = NULL;
  new_node->prev = watchmail_tail;
  //if there is a watchmail tail, new node is the tail's next
  if(watchmail_tail){
    watchmail_tail->next = new_node;
  }
  //reset tail to new node
  watchmail_tail = new_node;
  //if it's the first thing added, make it the head
  if(watchmail_head == NULL){
    watchmail_head = new_node;
  }
  return;
}

void delete_mail(struct watchmail_list *p, char *name){
  //set up temp
  struct watchmail_list* temp;
  //if empty watchmail list
  if (p == NULL){
    printf("\nNo enteries found\n");
    return;
  }
  //if p is the only item in the list
  if((p == watchmail_head) && (p == watchmail_tail)){
    if(strcmp(p->node, name) == 0){
      watchmail_head = NULL;
      watchmail_tail = NULL;
      printf("Deleted %s from watchmail list.\n", name);
      return;
    }
  }
  //if multiple items in list
  while (p != NULL){
    if (strcmp(p->node, name) == 0){
      temp = p->next; 
      //reassign pointers as appropriate
      if(watchmail_head == p)   
        watchmail_head = p->next;
      if(watchmail_tail == p) 
        watchmail_tail = p->prev;
      if(p->prev)
        p->prev->next = p->next;
      if(p->next)
        p->next->prev = p->prev;
      p->prev = p->next = NULL;
      free(p);        //free the item
      p = temp;       //move to next item
    }
    else{
      p = p->next;
    }
  }
  printf("Deleted %s from watchmail list.\n", name);
  return;
}

static void *watchmail(void *param){
  //set up file name
	const char *file_name = param;
  struct tm* ptm;
  //define struct so we can find the size of the file
	struct stat file_Stat;
  //define struct so we can get the time
	struct timeval TIME;
  char time_string[40];
 	int prev_file_Size = -1;
	while(1){
    struct watchmail_list *temp = watchmail_head;
    int found = 0;
    while(temp != NULL){
      if(strcmp(temp->node,file_name) == 0){
        found = 1;
        break;
      }
      else{
        temp = temp->next;
      }
    }
    if(found == 1){
  		stat(file_name, &file_Stat);
  		if (prev_file_Size == -1){
  			prev_file_Size = file_Stat.st_size;
  		}
  		if (file_Stat.st_size > prev_file_Size){
  			gettimeofday(&TIME, NULL);
        ptm = localtime (&TIME.tv_sec);
        strftime (time_string, sizeof (time_string), "%Y-%m-%d %H:%M:%S", ptm);
  			printf("BEEP\a You've Got Mail in %s at %s\n", file_name, time_string);
  			prev_file_Size = file_Stat.st_size;
  		}
  		sleep(1);
    }
    if(found == 0){
      pthread_exit(&exit);
    }
	}
}

void add_user(char* command){
  struct watchuser_list *new_node = malloc(sizeof(struct watchuser_list));
  new_node->node = malloc(1024);
  strcpy(new_node->node, command);
  new_node->next = NULL;
  new_node->prev = watchuser_tail;
  if(watchuser_tail){
    watchuser_tail->next = new_node;
  }
  watchuser_tail = new_node;
  if(!watchuser_head){
    watchuser_head = new_node;
  }
}


void delete_user(struct watchuser_list *p, char *user_name){
  struct watchuser_list* temp;
  if (p == NULL){
    printf("\nNo enteries found\n");
  }
  if(p == watchuser_head && p == watchuser_tail){   //if p is the only thing in the list
    if(strcmp(p->node, user_name) == 0){
      watchuser_head = NULL;
      watchuser_tail = NULL;
    }
  }
  while (p != NULL){
    if (strcmp(p->node, user_name) == 0){
      temp = p->next; 
      if(watchuser_head == p)       //reassign pointers as appropriate
        watchuser_head = p->next;
      if(watchuser_tail == p) 
        watchuser_tail = p->prev;
      if(p->prev)
        p->prev->next = p->next;
      if(p->next)
        p->next->prev = p->prev;
      p->prev = p->next = NULL;
      free(p);        //free the item
      p = temp;       //move to next item
    }
    else{
      p = p->next;
    }
  }
  printf("Deleted %s from watchuser list.\n", user_name);
}

static void *warnload(void *param){
  int i = 0;
  int exit = -1;
  double loads[3];
  const char *name=param;
  while (1){
    get_load(loads);
    if (warnload_num == 0.0){
      pthread_exit(&exit);
    }
    if ((loads[0]/100) > warnload_num){
      printf("Warning load level is %.2lf\n", loads[0]/100);
    }
    sleep(30);
  }
}

int get_load(double *loads){
#ifdef HAVE_KSTAT
 kstat_ctl_t *kc;
 kstat_t *ksp;
 kstat_named_t *kn;  kc = kstat_open();
 if (kc == 0)
 {
   perror("kstat_open");
   exit(1);
 }  ksp = kstat_lookup(kc, "unix", 0, "system_misc");
 if (ksp == 0)
 {
   perror("kstat_lookup");
   exit(1);
 }
 if (kstat_read(kc, ksp,0) == -1)
 {
   perror("kstat_read");
   exit(1);
 }  kn = kstat_data_lookup(ksp, "avenrun_1min");
 if (kn == 0)
 {
   fprintf(stderr,"not found\n");
   exit(1);
 }
 loads[0] = kn->value.ul/(FSCALE/100);  kn = kstat_data_lookup(ksp, "avenrun_5min");
 if (kn == 0)
 {
   fprintf(stderr,"not found\n");
   exit(1);
 }
 loads[1] = kn->value.ul/(FSCALE/100);  kn = kstat_data_lookup(ksp, "avenrun_15min");
 if (kn == 0)
 {
   fprintf(stderr,"not found\n");
   exit(1);
 }
 loads[2] = kn->value.ul/(FSCALE/100);  kstat_close(kc);
 return 0;
#else
 /* yes, this isn't right */
 loads[0] = loads[1] = loads[2] = 0;
 return -1;
#endif
} /* get_load() */

int redirection(char **args, char *cur_dir, int noclobber){
  int i = 0;
  char *cur_arg = args[0];
  while (cur_arg != NULL){
    if ((strcmp(cur_arg, ">") == 0) || (strcmp(cur_arg, ">&") == 0) || (strcmp(cur_arg, ">>") == 0) || (strcmp(cur_arg, ">>&") == 0) || (strcmp(cur_arg, "<") == 0)){
      break;
    }
    else{
      i++;
      cur_arg = args[i];
    }
  }
  if(args[i+1] != NULL){
    if (args[i+1][0] != '/'){
      char *tmp = malloc(256);
      strcpy(tmp,cur_dir);
      strcat(tmp,"/");
      strcat(tmp,args[i+1]);
      strcpy(args[i+1],tmp);
    }

    int fdes;

    if(strcmp(cur_arg, ">") == 0){
      //printf("redirecting\n");
      if(access(args[i+1],F_OK != -1) && (noclobber == 1)){
        printf("Can't overwrite existing file if noclobber is turned on.\n");
        return 0;
      }
      fdes = open(args[i+1], O_CREAT| O_WRONLY| O_TRUNC, S_IRWXU);
      close(1);
      dup(fdes);
      close(fdes);
      return 1;
    }

    else if(strcmp(cur_arg, ">&") == 0){
      if(access(args[i+1],F_OK != -1) && (noclobber == 1)){
        printf("Can't overwrite existing file if noclobber is turned on.\n");
        return 0;
      }
      fdes = open(args[i+1], O_CREAT| O_WRONLY| O_TRUNC, S_IRWXU);
      close(1);
      close(2);
      dup(fdes);
      dup(fdes);
      close(fdes);
      return 1;
    }

    else if(strcmp(cur_arg, ">>") == 0){
      if(access(args[i+1],F_OK == -1) && (noclobber == 1)){
        printf("Can't overwrite or create files with >> if noclobber is turned on.\n");
        return 0;
      }
      if (access(args[i+1],F_OK) != -1){
        fdes = open(args[i+1], O_RDWR | O_APPEND, S_IRUSR | S_IWUSR);
      }
      else{
        fdes = open(args[i+1], O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
      }
      close(1);
      dup(fdes);
      close(fdes);
      return 1;
    }

    else if(strcmp(cur_arg, ">>&") == 0){
      if(access(args[i+1],F_OK == -1) && (noclobber == 1)){
        printf("Can't overwrite or create files with >>& if noclobber is turned on.\n");
        return 0;
      }
      if (access(args[i+1],F_OK) != -1){
        fdes = open(args[i+1], O_RDWR | O_APPEND, S_IRUSR | S_IWUSR);
      }
      else{
        fdes = open(args[i+1], O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
      }
      close(1);
      close(2);
      dup(fdes);
      dup(fdes);
      close(fdes);
      return 1;
    }

    else if(strcmp(cur_arg, "<") == 0){
      fdes = open(args[i+1], O_RDWR | S_IRUSR | S_IWUSR);
      close(0);
      dup(fdes);
      close(fdes);
      return 1;
    }
    fdes = open("/dev/tty",O_WRONLY);
  }
  return 1;
}

void source(int pfd[], char **cmd, char *symbol){
  int pid;
  pid = fork();
  switch(pid){
    //child
    case 0:
      //checks to see if needs to pipe stderr
      if(strcmp(symbol, "|&") == 0){
        close(2);
      }
      close(1);
      dup(pfd[1]);
      close(pfd[0]);
      execvp(cmd[0], cmd);
      perror(cmd[0]);
    //parent
    default:
      break;
    //error
    case -1:
      perror("fork"); 
      exit(1);
  }
}

void dest(int pfd[], char **cmd, char *symbol){
  int pid;
  pid = fork();
  switch(pid){
    //child
    case 0:
      //check to so if needs to pipe stderr
      if(strcmp(symbol, "|&") == 0){
        close(2);
      }
      close(0);
      dup(pfd[0]);
      close(pfd[1]);
      execvp(cmd[0], cmd);
      perror(cmd[0]);
    //parent
    default:
      break;
    //error
    case -1:
      perror("fork"); 
      exit(1);
  }
}

void pipemain(char **args){
  char **cmd1 = calloc(MAXARGS, sizeof(char*)); //first command for piping
  char **cmd2 = calloc(MAXARGS, sizeof(char*)); //second command for piping
  char *symbol = calloc(MAX_CANON, sizeof(char*));
  int i = 0;  //used for iterating through args
  int j = 0;  //used for adding to cmd2
  int found = 0;
  //separate commands
  while(found == 0){
    if((strcmp(args[i], "|") == 0) || (strcmp(args[i], "|&") == 0)){
      symbol = args[i];
      i++;
      found = 1;
      break;
    }
    else{
      cmd1[i] = args[i];
      i++;
    }
  }
  while(args[i] != NULL){
    cmd2[j] = args[i];
    i++;
    j++;
  }
  //run piping
  int pid;
  int status;
  int fdes[2];
  pipe(fdes);
  source(fdes, cmd1, symbol);
  dest(fdes, cmd2, symbol);
  close(fdes[0]);
  close(fdes[1]);
  //pick up dead children
  while ((pid = wait(&status)) != -1){
    fprintf(stderr, "process %d exits with %d\n", pid, WEXITSTATUS(status)); 
  }
  //exit(0);
}



void sig_child_handler(int signal){
  int saved_error = errno;
  while(waitpid((pid_t)(-1), 0, WNOHANG) > 0){}
  errno = saved_error;
}

void cd(char *pth, char *prev){
  //append our path to current path
  char *path = malloc(sizeof(*path));
  strcpy(path,pth);
  char cwd[max_buf_size];
  //get current directory to add args to
  getcwd(cwd,sizeof(cwd));
  if (strcmp(path, "..") == 0){
    //printf("%s\n", cwd);
    chdir(cwd);
    return;
  }
  //create string to check
  strcat(cwd,"/");
  if(strcmp(path, "-")==0){
    //strcat(cwd, "..");
    chdir(prev);
    return;
  }
  //if directory is passed in as a whole path form
  if(strstr(path,"/")){
    if(chdir(path)!=0){
      chdir(path);
    }
    return;
  }
  //if not passed in whole path form
  strcat(cwd, path);
  int exists = chdir(cwd);
  if(exists != 0){
    //perror("cd");
  }
  else{
    chdir(cwd);
  }
}