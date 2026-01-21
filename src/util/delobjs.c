/* ************************************************************************
*  file:  delobjs.c                                   Part of CircleMud   *
*  Usage: deleting object files for players who are not in the playerfile *
*  Written by Jeremy Elson 4/2/93                                         *
*  All Rights Reserved                                                    *
*  Copyright (C) 1993 The Trustees of The Johns Hopkins University        *
************************************************************************* */

/*
   I recommend you use the script in the lib/plrobjs directory instead of
   invoking this program directly; however, you can use this program thusly:

   usage: switch into an obj directory; type: delobjs <plrfile> <obj wildcard>
 */

#include "conf.h"
#include "sysdep.h"

#include "structs.h"
#include "utils.h"
#include "toml.h"

#include <dirent.h>
#include <sys/stat.h>

struct name_element {
  char name[20];
  struct name_element *next;
};

struct name_element *name_list = 0;

void do_purge(int argc, char **argv)
{
  int x;
  struct name_element *tmp;
  char name[1024];
  int found;

  for (x = 2; x < argc; x++) {
    found = 0;
    strcpy(name, argv[x]);
    *(strchr(name, '.')) = '\0';
    for (tmp = name_list; !found && tmp; tmp = tmp->next)
      if (!strcmp(tmp->name, name))
	found = 1;
    if (!found) {
      remove(argv[x]);
      printf("Deleting %s\n", argv[x]);
    }
  }
}




int main(int argc, char **argv)
{
  struct name_element *tmp;
  struct stat st;

  if (argc < 3) {
    printf("Usage: %s <player-dir-or-file> <file1> <file2> ... <filen>\n",
	   argv[0]);
    exit(1);
  }
  if (stat(argv[1], &st) < 0) {
    perror("Unable to open player directory");
    exit(1);
  }
  if (S_ISDIR(st.st_mode)) {
    static const char *dirs[] = {"A-E", "F-J", "K-O", "P-T", "U-Z", "ZZZ", NULL};
    char testpath[PATH_MAX];
    struct stat tst;
    int i;

    snprintf(testpath, sizeof(testpath), "%s/A-E", argv[1]);
    if (stat(testpath, &tst) == 0 && S_ISDIR(tst.st_mode)) {
      for (i = 0; dirs[i]; i++) {
	DIR *dir;
	struct dirent *ent;
	char subdir[PATH_MAX];

	snprintf(subdir, sizeof(subdir), "%s/%s", argv[1], dirs[i]);
	if (!(dir = opendir(subdir)))
	  continue;

	while ((ent = readdir(dir)) != NULL) {
	  size_t len = strlen(ent->d_name);
	  char filename[PATH_MAX];
	  FILE *fl;
	  toml_table_t *tab, *cs;
	  toml_datum_t name, act;
	  char errbuf[256];

	  if (len <= 5 || strcmp(ent->d_name + len - 5, ".toml"))
	    continue;

	  snprintf(filename, sizeof(filename), "%s/%s", subdir, ent->d_name);
	  if (!(fl = fopen(filename, "r")))
	    continue;
	  tab = toml_parse_file(fl, errbuf, sizeof(errbuf));
	  fclose(fl);
	  if (!tab)
	    continue;

	  name = toml_string_in(tab, "name");
	  cs = toml_table_in(tab, "char_specials");
	  act.ok = 0;
	  if (cs)
	    act = toml_int_in(cs, "act");

	  if (name.ok && act.ok && !(act.u.i & PLR_DELETED)) {
	    char *ptr;

	    for (ptr = name.u.s; *ptr; ptr++)
	      *ptr = LOWER(*ptr);

	    tmp = (struct name_element *) malloc(sizeof(struct name_element));
	    tmp->next = name_list;
	    strcpy(tmp->name, name.u.s);
	    name_list = tmp;
	  }

	  if (name.ok)
	    free(name.u.s);
	  toml_free(tab);
	}
	closedir(dir);
      }
    } else {
      DIR *dir;
      struct dirent *ent;

      if (!(dir = opendir(argv[1]))) {
	perror("Unable to open player directory");
	exit(1);
      }

      while ((ent = readdir(dir)) != NULL) {
	size_t len = strlen(ent->d_name);
	char filename[PATH_MAX];
	FILE *fl;
	toml_table_t *tab, *cs;
	toml_datum_t name, act;
	char errbuf[256];

	if (len <= 5 || strcmp(ent->d_name + len - 5, ".toml"))
	  continue;

	snprintf(filename, sizeof(filename), "%s/%s", argv[1], ent->d_name);
	if (!(fl = fopen(filename, "r")))
	  continue;
	tab = toml_parse_file(fl, errbuf, sizeof(errbuf));
	fclose(fl);
	if (!tab)
	  continue;

	name = toml_string_in(tab, "name");
	cs = toml_table_in(tab, "char_specials");
	act.ok = 0;
	if (cs)
	  act = toml_int_in(cs, "act");

	if (name.ok && act.ok && !(act.u.i & PLR_DELETED)) {
	  char *ptr;

	  for (ptr = name.u.s; *ptr; ptr++)
	    *ptr = LOWER(*ptr);

	  tmp = (struct name_element *) malloc(sizeof(struct name_element));
	  tmp->next = name_list;
	  strcpy(tmp->name, name.u.s);
	  name_list = tmp;
	}

	if (name.ok)
	  free(name.u.s);
	toml_free(tab);
      }
      closedir(dir);
    }
  } else {
    FILE *fl;
    toml_table_t *tab, *cs;
    toml_datum_t name, act;
    char errbuf[256];

    if (!(fl = fopen(argv[1], "r"))) {
      perror("Unable to open player file");
      exit(1);
    }
    tab = toml_parse_file(fl, errbuf, sizeof(errbuf));
    fclose(fl);
    if (tab) {
      name = toml_string_in(tab, "name");
      cs = toml_table_in(tab, "char_specials");
      act.ok = 0;
      if (cs)
	act = toml_int_in(cs, "act");

      if (name.ok && act.ok && !(act.u.i & PLR_DELETED)) {
	char *ptr;

	for (ptr = name.u.s; *ptr; ptr++)
	  *ptr = LOWER(*ptr);

	tmp = (struct name_element *) malloc(sizeof(struct name_element));
	tmp->next = name_list;
	strcpy(tmp->name, name.u.s);
	name_list = tmp;
      }
      if (name.ok)
	free(name.u.s);
      toml_free(tab);
    }
  }

  do_purge(argc, argv);
  exit(0);
}
