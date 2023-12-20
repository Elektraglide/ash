#include <stdio.h>
#include <string.h>
#include <errno.h> 
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/signal.h>

#include <sys/dir.h>

extern void exit();
extern int atoi();
extern int kill();
extern char *getenv();

/*

cc -std=c89 ash.c -o ash

TODO:

- need to BS before characters show..
- running login doesn't work (invisible input)
- missing alias command
- missing input/output redirection

*/

#ifdef __clang__
#include <stdlib.h>
#include <sys/termios.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/ioctl_compat.h>
#include <sgtty.h>
struct termios origt,t = {};

#define SIGDEAD SIGCHLD
#define MAXLINELEN 512

#endif

#ifndef __clang__
#include <sys/modes.h>
#include <sys/sgtty.h>
#define MAXLINELEN 128
#endif

struct sgttyb slave_orig_term_settings;

typedef struct {
	char name[20];
} lsentry;

/* pushd/pop dir */
#define MAXPUSH 8
int dstacktop = 0;
char dstack[MAXPUSH][MAXLINELEN];

/* aliases */

/* history processing */
#define MAXHISTORY 1024
#define MAXJOBS 8
int cmdpid[MAXJOBS];
int cmdcount;
char history[MAXHISTORY];
int history_len;
int history_crp;

/* persistent store of expanded tilda */
char homeroot[MAXLINELEN];

int runningtask;

void inithistory()
{
	char filepath[MAXLINELEN];
	int fd;
	
	cmdcount = 0;
	
	history[0] = '\n';
	history_len = 1;
	
	strcpy(filepath, getenv("HOME"));
	strcat(filepath, "/.ash_history");

	fd = open(filepath, O_RDONLY);
	if (fd > 0)
	{
		history_len = read(fd, history+1, MAXHISTORY-1) + 1;
		close(fd);
	}
}

void addhistory(aline)
char *aline;
{
	char *ptr;
	int len;
	
	len = strlen(aline);

	if (history_len + len > MAXHISTORY)
	{
		ptr = history + history_len/2;
		while(*ptr++ != '\n');
		memcpy(history+1, ptr, history_len - (ptr-history));
		history_len -= (ptr-history);
	}

	memcpy(history+history_len, aline, len);
	history_len += len;
	history[history_len++] = '\n';
	history_crp = history_len - 1;
	
#if 0
	printf("history_len = %d\n", history_len);
	for(len=0; len<history_len; len++ )
		printf("%02x ", history[len]);
	printf("\n");
#endif
}

char *prevhistory(aline)
char *aline;
{
	char *ptr = history+history_crp;
	
	if (history_crp > 0)
	{
		while(*--ptr != '\n');
		ptr++;
		memcpy(aline, ptr, history_crp - (ptr - history));
		aline[history_crp - (ptr - history)] = '\0';
		history_crp = ptr - history - 1;
	}
	return ptr;
}

char *nexthistory(aline)
char *aline;
{
	char *ptr = history+history_crp;
	if (history_crp < history_len - 1)
	{
		while(*ptr++ != '\n');
		memcpy(aline, ptr+1, (ptr - history) - history_crp);
		aline[(ptr - history) - history_crp + 1] = '\0';
		history_crp = ptr - history;
	}
	return ptr;
}

void history_substitutions(aline)
char *aline;
{
	char *ptr,*cmd;
	int hindex;
	
	if (aline[0] != '!')
		return;
		
	hindex = atoi(aline+1);
	if (!strcmp(aline, "!!"))
	{
		prevhistory(aline);
	}
	else
	if (hindex > 0)
	{
		ptr = history;
		while (hindex-- > 0 && ptr - history + 1 < history_len)
		{
			ptr = strchr(ptr, '\n') + 1;
		}
		if (ptr - history + 1 < history_len)
			memcpy(aline, ptr, strchr(ptr, '\n') - ptr);
	}
	else	/* pattern match */
	{
		ptr = history  + history_len - 1;
		while (ptr > history)
		{
			while (*--ptr != '\n');
			if (ptr)
			{
				cmd = ptr + 1;
				if (strstr(cmd, aline+1) == cmd)
				{
					memcpy(aline, cmd, strchr(cmd, '\n') - cmd);
					break;
				}
			}
		}
	
	
	}
}

void insertch(line, crp)
char *line;
int crp;
{
	register int i;
	if (line[crp])
	{
		for(i=strlen(line); i>crp; i--)
			line[i] = line[i-1];
	}
}

void deletech(line, crp)
char *line;
int crp;
{
	register int i;
	int last = strlen(line);
	line[crp] = '\0';
	for(i=crp; i<=last; i++)
	  line[i] = line[i+1];
}

int complete(partial)
char *partial;
{
	char pathname[MAXLINELEN];
	char *expandpos,*lastsep;
  DIR *d;
  struct direct *dir;

	expandpos = strrchr(partial, '/');
	if (expandpos++)
		{}
	else
		expandpos = partial;

	strcpy(pathname, partial);
	if (partial[0] == '~')
	{
		strcpy(pathname, getenv("HOME"));
		strcat(pathname, partial+1);
	}
	
	lastsep = strrchr(pathname, '/');
	if (lastsep)
		*lastsep = 0;
	else
		strcpy(pathname, ".");
		
  d = opendir(pathname);
  if (d)
  {
    while ((dir = readdir(d)) != NULL)
    {
			if (strstr(dir->d_name, expandpos) == dir->d_name)
			{
				strcpy(expandpos, dir->d_name);
				closedir(d);
				return 1;
			}
    }
    closedir(d);
  }
  return(0);
}


int whereis(filepath, cmd)
char *filepath;
char *cmd;
{
  struct stat info;
  char *apath,*search;
  char workingsearch[1024];
  
  search = getenv("PATH");
	if (!search)
			search = "/bin:.";
	
	strcpy(workingsearch,search);
  apath = strtok(workingsearch, ":");
  while (apath)
  {
    strcpy(filepath, apath);
    strcat(filepath, "/");
    strcat(filepath, cmd);
      	
    if (stat(filepath, &info) == 0)
    {
			return 1;
    }

    apath = strtok(NULL,  ":");
  }

	return 0;
}

int countvisible(str)
char *str;
{
	int count = 0;
	int state = 1;
	char c;
	
	while((c = *str++) != '\0')
	{
		if (c == 0x1b)
			state = 0;
		else
		if (!state)
		{
			if (c >= 0x40 && c <= 0x7e && c != '[')
				state = 1;
		}
		else
		{
			count += state;
		}
	}
	
	return count;
}

void printdstack(pwd)
char *pwd;
{
  int i;
  char *home;
  char pathname[512];
  
	home = getenv("HOME");
  if (pwd)
  {
		strcpy(pathname, pwd);
		if (strstr(pathname, home) == pathname)
		{
			pathname[0] = '~';
			memcpy(pathname+1, pathname+strlen(home), strlen(pathname));
		}
	
    printf(pathname);
		putchar(' ');
	}

  i = dstacktop;
  while (i-- > 0)
  {
		strcpy(pathname, dstack[i]);
		if (strstr(pathname, home) == pathname)
		{
			pathname[0] = '~';
			memcpy(pathname+1, pathname+strlen(home), strlen(pathname));
		}
	
    printf(pathname);
		putchar(' ');
  }
		putchar('\n');
}

char *prettygetcwd(pathname, len)
char *pathname;
int len;
{
	char *name;
	
	getcwd(pathname, len);
	name = strrchr(pathname, '\n');
	if (name)
		*name = '\0';

	return pathname;
}

char *
readline()
{
  static char line[MAXLINELEN];
  int done = 0;
  int rc,i,lastlen, crp;
  char ch;

  /* CBREAK input */
#ifdef __clang__
  /* modern OS no longer support stty as above */
  tcgetattr(0, &t);
  origt = t;
  t.c_lflag &= ~(ICANON | ISIG | IEXTEN | ECHO);
  
  tcsetattr(0, TCSANOW, &t);
#else
  struct sgttyb new_term_settings;
  new_term_settings = slave_orig_term_settings;
  new_term_settings.sg_flag |= CBREAK;
  new_term_settings.sg_flag &= ~CRMOD;
  stty(0, &new_term_settings);
#endif

  /* force visible cursor */
  printf("\033[?25h");

  crp = 0;
  memset(line, 0, sizeof(line));
  while(!done)
  {
		char *prompt = getenv("PROMPT");
		if (!prompt)
			prompt = "\033[7mash++\033[0m";
			
    /* TODO: do any substitution in prompt */

    /* ignore ansi escapes */
    i = countvisible(prompt);

    printf("\r%s %s\033[K", prompt, line);
    printf("\r\033[%dC", (i + 1) + crp); /* NB +1 coz cursorx starts at 1 not 0 */
    fflush(stdout);

    lastlen = (int)strlen(line);
    read(0, &ch, 1);

if (ch == 'U' - 64)
{
  fprintf(stderr, "\012\n%02x %02x %02x %02x  crp=%d\012\n",line[0],line[1],line[2],line[3],crp);
}
else

    if (ch == '\n')
    {
      done = 1;
    }
		else
		if (ch == 'A' - 64)
		{
			crp = 0;
		}
		else
		if (ch == 'B' - 64)
		{
			if (crp > 0)
				crp--;
		}
		else
		if (ch == 'C' - 64)
		{
			line[0] = 0;
			done = 1;
		}
		else
		if (ch == 'D' - 64)
		{
			deletech(line, crp);
		}
		else
		if (ch == 'E' - 64)
		{
			crp = strlen(line);
		}
		else
		if (ch == 'F' - 64)
		{
			if (line[crp])
				crp++;
		}
		else
		if (ch == 'H' - 64)
		{
			if (crp > 0)
				crp--;
			deletech(line, crp);
		}
		else
		if (ch == 'I' - 64)
		{
			char partial[512];
			
			/* auto complete */
			i = crp;
			while (i > 0 && line[i] != ' ')
				i--;
			if (i)
				i++;
				
			memcpy(partial, line+i, crp - i + 1);
			if (complete(partial))
			{
				strcpy(line+i, partial);
				crp = strlen(line);
			}
		}
		else
		if (ch == 'P' - 64)
		{
			prevhistory(line);
			crp = strlen(line);
		}
		else
		if (ch == 'N' - 64)
		{
			nexthistory(line);
			crp = strlen(line);
		}
		else
		if (ch == 127)
		{
			if (crp > 0)
				crp--;
			deletech(line, crp);
		}
		else
		if (ch)
		{
			insertch(line, crp);
			line[crp++] = ch;
 		}
  }

#ifdef __clang__
  tcsetattr(0, TCSANOW, &origt);
#else
	stty(0, &slave_orig_term_settings);
#endif

	putchar('\n');

  return line;
}

int tokenize(args, aline)
char **args;
char *aline;
{
	char *token;
	int c = 0;

	token = strtok(aline, " \n");
	while(token)
	{
		args[c++] = token;
		token = strtok(NULL, " \n");
	}
	args[c] = NULL;

	return c;
}

void var_substitutions(args)
char **args;
{
	char *name;
	
	while(*args)
	{
		if (*args[0] == '$')
		{
			name = getenv((*args)+1);
			if (name)
				*args = name;
		}
		
		if (*args[0] == '~')
		{
			name = *args;
			strcpy(homeroot, getenv("HOME"));
			strcat(homeroot, name+1);
			
			*args = homeroot;
		}
		
		args++;
	}
}
void closedown()
{
	char filepath[MAXLINELEN];
	int fd,i;

#ifdef __clang__
  tcsetattr(0, TCSANOW, &origt);
#else
	stty(0, &slave_orig_term_settings);
#endif

	for(i=0; i<cmdcount; i++)
	{
		kill(cmdpid[i], SIGKILL);
	}

	strcpy(filepath, getenv("HOME"));
	strcat(filepath, "/.ash_history");
	/* printf("closedown: writing to %s\n", filepath); */

	creat(filepath, S_IREAD | S_IWRITE);
	fd = open(filepath, O_WRONLY);
	write(fd, history+1, history_len - 1);
	close(fd);

	exit(0);
}

void sh_exit(sig)
int sig;
{
	closedown();
}

int cmplsentry(a,b)
int *a;
int *b;
{
	lsentry *x = (lsentry *)a;
	lsentry *y = (lsentry *)b;

	return strcmp(x->name, y->name);
}

int builtins(args, env)
char **args;
char **env;
{
	char pathname[MAXLINELEN];
	char *name,*ptr;
	int i,len;
	
		if (!strcmp(args[0], "exit"))
		{
			closedown();
		}
		if (!strcmp(args[0], "ls"))
		{
			/* only if its simple column output */
			if (args[1] == NULL)
			{
				DIR *d;
				struct direct *dir;
				struct stat info;
				lsentry *entries;
				int numentries = 0;
				int maxentries = 64;
				
				entries = (lsentry *)malloc(sizeof(lsentry) * maxentries);
				prettygetcwd(pathname, sizeof(pathname));
				d = opendir(pathname);
				strcat(pathname, "/");
				len = strlen(pathname);
				if (d)
				{
					while ((dir = readdir(d)) != NULL)
					{
						strncpy(entries[numentries].name, dir->d_name, sizeof(lsentry)-2);
						
						strcpy(pathname + len, dir->d_name);
						stat(pathname, &info);

#ifndef __clang__
						if (info.st_perm & S_IEXEC)
						{
							strcat(entries[numentries].name, "*");
						}
#endif
				if ((info.st_mode & S_IFDIR) == S_IFDIR)
				{
					if (strcmp(entries[numentries].name, ".") && strcmp(entries[numentries].name, ".."))
						strcat(entries[numentries].name, "/");
				}

						numentries++;
						if (numentries > maxentries)
						{
							maxentries *= 2;
							entries = (lsentry *)realloc(entries, sizeof(lsentry) * maxentries);
						}
					}
					closedir(d);
					
					qsort(entries, numentries, sizeof(lsentry), cmplsentry);
					
					for (len=0; len<numentries/3; len++)
					{
						for (i=0; i<numentries; i+= (numentries+3)/4)
						{
								if (i+len < numentries) printf("%-20s", entries[i+len].name);
						}
						printf("\n");
 					}

				}

				free(entries);
			
				return 1;
			}
		}
		if (!strcmp(args[0], "pwd"))
		{
			prettygetcwd(pathname, sizeof(pathname));
			printf(pathname);
			putchar('\n');
			return 1;
		}
		if (!strcmp(args[0], "cd"))
		{
			name = args[1];
			if (!name)
				name = getenv("HOME");
			if (chdir(name) < 0)
			{
				printf("cd: %s: no such directory\n", name);
			}
			else
			{
#if 0
				getcwd(pathname, sizeof(pathname));
				setenv("PWD", pathname, 1);
#endif
			}
			
			return 1;
		}
		if (!strcmp(args[0], "dirs"))
		{
				prettygetcwd(pathname, sizeof(pathname));
				printdstack(pathname);
				return 1;
		}
		if (!strcmp(args[0], "pushd"))
		{
			if (dstacktop < MAXPUSH)
			{
				prettygetcwd(dstack[dstacktop], sizeof(dstack[0]));
				if (chdir(args[1]) < 0)
				{
					printf("pushd: %s: no such directory\n", args[1]);
				}
				dstacktop++;
				prettygetcwd(pathname, sizeof(pathname));
				printdstack(pathname);
			}
			return 1;
		}
		if (!strcmp(args[0], "popd"))
		{
			if (dstacktop > 0)
			{
				printdstack(NULL);
				dstacktop--;
				if (chdir(dstack[dstacktop]) < 0)
				{
					printf("popd: %s: no such directory\n", dstack[dstacktop]);
				}
			}
			return 1;
		}
		if (!strcmp(args[0], "env") || !strcmp(args[0], "printenv"))
		{
			while (*env)
			{
				printf(*env++);
				putchar('\n');
			}
			
			return 1;
		}
		if (!strcmp(args[0], "jobs"))
		{
			if (!cmdcount)
				printf("No background jobs\n");
			for (i=0; i<cmdcount; i++)
			{
				printf("[%d] Running %d\n", i+1, cmdpid[i]);
			}

			return 1;
		}
		if (!strcmp(args[0], "history"))
		{
			ptr = history + 1;
			i = 0;
			while(ptr - history < history_len)
			{
				name = strchr(ptr, '\n');
				memcpy(pathname, ptr, name - ptr);
				pathname[name - ptr] = '\0';
				printf("%4d  %s\n", ++i, pathname);
				ptr = name + 1;
			}

			return 1;
		}
		if (!strcmp(args[0], "echo"))
		{
			i = 1;
			while(args[i])
			{
				printf(args[i]);
				putchar(' ');
				i++;
			}
			putchar('\n');
			return 1;
		}
		if (!strcmp(args[0], "alias"))
		{
			return 1;
		}
		if (!strcmp(args[0], "unalias"))
		{
			return 1;
		}
		if (!strcmp(args[0], "set"))
		{
			return 1;
		}
		if (!strcmp(args[0], "unset"))
		{
			return 1;
		}
		
		/* fake alias */
		if (!strcmp(args[0], "ps"))
		{
			args[0] = "status";
			args[1] = "+axl";
			args[2] = NULL;
			return 0;
		}
		if (!strcmp(args[0], "cp"))
		{
			args[0] = "copy";
			return 0;
		}
		if (!strcmp(args[0], "chmod"))
		{
			args[0] = "perms";
			return 0;
		}
		if (!strcmp(args[0], "df"))
		{
			args[0] = "free";
			args[1] = "/dev/disk";
			args[2] = NULL;
			return 0;
		}
		

	return 0;
}

void sh_reap(sig)
int sig;
{
	int result;
	int pid;
	int i;
	
	pid = wait(&result);
	if (pid > 0)
	{
		/* find in cmdpid[] and remove */
		for(i=0; i<MAXJOBS; i++)
		{
			if (cmdpid[i] == pid)
			{
				printf("[%d] %d exit\n", i+1, pid);
		
				cmdpid[i] = cmdpid[cmdcount-1];
				cmdcount--;
				break;
			}
		}
	}

	signal(SIGDEAD, sh_reap);
}

void sh_int(sig)
int sig;
{
  if (runningtask)
  {
    kill(runningtask, SIGINT);
 
    /* do we need to do this? 'more' just hangs */
    kill(runningtask, SIGTERM);
  }
  /* re-enable */  
  signal(SIGINT, sh_int);
}

int main(argc, argv, env)
int argc;
char **argv;
char **env;
{
  char *path;
  char *aline;
  char *cmdtokens[64];
  char filepath[MAXLINELEN];
  int fd,i,c,fgtask,nctask,result;

	runningtask = 0;
	signal(SIGINT, sh_int);
	signal(SIGTERM, sh_exit);
	signal(SIGDEAD, sh_reap);

  /* initial terminal settings */
  gtty(0, &slave_orig_term_settings);

	inithistory();

	while(1)
	{
		aline = readline();
		if (strlen(aline) > 0)
		{
			history_substitutions(aline);
		
			addhistory(aline);

			c = tokenize(cmdtokens, aline);

			/* wildcard expansion */

			var_substitutions(cmdtokens);
			
			/* piping */
			
			/* redirection */
			

			/* is NICE task */
			nctask = strcmp(cmdtokens[0], "nice") == 0;
			if (nctask)
			{
			  memcpy(cmdtokens, cmdtokens+1, sizeof(char *) * c);
			  c--;
			}
			
			/* is background job */
			fgtask = (cmdtokens[c-1][0] != '&');
			if (!fgtask)
			{
				cmdtokens[--c] = NULL;
			}
			
			if (!builtins(cmdtokens, env))
			{
				strcpy(filepath, cmdtokens[0]);
				if (cmdtokens[0][0] == '.' || cmdtokens[0][0] == '/' || whereis(filepath, cmdtokens[0]))
				{
					runningtask = fork();
					if (runningtask == 0)
					{
						if (nctask)
						{
							nice(10);
						}
						
						if (!fgtask)
						{
							/* force stdin to be /dev/null */
							fd = open("/dev/null", O_RDONLY);
							dup2(fd, 0);
						}

						signal(SIGINT, SIG_DFL);
						result = execvp(filepath, cmdtokens);
						if (result < 0)
						{
							fprintf(stderr, "Error %d on exec\n", errno);
						}
						exit(errno);
					}
					
					if (fgtask)
					{
						wait(&result);
						
						/* restore terminal */
						stty(0, &slave_orig_term_settings);
					}
					else
					{
						cmdpid[cmdcount++] = runningtask;
						printf("[%d] %d\n", cmdcount, runningtask);
					}
					runningtask = 0;
				}
				else
				{
					printf("%s: command not found\n", cmdtokens[0]);
				}
			}
		}
	}
}
