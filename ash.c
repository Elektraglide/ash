
#include <stdio.h>
#include <string.h>
#include <errno.h> 
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <signal.h>

#include <sys/dir.h>

extern void exit();
extern int atoi();
extern int kill();
extern char *getenv();

/*

cc -std=c89 ash.c -o ash

*/

#ifdef __clang__
#include <sys/termios.h>
#include <sys/wait.h>
#include <unistd.h>
struct termios origt,t = {};
#endif

#ifndef __clang__
#include <sys/modes.h>
#include <sys/sgtty.h>
struct sgttyb slave_orig_term_settings;
#endif

/* escape sequences */
char curvis[] = {0x1b, '[', '?', '2', '5', 'h', 0};
char escape[] = {0x1b, '[', 0};
char delright[] = {0x1b, '[', '1', 'K', 0};

/* pushd/pop dir */
int dstacktop = 0;
char dstack[8][256];

/* history processing */
#define MAXHISTORY 1024
int cmdpid[8];
int cmdcount;
char history[MAXHISTORY];
int history_len;
int history_crp;

char pathname[512];

void inithistory()
{
	char filepath[512];
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
	int i;
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
	int i;
	int last = strlen(line)+1;
	for(i=crp; i<last; i++)
		line[i] = line[i+1];
	line[i] = '\0';
}

int complete(partial)
char *partial;
{
	char pathname[128];
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

char *
readline()
{
  static char line[512];
  int done = 0;
  int rc,i,lastlen, len = 0;
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
	rc = gtty(0, &slave_orig_term_settings);
	new_term_settings = slave_orig_term_settings;
  new_term_settings.sg_flag |= CBREAK;
  new_term_settings.sg_flag &= ~CRMOD;
  stty(0, &new_term_settings);
#endif

	printf("%s", curvis);

  memset(line, 0, sizeof(line));
  while(!done)
  {
		char *prompt = getenv("PROMPT");
/*
		if (!prompt)
*/
			prompt = "ash++ ";
			
		/* TODO: do any substitution in prompt */

		/* FIXME: tek4404 throws newline each time..  */

    printf("%s\r%s%s", delright, prompt, line);
    printf("\r%s%dC", escape, (int)strlen(prompt) + len);
    fflush(stdout);

		lastlen = (int)strlen(line);
    read(0, &ch, 1);
    
    if (ch == '\n')
    {
      done = 1;
    }
		else
		if (ch == 'A' - 64)
		{
			len = 0;
		}
		else
		if (ch == 'B' - 64)
		{
			if (len > 0)
				len--;
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
			deletech(line, len);
		}
		else
		if (ch == 'E' - 64)
		{
			len = strlen(line);
		}
		else
		if (ch == 'F' - 64)
		{
			if (line[len])
				len++;
		}
		else
		if (ch == 'H' - 64)
		{
			deletech(line, len);
			if (len > 0)
				len--;
		}
		else
		if (ch == 'I' - 64)
		{
			char partial[512];
			
			/* auto complete */
			i = len;
			while (i > 0 && line[i] != ' ')
				i--;
			if (i)
				i++;
				
			memcpy(partial, line+i, len - i + 1);
			if (complete(partial))
			{
				strcpy(line+i, partial);
				len = strlen(line);
			}
		}
		else
		if (ch == 'P' - 64)
		{
			prevhistory(line);
			len = strlen(line);
		}
		else
		if (ch == 'N' - 64)
		{
			nexthistory(line);
			len = strlen(line);
		}
		else
		if (ch == 127)
		{
			if (len > 0)
				len--;
			deletech(line, len);
		}
		else
		{
			insertch(line, len);
			line[len++] = ch;
    }
  }

#ifdef __clang__
  tcsetattr(0, TCSANOW, &origt);
#else
	stty(0, &slave_orig_term_settings);
#endif

	printf("\n");

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
			strcpy(pathname, getenv("HOME"));
			strcat(pathname, name+1);
			
			*args = pathname;
		}
		
		args++;
	}
}
void closedown()
{
	char filepath[512];
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
printf("closedown: writing to %s\n", filepath);

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

int builtins(args, env)
char **args;
char **env;
{
	char pathname[512];
	char *name,*ptr;
	int i,len;
	
		if (!strcmp(args[0], "exit"))
		{
			closedown();
		}
		if (!strcmp(args[0], "LS"))
		{
			return 1;
		}
		if (!strcmp(args[0], "pwd"))
		{
			getcwd(pathname, sizeof(pathname));
			name = strrchr(pathname, '\n');
			if (name)
				*name = '\0';
			printf("%s\n", pathname);
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
		if (!strcmp(args[0], "pushd"))
		{
			if (dstacktop < 8)
			{
				getcwd(dstack[dstacktop], sizeof(dstack[0]));
				name = strrchr(dstack[dstacktop], '\n');
				if (name)
					*name = '\0';
fprintf(stderr, "%d: pushed %s\n", dstacktop, dstack[dstacktop]);
				if (chdir(args[1]) < 0)
				{
					printf("pushd: %s: no such directory\n", args[1]);
				}
				dstacktop++;
			}
			return 1;
		}
		if (!strcmp(args[0], "popd"))
		{
			if (dstacktop > 0)
			{
				dstacktop--;
fprintf(stderr, "%d: pop %s\n", dstacktop, dstack[dstacktop]);
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
				printf("%s\n", *env++);
			}
			
			return 1;
		}
		if (!strcmp(args[0], "jobs"))
		{
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
				printf("%s ", args[i]);
				i++;
			}
			printf("\n");
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


	return 0;
}

int main(argc, argv, env)
int argc;
char **argv;
char **env;
{
	char *path;
  char *aline;
  char *args[64];
  char filepath[512];
  int i,c,fgtask,result;

	signal(SIGINT, SIG_IGN);
	signal(SIGTERM, sh_exit);
	inithistory();

	while(1)
	{
		aline = readline();
		if (strlen(aline) > 0)
		{
			history_substitutions(aline);
		
			addhistory(aline);

			c = tokenize(args, aline);

			/* wildcard expansion */

			var_substitutions(args);
			
			/* piping */
			
			/* redirection */
			
			/* is background job */
			fgtask = (args[c-1][0] != '&');
			if (!fgtask)
			{
				args[--c] = NULL;
			}
			
			if (!builtins(args, env))
			{
				strcpy(filepath, args[0]);
				if (args[0][0] == '.' || args[0][0] == '/' || whereis(filepath, args[0]))
				{
					if (cmdcount > 3)
					{
						result = wait(&result);
						if (result > 0)
						{
							printf("%d exit\n", result);
							
							/* find in cmdpid[] and remove */
							for(i=0; i<8; i++)
							{
								if (cmdpid[i] == result)
								{
									cmdpid[i] = cmdpid[cmdcount-1];
									cmdcount--;
									break;
								}
							}
						}
					}
				
				
					result = fork();
					if (result == 0)
					{
						signal(SIGINT, SIG_DFL);
						result = execvp(filepath, args);
						if (result < 0)
						{
							fprintf(stderr, "Error %d on exec\n", errno);
						}
						exit(errno);
					}
					
					if (fgtask)
					{
						wait(&result);
#ifndef __clang__
						if (result & 0x80)
							printf("core dumped\n");
#endif
					}
					else
					{
						/* FIXME: how to know when they finish? */
						cmdpid[cmdcount++] = result;
						printf("[%d] %d\n", cmdcount, result);
					}
				}
				else
				{
					printf("%s: command not found\n", args[0]);
				}
			}
		}
	}
}
