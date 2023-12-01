#include<stdio.h>
#include<string.h>
#include <stdlib.h>
#include <errno.h>
//#include <sys/sgtty.h>
#include <sys/termios.h>
#include <sys/stat.h>
#include <sys/dir.h>

#include <unistd.h>

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

void delch(line, crp)
char *line;
int crp;
{
	int i;
	int last = strlen(line)+1;
	for(i=crp; i<last; i++)
		line[i] = line[i+1];
	line[i] = '\0';
}

int complete(fullname, partial)
char *fullname;
char *partial;
{

  DIR *d;
  struct dirent *dir;
  d = opendir(".");
  if (d)
  {
    while ((dir = readdir(d)) != NULL)
    {
			if (strstr(dir->d_name, partial) == dir->d_name)
			{
				strcpy(fullname, dir->d_name);
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
  char *workingsearch;
  
  search = getenv("PATH");
	if (!search)
			search = "/bin:.";
	
	workingsearch = strdup(search);
  apath = strtok(workingsearch, ":");
  while (apath)
  {
    strcpy(filepath, apath);
    strcat(filepath, "/");
    strcat(filepath, cmd);
      	
    if (stat(filepath, &info) == 0)
    {
			free(workingsearch);
			return 1;
    }

    apath = strtok(NULL,  ":");
  }

	free(workingsearch);
	return 0;
}

char *
readline()
{
  static char line[256];
  int done = 0;
  int i,lastlen, len = 0;
  char ch;

  /* CBREAK input */
#ifdef __clang__
  /* modern OS no longer support stty as above */
  struct termios origt,t = {};
  tcgetattr(0, &t);
  origt = t;
  t.c_lflag &= ~(ICANON | ISIG | IEXTEN | ECHO);
  
  tcsetattr(0, TCSANOW, &t);
#else
  struct sgttyb new_term_settings;
  rc = gtty(0, &new_term_settings);
  new_term_settings.sg_flag |= CBREAK;
  stty(0, &new_term_settings);
#endif

	char curvis[] = {0x1b, '[', '?', '2', '5', 'h', 0};
	printf("%s", curvis);

  memset(line, 0, sizeof(line));
  while(!done)
  {
		char startline[] = {0x1b, '[', '1', 'G', 0};
		
		char *prompt = getenv("PROMPT");
		if (!prompt)
			prompt = "sys++ ";
			
		/* TODO: do any substitution in prompt */

    printf("%s%s%s", startline, prompt, line);
    if (strlen(line) < lastlen) printf("    ");
    printf("%c\[%dG", 0x1b, (int)strlen(prompt) + len+1);
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
			delch(line, len);
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
			delch(line, len);
			if (len > 0)
				len--;
		}
		else
		if (ch == 'I' - 64)
		{
			char partial[64];
			char fullname[512];
			
			/* auto complete */
			i = len;
			while (i > 0 && line[i] != ' ')
				i--;
			if (i)
				i++;
				
			memcpy(partial, line+i, len - i + 1);
			if (complete(fullname, partial))
			{
				strcpy(line+i, fullname);
				len = strlen(line);
			}
		}
		else
		if (ch == 'P' - 64)
		{
			printf("previous history\n");
		}
		else
		if (ch == 'N' - 64)
		{
			printf("next history\n");
		}
		else
		if (ch == 127)
		{
			if (len > 0)
				len--;
			delch(line, len);
		}
		else
		{
			insertch(line, len);
			line[len++] = ch;
    }
  }

#ifdef __clang__
  tcsetattr(0, TCSANOW, &origt);
#endif

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

int builtins(args)
char **args;
{
	char pathname[512];
	int i;
	
		if (!strcmp(args[0], "exit"))
		{
			exit(0);
		}
		if (!strcmp(args[0], "LS"))
		{
			return 1;
		}
		if (!strcmp(args[0], "pwd"))
		{
			getcwd(pathname, sizeof(pathname));
			printf("%s\n", pathname);
			return 1;
		}
		if (!strcmp(args[0], "cd"))
		{
			if (chdir(args[1]) < 0)
			{
				
			}
			
			return 1;
		}
		if (!strcmp(args[0], "pushd"))
		{
			return 1;
		}
		if (!strcmp(args[0], "popd"))
		{
			return 1;
		}
		if (!strcmp(args[0], "env") || !strcmp(args[0], "printenv"))
		{
			return 1;
		}
		if (!strcmp(args[0], "history"))
		{
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
  char *args[256];
  char filepath[512];
  int i,c,bgtask,result;

	signal(SIGINT, SIG_IGN);

	while(1)
	{
		aline = readline();
		if (strlen(aline) > 0)
		{
			printf("\ncommand line was: %s\n", aline);
			printf("----\n");
			
			//strcpy(history, aline);
			
			c = tokenize(args, aline);

			/* $ shell var substitutions */
			
			/* piping */
			
			/* background job */
			bgtask = (args[c-1][0] == '&');
			
			if (!builtins(args))
			{
				strcpy(filepath, args[0]);
				if (args[0][0] == '.' || args[0][0] == '/' || whereis(filepath, args[0]))
				{
					fprintf(stderr, "exec: %s\n", filepath);
					if (fork() == 0)
					{
						signal(SIGINT, SIG_DFL);
						result = execvp(filepath, args);
						if (result < 0)
						{
							fprintf(stderr, "Error %d on exec\n", errno);
						}
					}
					
					if (!bgtask)
					{
						wait(&result);
						if (result & 0x80)
							printf("core dumped\n");
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
