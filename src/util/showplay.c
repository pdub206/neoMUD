/* ************************************************************************
*  file:  showplay.c                                  Part of CircleMud   *
*  Usage: list a diku playerfile                                          *
*  Copyright (C) 1990, 1991 - see 'license.doc' for complete information. *
*  All Rights Reserved                                                    *
************************************************************************* */

#include "conf.h"
#include "sysdep.h"

#include "structs.h"
#include "toml.h"

#include <dirent.h>
#include <sys/stat.h>


static int show_file(const char *filename, int *num)
{
  char sexname;
  char classname[10];
  FILE *fl;
  toml_table_t *tab, *cs, *pt;
  toml_datum_t name, level, chclass, sex, idnum, gold, bank;
  char errbuf[256];

  if (!(fl = fopen(filename, "r")))
    return (0);

  tab = toml_parse_file(fl, errbuf, sizeof(errbuf));
  fclose(fl);
  if (!tab)
    return (0);

  name = toml_string_in(tab, "name");
  level = toml_int_in(tab, "level");
  chclass = toml_int_in(tab, "class");
  sex = toml_int_in(tab, "sex");
  cs = toml_table_in(tab, "char_specials");
  pt = toml_table_in(tab, "points");
  idnum.ok = 0;
  gold.ok = 0;
  bank.ok = 0;
  if (cs)
    idnum = toml_int_in(cs, "idnum");
  if (pt) {
    gold = toml_int_in(pt, "gold");
    bank = toml_int_in(pt, "bank_gold");
  }

  if (!name.ok || !level.ok || !chclass.ok || !sex.ok || !idnum.ok || !gold.ok || !bank.ok) {
    if (name.ok)
      free(name.u.s);
    toml_free(tab);
    return (0);
  }

  switch (chclass.u.i) {
    case CLASS_THIEF:
      strcpy(classname, "Th");
      break;
    case CLASS_WARRIOR:
      strcpy(classname, "Wa");
      break;
    case CLASS_MAGIC_USER:
      strcpy(classname, "Mu");
      break;
    case CLASS_CLERIC:
      strcpy(classname, "Cl");
      break;
    default:
      strcpy(classname, "--");
      break;
    }

  switch (sex.u.i) {
    case SEX_FEMALE:
      sexname = 'F';
      break;
    case SEX_MALE:
      sexname = 'M';
      break;
    case SEX_NEUTRAL:
      sexname = 'N';
      break;
    default:
      sexname = '-';
      break;
    }

  printf("%5d. ID: %5ld (%c) [%2d %s] %-16s %9d g %9d b\n", ++(*num),
	 (long)idnum.u.i, sexname, (int)level.u.i, classname, name.u.s,
	 (int)gold.u.i, (int)bank.u.i);

  free(name.u.s);
  toml_free(tab);
  return (1);
}

void show(char *path)
{
  struct stat st;
  int num = 0;

  if (stat(path, &st) < 0) {
    perror("error opening playerfile");
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
	  show_file(filename, &num);
	}
	closedir(dir);
      }
    } else {
      DIR *dir;
      struct dirent *ent;

      if (!(dir = opendir(path))) {
	perror("error opening player directory");
	exit(1);
      }

      while ((ent = readdir(dir)) != NULL) {
	size_t len = strlen(ent->d_name);
	char filename[PATH_MAX];

	if (len <= 5 || strcmp(ent->d_name + len - 5, ".toml"))
	  continue;

	snprintf(filename, sizeof(filename), "%s/%s", path, ent->d_name);
	show_file(filename, &num);
      }
      closedir(dir);
    }
  } else
    show_file(path, &num);
}


int main(int argc, char **argv)
{
  if (argc != 2)
    printf("Usage: %s player-dir-or-file\n", argv[0]);
  else
    show(argv[1]);

  return (0);
}
