/* ************************************************************************
*  file: listrent.c                                  Part of CircleMUD *
*  Usage: list player rent files                                          *
*  Written by Jeremy Elson                                                *
*  All Rights Reserved                                                    *
*  Copyright (C) 1993 The Trustees of The Johns Hopkins University        *
************************************************************************* */

#include "conf.h"
#include "sysdep.h"

#include "structs.h"
#include "toml.h"

void Crash_listrent(char *fname);

int main(int argc, char **argv)
{
  int x;

  for (x = 1; x < argc; x++)
    Crash_listrent(argv[x]);

  return (0);
}


void Crash_listrent(char *fname)
{
  char buf[MAX_STRING_LENGTH];
  struct rent_info rent;
  int objcount = 0;
  int i;
  FILE *fl;
  toml_table_t *tab, *rent_tab;
  toml_array_t *objs;
  char errbuf[256];

  if (!(fl = fopen(fname, "r"))) {
    sprintf(buf, "%s has no rent file.\r\n", fname);
    printf("%s", buf);
    return;
  }
  tab = toml_parse_file(fl, errbuf, sizeof(errbuf));
  fclose(fl);
  if (!tab) {
    printf("%s has no rent file.\r\n", fname);
    return;
  }

  rent_tab = toml_table_in(tab, "rent");
  if (!rent_tab) {
    toml_free(tab);
    printf("%s has no rent file.\r\n", fname);
    return;
  }
  {
    toml_datum_t rcode = toml_int_in(rent_tab, "rentcode");
    if (!rcode.ok) {
      toml_free(tab);
      printf("%s has no rent file.\r\n", fname);
      return;
    }
    rent.rentcode = (int)rcode.u.i;
  }

  sprintf(buf, "%s\r\n", fname);
  switch (rent.rentcode) {
  case RENT_RENTED:
    strcat(buf, "Rent\r\n");
    break;
  case RENT_CRASH:
    strcat(buf, "Crash\r\n");
    break;
  case RENT_CRYO:
    strcat(buf, "Cryo\r\n");
    break;
  case RENT_TIMEDOUT:
  case RENT_FORCED:
    strcat(buf, "TimedOut\r\n");
    break;
  default:
    strcat(buf, "Undef\r\n");
    break;
  }
  objs = toml_array_in(tab, "objects");
  if (objs) {
    objcount = toml_array_nelem(objs);
    for (i = 0; i < objcount; i++) {
      toml_table_t *otab = toml_table_at(objs, i);
      toml_datum_t val;
      if (!otab)
	continue;
      val = toml_int_in(otab, "item_number");
      if (val.ok)
	sprintf(buf, "%s[%5d] %s\n", buf, (int)val.u.i, fname);
    }
  }
  printf("%s", buf);
  toml_free(tab);
}
