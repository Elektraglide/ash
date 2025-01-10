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

#ifdef __clang__
#define TARGET_NEWLINE '\n'
#else
#define TARGET_NEWLINE '\r'
#include <varargs.h>
#endif

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
#define sg_flag sg_flags

#define SIGDEAD SIGCHLD
#define MAXLINELEN 256
#define MAX_ENVIRON 8192

#endif

#ifndef __clang__
#include <sys/modes.h>
#include <sys/sgtty.h>
#define MAXLINELEN 128
#define MAX_ENVIRON 1024
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


/* environ */
int numenvs = 0;
char *envptrs[64];
char envstrings[MAX_ENVIRON];

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

/* local qsort */

static int	(*qscmp)();
static int	qses;

static void qsexc(i, j)
char *i, *j;
{
	register char *ri, *rj, c;
	int n;

	n = qses;
	ri = i;
	rj = j;
	do {
		c = *ri;
		*ri++ = *rj;
		*rj++ = c;
	} while(--n);
}

static void qstexc(i, j, k)
char *i, *j, *k;
{
	register char *ri, *rj, *rk;
	int c;
	int n;

	n = qses;
	ri = i;
	rj = j;
	rk = k;
	do {
		c = *ri;
		*ri++ = *rk;
		*rk++ = *rj;
		*rj++ = c;
	} while(--n);
}

static void qs1(a, l)
char *a, *l;
{
	register char *i, *j;
	register es;
	char **k;
	char *lp, *hp;
	int c;
	unsigned n;


	es = qses;

start:
	if((n=l-a) <= es)
		return;
	n = es * (n / (2*es));
	hp = lp = a+n;
	i = a;
	j = l-es;
	for(;;) {
		if(i < lp) {
			if((c = (*qscmp)(i, lp)) == 0) {
				qsexc(i, lp -= es);
				continue;
			}
			if(c < 0) {
				i += es;
				continue;
			}
		}

loop:
		if(j > hp) {
			if((c = (*qscmp)(hp, j)) == 0) {
				qsexc(hp += es, j);
				goto loop;
			}
			if(c > 0) {
				if(i == lp) {
					qstexc(i, hp += es, j);
					i = lp += es;
					goto loop;
				}
				qsexc(i, j);
				j -= es;
				i += es;
				continue;
			}
			j -= es;
			goto loop;
		}


		if(i == lp) {
			if(lp-a >= l-hp) {
				qs1(hp+es, l);
				l = lp;
			} else {
				qs1(a, lp);
				a = hp+es;
			}
			goto start;
		}


		qstexc(j, lp -= es, i);
		j = hp -= es;
	}
}


void myqsort(a, n, es, fc)
char *a;
unsigned n;
int es;
int (*fc)();
{
	qscmp = fc;
	qses = es;
	qs1(a, a+n*es);
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

  	if (runningtask)
  	{
    		kill(runningtask, SIGKILL);	
  	}

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

void initenviron(env)
char **env;
{
  char *crp = envstrings;
  int len;
  
  numenvs = 0;
  while(*env)
  {
    len = strlen(*env);
    if (len > 0)
    {
      envptrs[numenvs++] = crp;
      strcpy(crp, *env);
      crp += len;
      *crp++ = '\0';
    }
    
    env++;
  }
}

char **getenviron()
{
  return envptrs;
}

void addenviron(keyval)
char *keyval;
{
  int i;
  char *key;
  char *last;
  
  key = strchr(keyval, '=');
  if (key)
  {
    /* just key part */
    *key = 0;
    
    last = envstrings;
    
    /* replace existing? */
    for (i=0; i<numenvs; i++)
    {
      if (!strncmp(envptrs[i], keyval, key - keyval))
      {
        envptrs[i] = envptrs[numenvs-1];
        i--;
      }

      /* track last string in our array */
      if (last < envptrs[i]) last = envptrs[i];
    }
    *key = '=';

    /* append to end */
    last += strlen(last) + 1;

    envptrs[numenvs++] = last;
    strcpy(last, keyval);
    
  }
}

void inithistory()
{
	char filepath[MAXLINELEN];
	int fd;
	
	cmdcount = 0;
	
	history[0] = TARGET_NEWLINE;
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
		while(*ptr++ != TARGET_NEWLINE);
		memcpy(history+1, ptr, history_len - (ptr-history));
		history_len -= (ptr-history);
	}

	memcpy(history+history_len, aline, len);
	history_len += len;
	history[history_len++] = TARGET_NEWLINE;
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
		while(*--ptr != TARGET_NEWLINE);
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
		while(*ptr++ != TARGET_NEWLINE);
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
			ptr = strchr(ptr, TARGET_NEWLINE) + 1;
		}
		if (ptr - history + 1 < history_len)
			memcpy(aline, ptr, strchr(ptr, TARGET_NEWLINE) - ptr);
	}
	else	/* pattern match */
	{
		ptr = history  + history_len - 1;
		while (ptr > history)
		{
			while (*--ptr != TARGET_NEWLINE);
			if (ptr)
			{
				cmd = ptr + 1;
				if (strstr(cmd, aline+1) == cmd)
				{
					memcpy(aline, cmd, strchr(cmd, TARGET_NEWLINE) - cmd);
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
	name = strrchr(pathname, TARGET_NEWLINE);
	if (name)
		*name = '\0';

	return pathname;
}
#ifdef __clang__
void loggerf(char *fmt, ...)
{

}
#else
void
loggerf(_varargs)
int _varargs;
{
va_list p;
char buffer[256];
int c;
char *fmt;
unsigned int val1;
unsigned int val2;
unsigned int val3;
unsigned int val4;

	va_start(p);
    fmt = va_arg(p, char *);
    val1 = va_arg(p, unsigned int);
    val2 = va_arg(p, unsigned int);
    val3 = va_arg(p, unsigned int);
    val4 = va_arg(p, unsigned int);
    
	sprintf(buffer, fmt, val1, val2, val3, val4);	 
	c = open("/dev/comm", O_RDWR);
	write(c, buffer, strlen(buffer));
	close(c);

	va_end(p);
}
#endif

char *
readline()
{
  static char line[MAXLINELEN];
  int done = 0;
  int rc,i,lastcrp, crp;
  char ch, seq[3];
	char *prompt;
	
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
  new_term_settings.sg_flag &= ~ECHO;
  stty(0, &new_term_settings);
#endif

  /* force visible cursor */
  printf("\033[?25h");

	/* default prompt */
	prompt = getenv("PROMPT");
	if (!prompt)
		prompt = "\033[7mash++\033[0m ";
		
	/* TODO: do any substitution in prompt */

	printf("%s", prompt);
	
  crp = 0;
  memset(line, 0, sizeof(line));
  lastcrp = 0;
  while(!done)
  {
	/* loggerf("lastcrp %d line %d crp %d\n\012", lastcrp, strlen(line), crp); */

	/* NB cursor movement takes value of 0 to mean default of 1 */
	if (lastcrp) printf("\033[%dD", lastcrp);

	printf("%s\033[K", line);		
	if (strlen(line)) 
		printf("\033[%dD", (int)strlen(line));

	if (crp) printf("\033[%dC", crp);

    fflush(stdout);

    lastcrp = crp;

    rc = (int)read(0, &ch, 1);
    if (rc <  0)
    {
    	closedown();
    	done = 1;
    }

if (ch == 'U' - 64)
{
  fprintf(stderr, "\012\n%02x %02x %02x %02x  crp=%d len=%d\012\n",line[0],line[1],line[2],line[3],crp,(int)strlen(line));
}
else

		if (ch == 0x1b)
		{
			read(0, seq, 2);

			/* cursor controls from modern terminals */
			if (seq[0] == 0x5b)
			{
					if (seq[1] == 'A')
					{
						prevhistory(line);
						crp = strlen(line);
					}
					if (seq[1] == 'B')
					{
						nexthistory(line);
						crp = strlen(line);
					}
					if (seq[1] == 'C')
					{
						if (line[crp])
							crp++;
					}
					if (seq[1] == 'D')
					{
						if (crp > 0)
							crp--;
					}
			}
		}
		else
    if (ch == '\n' || ch == '\r')
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
		if (ch == 'L' - 64)
		{
			putchar('\r');
			putchar('\n');
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

	putchar(TARGET_NEWLINE);

  return line;
}

int tokenize(args, aline)
char **args;
char *aline;
{
	char *token;
	int c = 0;

	token = strtok(aline, " \n\r");
	while(token)
	{
		args[c++] = token;
		token = strtok(NULL, " \n\r");
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

int do_ls()
{
	char pathname[MAXLINELEN];
	char *name,*ptr;
	int i,j,len;

  DIR *d;
  struct direct *dir;
  struct stat info;
  lsentry *entries;
  int numentries = 0;
  int maxentries = 64;
  int maxwidth = 20;
  int field;
          
  entries = (lsentry *)malloc(sizeof(lsentry) * maxentries);
  prettygetcwd(pathname, sizeof(pathname));
  d = opendir(pathname);
  strcat(pathname, "/");
  len = strlen(pathname);
  if (d)
  {
    while ((dir = readdir(d)) != NULL)
    {
      strncpy(entries[numentries].name, dir->d_name, dir->d_namlen);
      entries[numentries].name[dir->d_namlen] = '\0';
      if (dir->d_namlen > maxwidth)
        maxwidth = dir->d_namlen;
        
      strcpy(pathname + len, dir->d_name);
      stat(pathname, &info);

      if (strcmp(entries[numentries].name, ".") && strcmp(entries[numentries].name, ".."))
      {
        if ((info.st_mode & S_IFDIR) == S_IFDIR)
        {
            strcat(entries[numentries].name, "/");
        }
#ifndef __clang__
        else
        {
        if (info.st_perm & S_IEXEC)
        {
          strcat(entries[numentries].name, "*");
        }
        if (info.st_nlink > 2)
        {
          strcat(entries[numentries].name, "@");
        }
        }
#endif
      }
      
      numentries++;
      if (numentries >= maxentries)
      {
        maxentries *= 2;
        entries = (lsentry *)realloc(entries, sizeof(lsentry) * maxentries);
        if (!entries)
        {
          return 0;
        }
      }
    }
    closedir(d);
    
    myqsort(entries, numentries, sizeof(lsentry), cmplsentry);

    /* stride through list TODO: use maxwidth */
    j = (numentries+3)/4;
    for (len=0; len<j; len++)
    {
      for (i=0; i<numentries; i+= j)
      {
        if (i+len < numentries)
        {
          name = entries[i+len].name;
          field = 0;
          while(name[field] && field < 20)
            putchar(name[field++]);
          while(field++ < 20)
            putchar(' ');
        }
      }
      putchar(TARGET_NEWLINE);
    }

  }

  free(entries);

  return 1;
}

int do_umask(arg)
char *arg;
{
register char *ptr;
int i,j;

  i = umask(31);
  if (arg)
  {
    i = 0;
    ptr = arg;
    j = *ptr == '0' ? 8 : 10;	/* leading zero indicates octal, else decimal */
    while (*ptr)
    {
      i *= j;
      i += *ptr++ - '0';
    }
  }
  printf("u-%c%c%c ", (i & 1) ? 'r' : '-', (i & 2) ? 'w' : '-', (i & 4) ? 'x' : '-');
  printf("o-%c%c%c ", (i & 8) ? 'r' : '-', (i & 16) ? 'w' : '-', (i & 32) ? 'x' : '-');
  putchar(TARGET_NEWLINE);
  
  umask(i);
  return 1;
}

int builtins(args, env)
char **args;
char **env;
{
	char pathname[MAXLINELEN];
	char *name,*ptr;
	int i,j,len;
	
		if (!strcmp(args[0], "exit"))
		{
			closedown();
		}
		if (!strcmp(args[0], "ls"))
		{
			/* only if its simple column output */
			if (args[1] == NULL)
			{
				return do_ls();
			}
		}
		if (!strcmp(args[0], "umask") || !strcmp(args[0], "dperm"))
		{
			return do_umask(args[1]);
		}
		if (!strcmp(args[0], "pwd"))
		{
			prettygetcwd(pathname, sizeof(pathname));
			printf(pathname);
			putchar(TARGET_NEWLINE);
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
		if (strstr(args[0], "="))
		{
			addenviron(args[0]);
			return 1;
		}
		if (args[1] && !strcmp(args[1], "="))
		{
			strcpy(pathname, args[0]);
			strcat(pathname, "=");
			strcat(pathname, args[2]);
			addenviron(pathname);
			return 1;
		}
		
		if (!strcmp(args[0], "env") || !strcmp(args[0], "printenv"))
		{
			while (*env)
			{
				printf(*env++);
				putchar(TARGET_NEWLINE);
			}
			
			return 1;
		}
		if (!strcmp(args[0], "jobs"))
		{
			if (!cmdcount)
				printf("No background jobs\n\r");
			for (i=0; i<cmdcount; i++)
			{
				printf("[%d] Running %d%c", i+1, cmdpid[i],TARGET_NEWLINE);
			}

			return 1;
		}
		if (!strcmp(args[0], "history"))
		{
			ptr = history + 1;
			i = 0;
			while(ptr - history < history_len)
			{
				name = strchr(ptr, TARGET_NEWLINE);
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
			putchar(TARGET_NEWLINE);
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
			args[0] = "diskutil";
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
		/* fprintf(stderr, "SIGDEAD %d  => %d\n", pid, result); */
	
 		/* find in cmdpid[] and remove */
		for(i=0; i<MAXJOBS; i++)
		{
			if (cmdpid[i] == pid)
			{
				printf("[%d] %d exit\n\r", i+1, pid);
		
				cmdpid[i] = cmdpid[cmdcount-1];
				cmdcount--;
				break;
			}
		}
		if (runningtask == pid)
			runningtask = 0;
	}

	signal(SIGDEAD, sh_reap);
}

void sh_int(sig)
int sig;
{
struct sgttyb term_settings;

	printf("Interrupted %d\n",sig);

  if (runningtask)
  {
	/* if not RAW mode, forward to child process */
  	gtty(0, &term_settings);
  	if ((term_settings.sg_flag & RAW) == 0)
  	{
		/* fprintf(stderr, "SIGINT %d\n", runningtask); */

		kill(runningtask, SIGINT);
	 
		/* do we need to do this? */
		/* 'sleep' does not respond top SIGINT.. */
		kill(runningtask, SIGTERM);

		runningtask = 0;

    }
  }

  signal(sig, sh_int);
}

void sh_handler(sig)
int sig;
{
  fprintf(stderr, "****** signal(%d)\n", sig);
  
  signal(sig, sh_handler);
}

int do_separators(aline, env)
char *aline;
char **env;
{
  char filepath[MAXLINELEN];
  char *cmdtokens[64];
  char *fin, *fout;
  int  finfd, foutfd;
  int fd,i,j,c,fgtask,nctask,result;
	char *phrase,*phraseend;

	phrase = aline;
	do
	{
		/* get next run up to semi-colon separator */
		phraseend = strchr(phrase, ';');
		if (phraseend)
		{
			*phraseend = '\0';
		}
	
		c = tokenize(cmdtokens, phrase);

		/* wildcard expansion */

		var_substitutions(cmdtokens);
		
		/* piping */
		
		/* is NICE task */
		nctask = strcmp(cmdtokens[0], "nice") == 0;
		if (nctask)
		{
			memcpy(cmdtokens, cmdtokens+1, sizeof(char *) * c);
			c--;
		}
		
		/* redirection */
		fin = NULL;
		fout = NULL;
		for(i=1; i<c; i++)
		{
			
			if (cmdtokens[i] && cmdtokens[i][0] == '>')
			{
				j = i;
				fout = cmdtokens[i] + 1;

				/* is filename in next arg? */
				if (fout[0] == 0)
				{
					i++;
					fout = cmdtokens[i];
					c--;
				}
				memcpy(cmdtokens+j, cmdtokens+i+1, sizeof(char *) * c-j);
				c--;
			}
			else
			if (cmdtokens[i] && cmdtokens[i][0] == '<')
			{
				j = i;
				fin = cmdtokens[i] + 1;

				/* is filename in next arg? */
				if (fin[0] == 0)
				{
					i++;
					fin = cmdtokens[i];
					c--;
				}
				memcpy(cmdtokens+j, cmdtokens+i+1, sizeof(char *) * c-j);
				c--;
				i--;
			}
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
				/* redirect stdio */
				finfd = 0;
				foutfd = 0;
				if (fin)
				{
					creat(fin, S_IREAD | S_IWRITE);
					finfd = open(fin, O_RDONLY);
				}
				if (fout)
				{
					creat(fout, S_IREAD | S_IWRITE);
					foutfd = open(fout, O_WRONLY);
				}
					
				runningtask = fork();
				if (runningtask == 0)
				{
					if (nctask)
					{
						nice(10);
					}

					/* redirect stdio */
					if (finfd > 0)
						dup2(finfd, 0);
					if (foutfd > 0)
						dup2(foutfd, 1);

					if (!fgtask && !finfd)
					{
						/* force stdin to be /dev/null */
						fd = open("/dev/null", O_RDONLY);
						dup2(fd, 0);
					}

					signal(SIGINT, SIG_DFL);

					result = execvp(filepath, cmdtokens);
					if (result < 0)
					{
						fprintf(stderr, "Error %d on exec\n\r", errno);
					}
					exit(errno);
				}
				
				if (fgtask)
				{
					/* just while subcommand is running */
					signal(SIGINT, sh_int);

					c = -1;
					while(c == -1 && runningtask)
						c = wait(&result);
					
					signal(SIGINT, SIG_IGN);

					if (finfd)
						close(finfd);
					if (foutfd)
						close(foutfd);

					/* restore terminal */
					stty(0, &slave_orig_term_settings);
					
					/* if command was aborted, stop processing */
				}
				else
				{
					cmdpid[cmdcount++] = runningtask;
					printf("[%d] %d\n\r", cmdcount, runningtask);
				}
				runningtask = 0;
			}
			else
			{
				printf("%s: command not found\n\r", cmdtokens[0]);
			}
		}

		phrase = phraseend ? phraseend + 1 : NULL;

	} while(phrase);

	return 0;
}

int main(argc, argv, env)
int argc;
char **argv;
char **env;
{
  char *aline;
	int i;
	
	runningtask = 0;
	signal(SIGINT, SIG_IGN);
	signal(SIGTERM, sh_exit);
	signal(SIGDEAD, sh_reap);

  /* initial terminal settings */
  gtty(0, &slave_orig_term_settings);

	inithistory();
 
  initenviron(env);

	while(1)
	{
		aline = readline();
		if (strlen(aline) > 0)
		{
			history_substitutions(aline);
		
			addhistory(aline);

			do_separators(aline, getenviron());

		}
	}
}

