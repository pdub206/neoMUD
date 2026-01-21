/* ************************************************************************
*  file: purgeplay.c                                    Part of CircleMUD * 
*  Usage: purge useless chars from playerfile                             *
*  All Rights Reserved                                                    *
*  Copyright (C) 1992, 1993 The Trustees of The Johns Hopkins University  *
************************************************************************* */

#include "conf.h"
#include "sysdep.h"

#include "structs.h"
#include "utils.h"
#include "toml.h"

#include <dirent.h>
#include <sys/stat.h>


static int purge_file(const char *filename, int *num)
{
  FILE *fl;
  toml_table_t *tab, *cs;
  toml_datum_t name, level, last_logon, act;
  char errbuf[256];
  int okay;
  long timeout;
  char *ptr, reason[80];

  if (!(fl = fopen(filename, "r")))
    return (0);

  tab = toml_parse_file(fl, errbuf, sizeof(errbuf));
  fclose(fl);
  if (!tab)
    return (0);

  name = toml_string_in(tab, "name");
  level = toml_int_in(tab, "level");
  last_logon = toml_int_in(tab, "last_logon");
  cs = toml_table_in(tab, "char_specials");
  act.ok = 0;
  if (cs)
    act = toml_int_in(cs, "act");

  if (!name.ok || !level.ok || !last_logon.ok || !act.ok) {
    if (name.ok)
      free(name.u.s);
    toml_free(tab);
    return (0);
  }

  okay = 1;
  *reason = '\0';

  for (ptr = name.u.s; *ptr; ptr++)
    if (!isalpha(*ptr) || *ptr == ' ') {
      okay = 0;
      strcpy(reason, "Invalid name");
    }
  if (level.u.i == 0) {
    okay = 0;
    strcpy(reason, "Never entered game");
  }
  if (level.u.i < 0 || level.u.i > LVL_IMPL) {
    okay = 0;
    strcpy(reason, "Invalid level");
  }
  /* now, check for timeouts.  They only apply if the char is not
     cryo-rented.   Lev 32-34 do not time out.  */

  timeout = 1000;

  if (okay && level.u.i <= LVL_IMMORT) {

    if (!(act.u.i & PLR_CRYO)) {
      if (level.u.i == 1)		timeout = 4;	/* Lev   1 : 4 days */
      else if (level.u.i <= 4)	timeout = 7;	/* Lev 2-4 : 7 days */
      else if (level.u.i <= 10)	timeout = 30;	/* Lev 5-10: 30 days */
      else if (level.u.i <= LVL_IMMORT - 1)
	timeout = 60;		/* Lev 11-30: 60 days */
      else if (level.u.i <= LVL_IMMORT)
	timeout = 90;		/* Lev 31: 90 days */
    } else
      timeout = 90;

    timeout *= SECS_PER_REAL_DAY;

    if ((time(0) - last_logon.u.i) > timeout) {
      okay = 0;
      sprintf(reason, "Level %2d idle for %3ld days", (int)level.u.i,
	      ((time(0) - last_logon.u.i) / SECS_PER_REAL_DAY));
    }
  }
  if (act.u.i & PLR_DELETED) {
    okay = 0;
    sprintf(reason, "Deleted flag set");
  }

  /* Don't delete for *any* of the above reasons if they have NODELETE */
  if (!okay && (act.u.i & PLR_NODELETE)) {
    okay = 2;
    strcat(reason, "; NOT deleted.");
  }

  if (!okay) {
    printf("%4d. %-20s %s\n", ++(*num), name.u.s, reason);
    if (okay == 2)
      fprintf(stderr, "%-20s %s\n", name.u.s, reason);
    else
      remove(filename);
  }

  free(name.u.s);
  toml_free(tab);
  return (1);
}


void purge(char *path)
{
  struct stat st;
  int num = 0;

  printf("Deleting: \n");

  if (stat(path, &st) < 0) {
    printf("Can't open %s.", path);
    exit(1);
  }

  if (S_ISDIR(st.st_mode)) {
    static const char *dirs[] = {"A-E", "F-J", "K-O", "P-T", "U-Z", "ZZZ", NULL};
    char testpath[PATH_MAX];
    struct stat tst;
    int i;

    snprintf(testpath, sizeof(testpath), "%s/A-E", path);
    if (stat(testpath, &tst) == 0 && S_ISDIR(tst.st_mode)) {
      for (i = 0; dirs[i]; i++) {
	DIR *dir;
	struct dirent *ent;
	char subdir[PATH_MAX];

	snprintf(subdir, sizeof(subdir), "%s/%s", path, dirs[i]);
	if (!(dir = opendir(subdir)))
	  continue;

	while ((ent = readdir(dir)) != NULL) {
	  size_t len = strlen(ent->d_name);
	  char filename[PATH_MAX];

	  if (len <= 5 || strcmp(ent->d_name + len - 5, ".toml"))
	    continue;

	  snprintf(filename, sizeof(filename), "%s/%s", subdir, ent->d_name);
	  purge_file(filename, &num);
	}
	closedir(dir);
      }
    } else {
      DIR *dir;
      struct dirent *ent;

      if (!(dir = opendir(path)))
	return;

      while ((ent = readdir(dir)) != NULL) {
	size_t len = strlen(ent->d_name);
	char filename[PATH_MAX];

	if (len <= 5 || strcmp(ent->d_name + len - 5, ".toml"))
	  continue;

	snprintf(filename, sizeof(filename), "%s/%s", path, ent->d_name);
	purge_file(filename, &num);
      }
      closedir(dir);
    }
  } else
    purge_file(path, &num);

  printf("Done.\n");
}


int main(int argc, char *argv[])
{
  if (argc != 2)
    printf("Usage: %s player-dir-or-file\n", argv[0]);
  else
    purge(argv[1]);

  return (0);
}
