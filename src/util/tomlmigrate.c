/* ************************************************************************
*  file: tomlmigrate.c                               Part of CircleMUD   *
*  Usage: convert legacy world/player/object files to TOML              *
*                                                                        *
*  This utility is derived from existing CircleMUD parsing code.         *
************************************************************************ */

#include "conf.h"
#include "sysdep.h"

#include <dirent.h>
#include <sys/stat.h>

#include "structs.h"
#include "utils.h"
#include "db.h"
#include "shop.h"
#include "constants.h"

#define MODE_WLD 0
#define MODE_MOB 1
#define MODE_OBJ 2
#define MODE_ZON 3
#define MODE_SHP 4

static void toml_write_escaped(FILE *fp, const char *text)
{
  const unsigned char *p = (const unsigned char *)(text ? text : "");

  fputc('"', fp);
  for (; *p; p++) {
    switch (*p) {
    case '\\':
      fputs("\\\\", fp);
      break;
    case '"':
      fputs("\\\"", fp);
      break;
    case '\n':
      fputs("\\n", fp);
      break;
    case '\r':
      fputs("\\r", fp);
      break;
    case '\t':
      fputs("\\t", fp);
      break;
    default:
      if (*p < 32)
        fprintf(fp, "\\u%04x", *p);
      else
        fputc(*p, fp);
      break;
    }
  }
  fputc('"', fp);
}

static void toml_write_key_string(FILE *fp, const char *key, const char *text)
{
  fprintf(fp, "%s = ", key);
  toml_write_escaped(fp, text ? text : "");
  fputc('\n', fp);
}

static void toml_write_int_array(FILE *fp, const char *key, int *vals, int count)
{
  int i;

  fprintf(fp, "%s = [", key);
  for (i = 0; i < count; i++) {
    if (i)
      fputs(", ", fp);
    fprintf(fp, "%d", vals[i]);
  }
  fputs("]\n", fp);
}

static int get_line_legacy(FILE *fl, char *buf)
{
  char temp[READ_SIZE];
  int lines = 0;
  int sl;

  do {
    if (!fgets(temp, READ_SIZE, fl))
      return (0);
    lines++;
  } while (*temp == '*' || *temp == '\n' || *temp == '\r');

  sl = strlen(temp);
  while (sl > 0 && (temp[sl - 1] == '\n' || temp[sl - 1] == '\r'))
    temp[--sl] = '\0';

  strcpy(buf, temp); /* buf is READ_SIZE */
  return (lines);
}

struct line_cache {
  int has_line;
  char line[READ_SIZE];
};

static int get_line_cached(FILE *fl, char *buf, struct line_cache *cache)
{
  if (cache->has_line) {
    strcpy(buf, cache->line);
    cache->has_line = 0;
    return (1);
  }
  return (get_line_legacy(fl, buf));
}

static void push_line_cached(const char *buf, struct line_cache *cache)
{
  strlcpy(cache->line, buf, sizeof(cache->line));
  cache->has_line = 1;
}

static char *fread_string_legacy(FILE *fl, const char *error)
{
  char buf[MAX_STRING_LENGTH], tmp[513];
  char *point;
  int done = 0, length = 0, templength;

  *buf = '\0';

  do {
    if (!fgets(tmp, 512, fl)) {
      fprintf(stderr, "SYSERR: fread_string: format error at or near %s\n", error);
      exit(1);
    }
    if ((point = strchr(tmp, '~')) != NULL) {
      *point = '\0';
      done = 1;
    } else {
      point = tmp + strlen(tmp) - 1;
      *(point++) = '\r';
      *(point++) = '\n';
      *point = '\0';
    }

    templength = strlen(tmp);

    if (length + templength >= MAX_STRING_LENGTH) {
      fprintf(stderr, "SYSERR: fread_string: string too large (tomlmigrate)\n");
      fprintf(stderr, "%s\n", error);
      exit(1);
    } else {
      strcat(buf + length, tmp);
      length += templength;
    }
  } while (!done);

  return (strlen(buf) ? strdup(buf) : NULL);
}

static void write_room(FILE *out, int vnum, char *name, char *desc, char *flags, int sector,
                       struct room_direction_data **room_dirs, struct extra_descr_data *extra)
{
  int i;

  fputs("[[rooms]]\n", out);
  fprintf(out, "vnum = %d\n", vnum);
  toml_write_key_string(out, "name", name ? name : "");
  toml_write_key_string(out, "description", desc ? desc : "");
  toml_write_key_string(out, "flags", flags ? flags : "0");
  fprintf(out, "sector = %d\n", sector);

  for (i = 0; i < NUM_OF_DIRS; i++) {
    if (!room_dirs[i])
      continue;
    fputs("\n[[rooms.exits]]\n", out);
    fprintf(out, "dir = %d\n", i);
    if (room_dirs[i]->general_description)
      toml_write_key_string(out, "description", room_dirs[i]->general_description);
    if (room_dirs[i]->keyword)
      toml_write_key_string(out, "keyword", room_dirs[i]->keyword);
    if (IS_SET(room_dirs[i]->exit_info, EX_PICKPROOF))
      fprintf(out, "door = 2\n");
    else if (IS_SET(room_dirs[i]->exit_info, EX_ISDOOR))
      fprintf(out, "door = 1\n");
    else
      fprintf(out, "door = 0\n");
    fprintf(out, "key = %d\n", room_dirs[i]->key);
    fprintf(out, "to_room = %d\n", room_dirs[i]->to_room);
  }

  for (; extra; extra = extra->next) {
    fputs("\n[[rooms.extra]]\n", out);
    toml_write_key_string(out, "keyword", extra->keyword ? extra->keyword : "");
    toml_write_key_string(out, "description", extra->description ? extra->description : "");
  }
}

static void free_room_data(struct room_direction_data **room_dirs, struct extra_descr_data *extra,
                           char *name, char *desc)
{
  int i;
  struct extra_descr_data *next;

  for (i = 0; i < NUM_OF_DIRS; i++) {
    if (!room_dirs[i])
      continue;
    if (room_dirs[i]->general_description)
      free(room_dirs[i]->general_description);
    if (room_dirs[i]->keyword)
      free(room_dirs[i]->keyword);
    free(room_dirs[i]);
    room_dirs[i] = NULL;
  }

  while (extra) {
    next = extra->next;
    if (extra->keyword)
      free(extra->keyword);
    if (extra->description)
      free(extra->description);
    free(extra);
    extra = next;
  }

  if (name)
    free(name);
  if (desc)
    free(desc);
}

static void convert_wld_file(const char *infile, const char *outfile)
{
  FILE *fl, *out;
  char line[READ_SIZE];
  char buf2[MAX_STRING_LENGTH];

  if (!(fl = fopen(infile, "r"))) {
    fprintf(stderr, "Unable to open %s\n", infile);
    exit(1);
  }
  if (!(out = fopen(outfile, "w"))) {
    fclose(fl);
    fprintf(stderr, "Unable to write %s\n", outfile);
    exit(1);
  }

  for (;;) {
    if (!get_line_legacy(fl, line))
      break;
    if (*line == '$')
      break;

    if (*line == '#') {
      int vnum, t[10], sector;
      char flags[128];
      char *name, *desc;
      struct room_direction_data *room_dirs[NUM_OF_DIRS];
      struct extra_descr_data *extra = NULL;

      if (sscanf(line, "#%d", &vnum) != 1)
        continue;
      snprintf(buf2, sizeof(buf2), "room #%d", vnum);

      name = fread_string_legacy(fl, buf2);
      desc = fread_string_legacy(fl, buf2);

      if (!get_line_legacy(fl, line) || sscanf(line, " %d %s %d ", t, flags, t + 2) != 3) {
        fprintf(stderr, "Format error in room #%d\n", vnum);
        exit(1);
      }
      sector = t[2];

      memset(room_dirs, 0, sizeof(room_dirs));

      for (;;) {
        if (!get_line_legacy(fl, line)) {
          fprintf(stderr, "Format error in room #%d\n", vnum);
          exit(1);
        }
        switch (*line) {
        case 'D': {
          int dir, tdir[5];
          struct room_direction_data *d;

          dir = atoi(line + 1);
          CREATE(d, struct room_direction_data, 1);
          snprintf(buf2, sizeof(buf2), "room #%d, direction D%d", vnum, dir);
          d->general_description = fread_string_legacy(fl, buf2);
          d->keyword = fread_string_legacy(fl, buf2);
          if (!get_line_legacy(fl, line) || sscanf(line, " %d %d %d ", tdir, tdir + 1, tdir + 2) != 3) {
            fprintf(stderr, "Format error in room #%d direction\n", vnum);
            exit(1);
          }
          if (tdir[0] == 1)
            d->exit_info = EX_ISDOOR;
          else if (tdir[0] == 2)
            d->exit_info = EX_ISDOOR | EX_PICKPROOF;
          else
            d->exit_info = 0;
          d->key = tdir[1];
          d->to_room = tdir[2];
          room_dirs[dir] = d;
          break;
        }
        case 'E': {
          struct extra_descr_data *new_descr;

          CREATE(new_descr, struct extra_descr_data, 1);
          new_descr->keyword = fread_string_legacy(fl, buf2);
          new_descr->description = fread_string_legacy(fl, buf2);
          new_descr->next = extra;
          extra = new_descr;
          break;
        }
        case 'S':
          write_room(out, vnum, name, desc, flags, sector, room_dirs, extra);
          free_room_data(room_dirs, extra, name, desc);
          extra = NULL;
          goto done_room;
        default:
          fprintf(stderr, "Format error in room #%d\n", vnum);
          exit(1);
        }
      }
    done_room:
      ;
    }
  }

  fclose(fl);
  fclose(out);
}

static void convert_mob_file(const char *infile, const char *outfile)
{
  FILE *fl, *out;
  char line[READ_SIZE], f1[128], f2[128], buf2[128];
  struct line_cache cache;

  cache.has_line = 0;

  if (!(fl = fopen(infile, "r"))) {
    fprintf(stderr, "Unable to open %s\n", infile);
    exit(1);
  }
  if (!(out = fopen(outfile, "w"))) {
    fclose(fl);
    fprintf(stderr, "Unable to write %s\n", outfile);
    exit(1);
  }

  for (;;) {
    int nr = -1, t[10];
    char *name, *short_descr, *long_descr, *description;
    char letter;

    if (!get_line_cached(fl, line, &cache))
      break;
    if (*line == '$')
      break;
    if (*line != '#')
      continue;

    if (sscanf(line, "#%d", &nr) != 1)
      continue;
    if (nr >= 99999)
      break;

    snprintf(buf2, sizeof(buf2), "mob vnum %d", nr);
    name = fread_string_legacy(fl, buf2);
    short_descr = fread_string_legacy(fl, buf2);
    long_descr = fread_string_legacy(fl, buf2);
    description = fread_string_legacy(fl, buf2);

#ifdef CIRCLE_ACORN
    if (!get_line_cached(fl, line, &cache) || sscanf(line, "%s %s %d %s", f1, f2, t + 2, &letter) != 4) {
#else
    if (!get_line_cached(fl, line, &cache) || sscanf(line, "%s %s %d %c", f1, f2, t + 2, &letter) != 4) {
#endif
      fprintf(stderr, "Format error after string section of mob #%d\n", nr);
      exit(1);
    }

    fputs("[[mobs]]\n", out);
    fprintf(out, "vnum = %d\n", nr);
    toml_write_key_string(out, "name", name ? name : "");
    toml_write_key_string(out, "short_descr", short_descr ? short_descr : "");
    toml_write_key_string(out, "long_descr", long_descr ? long_descr : "");
    toml_write_key_string(out, "description", description ? description : "");
    toml_write_key_string(out, "mob_flags", f1);
    toml_write_key_string(out, "aff_flags", f2);
    fprintf(out, "alignment = %d\n", t[2]);
    fprintf(out, "type = \"%c\"\n", letter);

    if (!get_line_cached(fl, line, &cache)) {
      fprintf(stderr, "Format error in mob #%d\n", nr);
      exit(1);
    }
    if (sscanf(line, " %d %d %d %dd%d+%d %dd%d+%d ",
               t, t + 1, t + 2, t + 3, t + 4, t + 5, t + 6, t + 7, t + 8) != 9) {
      fprintf(stderr, "Format error in mob #%d\n", nr);
      exit(1);
    }
    fprintf(out, "level = %d\n", t[0]);
    fprintf(out, "hitroll = %d\n", 20 - t[1]);
    fprintf(out, "ac = %d\n", 10 * t[2]);
    fprintf(out, "hit = %d\n", t[3]);
    fprintf(out, "mana = %d\n", t[4]);
    fprintf(out, "move = %d\n", t[5]);
    fprintf(out, "damnodice = %d\n", t[6]);
    fprintf(out, "damsizedice = %d\n", t[7]);
    fprintf(out, "damroll = %d\n", t[8]);

    if (!get_line_cached(fl, line, &cache) || sscanf(line, " %d %d ", t, t + 1) != 2) {
      fprintf(stderr, "Format error in mob #%d\n", nr);
      exit(1);
    }
    fprintf(out, "gold = %d\n", t[0]);
    fprintf(out, "exp = %d\n", t[1]);

    if (!get_line_cached(fl, line, &cache) || sscanf(line, " %d %d %d ", t, t + 1, t + 2) != 3) {
      fprintf(stderr, "Format error in mob #%d\n", nr);
      exit(1);
    }
    fprintf(out, "position = %d\n", t[0]);
    fprintf(out, "default_pos = %d\n", t[1]);
    fprintf(out, "sex = %d\n", t[2]);

    if (UPPER(letter) == 'E') {
      while (get_line_cached(fl, line, &cache)) {
        if (!strcmp(line, "E"))
          break;
        if (*line == '#') {
          push_line_cached(line, &cache);
          break;
        }
        {
          char *ptr = strchr(line, ':');
          if (ptr) {
            *ptr++ = '\0';
            while (isspace(*ptr))
              ptr++;
            if (*ptr) {
              char *end = ptr;
              while (*end && isdigit(*end))
                end++;
              if (*end == '\0')
                fprintf(out, "espec.%s = %s\n", line, ptr);
              else {
                fprintf(out, "espec.%s = ", line);
                toml_write_escaped(out, ptr);
                fputc('\n', out);
              }
            }
          }
        }
      }
    }

    if (name)
      free(name);
    if (short_descr)
      free(short_descr);
    if (long_descr)
      free(long_descr);
    if (description)
      free(description);
  }

  fclose(fl);
  fclose(out);
}

static void convert_obj_file(const char *infile, const char *outfile)
{
  FILE *fl, *out;
  char line[READ_SIZE], f1[READ_SIZE], f2[READ_SIZE], buf2[128];
  struct line_cache cache;

  cache.has_line = 0;

  if (!(fl = fopen(infile, "r"))) {
    fprintf(stderr, "Unable to open %s\n", infile);
    exit(1);
  }
  if (!(out = fopen(outfile, "w"))) {
    fclose(fl);
    fprintf(stderr, "Unable to write %s\n", outfile);
    exit(1);
  }

  for (;;) {
    int nr = -1, values[4], obj_type, weight, cost, rent;
    char *name, *short_descr, *description, *action;

    if (!get_line_cached(fl, line, &cache))
      break;
    if (*line == '$')
      break;
    if (*line != '#')
      continue;

    if (sscanf(line, "#%d", &nr) != 1)
      continue;
    if (nr >= 99999)
      break;

    snprintf(buf2, sizeof(buf2), "object #%d", nr);
    name = fread_string_legacy(fl, buf2);
    short_descr = fread_string_legacy(fl, buf2);
    description = fread_string_legacy(fl, buf2);
    action = fread_string_legacy(fl, buf2);

    if (!get_line_cached(fl, line, &cache) || sscanf(line, " %d %s %s", &obj_type, f1, f2) != 3) {
      fprintf(stderr, "Format error in object #%d\n", nr);
      exit(1);
    }
    if (!get_line_cached(fl, line, &cache) || sscanf(line, "%d %d %d %d", &values[0], &values[1],
                                                     &values[2], &values[3]) != 4) {
      fprintf(stderr, "Format error in object #%d\n", nr);
      exit(1);
    }
    if (!get_line_cached(fl, line, &cache) || sscanf(line, "%d %d %d", &weight, &cost, &rent) != 3) {
      fprintf(stderr, "Format error in object #%d\n", nr);
      exit(1);
    }

    fputs("[[objects]]\n", out);
    fprintf(out, "vnum = %d\n", nr);
    toml_write_key_string(out, "name", name ? name : "");
    toml_write_key_string(out, "short_descr", short_descr ? short_descr : "");
    toml_write_key_string(out, "description", description ? description : "");
    if (action)
      toml_write_key_string(out, "action_description", action);
    fprintf(out, "type = %d\n", obj_type);
    toml_write_key_string(out, "extra_flags", f1);
    toml_write_key_string(out, "wear_flags", f2);
    toml_write_int_array(out, "values", values, 4);
    fprintf(out, "weight = %d\n", weight);
    fprintf(out, "cost = %d\n", cost);
    fprintf(out, "rent = %d\n", rent);

    for (;;) {
      if (!get_line_cached(fl, line, &cache)) {
        fprintf(stderr, "Format error in object #%d\n", nr);
        exit(1);
      }
      switch (*line) {
      case 'E': {
        char *kw = fread_string_legacy(fl, buf2);
        char *desc = fread_string_legacy(fl, buf2);
        fputs("\n[[objects.extra_desc]]\n", out);
        toml_write_key_string(out, "keyword", kw ? kw : "");
        toml_write_key_string(out, "description", desc ? desc : "");
        if (kw)
          free(kw);
        if (desc)
          free(desc);
        break;
      }
      case 'A': {
        int a1, a2;
        if (!get_line_cached(fl, line, &cache) || sscanf(line, " %d %d ", &a1, &a2) != 2) {
          fprintf(stderr, "Format error in object #%d\n", nr);
          exit(1);
        }
        fputs("\n[[objects.affects]]\n", out);
        fprintf(out, "location = %d\n", a1);
        fprintf(out, "modifier = %d\n", a2);
        break;
      }
      case '$':
      case '#':
        if (*line == '#')
          push_line_cached(line, &cache);
        goto done_obj;
      default:
        fprintf(stderr, "Format error in object #%d\n", nr);
        exit(1);
      }
    }
  done_obj:
    if (name)
      free(name);
    if (short_descr)
      free(short_descr);
    if (description)
      free(description);
    if (action)
      free(action);
  }

  fclose(fl);
  fclose(out);
}

static void convert_zon_file(const char *infile, const char *outfile)
{
  FILE *fl, *out;
  char buf[READ_SIZE], zname[READ_SIZE];
  int line_num, tmp;

  if (!(fl = fopen(infile, "r"))) {
    fprintf(stderr, "Unable to open %s\n", infile);
    exit(1);
  }
  if (!(out = fopen(outfile, "w"))) {
    fclose(fl);
    fprintf(stderr, "Unable to write %s\n", outfile);
    exit(1);
  }

  strlcpy(zname, infile, sizeof(zname));

  for (;;) {
    int vnum, bot, top, lifespan, reset_mode;
    char *ptr, *name;

    line_num = get_line_legacy(fl, buf);
    if (!line_num)
      break;
    if (sscanf(buf, "#%d", &vnum) != 1)
      break;

    line_num += get_line_legacy(fl, buf);
    if ((ptr = strchr(buf, '~')) != NULL)
      *ptr = '\0';
    name = strdup(buf);

    line_num += get_line_legacy(fl, buf);
    if (sscanf(buf, " %d %d %d %d ", &bot, &top, &lifespan, &reset_mode) != 4) {
      fprintf(stderr, "Format error in %s\n", zname);
      exit(1);
    }

    fputs("[[zones]]\n", out);
    fprintf(out, "vnum = %d\n", vnum);
    toml_write_key_string(out, "name", name ? name : "");
    fprintf(out, "bot = %d\n", bot);
    fprintf(out, "top = %d\n", top);
    fprintf(out, "lifespan = %d\n", lifespan);
    fprintf(out, "reset_mode = %d\n", reset_mode);

    for (;;) {
      if ((tmp = get_line_legacy(fl, buf)) == 0) {
        fprintf(stderr, "Format error in %s\n", zname);
        exit(1);
      }
      ptr = buf;
      while (*ptr && isspace(*ptr))
        ptr++;

      if (*ptr == '*')
        continue;

      if (*ptr == 'S' || *ptr == '$')
        break;

      fputs("\n[[zones.commands]]\n", out);
      fprintf(out, "command = \"%c\"\n", *ptr);
      ptr++;
      if (strchr("MOEPD", *(ptr - 1)) == NULL) {
        int if_flag, arg1, arg2;
        if (sscanf(ptr, " %d %d %d ", &if_flag, &arg1, &arg2) != 3) {
          fprintf(stderr, "Format error in %s\n", zname);
          exit(1);
        }
        fprintf(out, "if_flag = %d\n", if_flag);
        fprintf(out, "arg1 = %d\n", arg1);
        fprintf(out, "arg2 = %d\n", arg2);
      } else {
        int if_flag, arg1, arg2, arg3;
        if (sscanf(ptr, " %d %d %d %d ", &if_flag, &arg1, &arg2, &arg3) != 4) {
          fprintf(stderr, "Format error in %s\n", zname);
          exit(1);
        }
        fprintf(out, "if_flag = %d\n", if_flag);
        fprintf(out, "arg1 = %d\n", arg1);
        fprintf(out, "arg2 = %d\n", arg2);
        fprintf(out, "arg3 = %d\n", arg3);
      }
    }

    if (name)
      free(name);
    if (*buf == '$')
      break;
  }

  fclose(fl);
  fclose(out);
}

static int read_line(FILE *shop_f, const char *string, void *data, int shop_nr)
{
  char buf[READ_SIZE];
  char *endptr;

  if (!get_line_legacy(shop_f, buf)) {
    fprintf(stderr, "SYSERR: Error in shop #%d, near '%s' with '%s'\n", shop_nr, buf, string);
    exit(1);
  }

  if (!strcmp(string, "%d")) {
    long val = strtol(buf, &endptr, 10);
    if (endptr == buf) {
      fprintf(stderr, "SYSERR: Error in shop #%d, near '%s' with '%s'\n", shop_nr, buf, string);
      exit(1);
    }
    *((int *)data) = (int)val;
  } else if (!strcmp(string, "%hd")) {
    long val = strtol(buf, &endptr, 10);
    if (endptr == buf) {
      fprintf(stderr, "SYSERR: Error in shop #%d, near '%s' with '%s'\n", shop_nr, buf, string);
      exit(1);
    }
    *((sh_int *)data) = (sh_int)val;
  } else if (!strcmp(string, "%f")) {
    float val = (float)strtod(buf, &endptr);
    if (endptr == buf) {
      fprintf(stderr, "SYSERR: Error in shop #%d, near '%s' with '%s'\n", shop_nr, buf, string);
      exit(1);
    }
    *((float *)data) = val;
  } else {
    fprintf(stderr, "SYSERR: Error in shop #%d, unknown format '%s'\n", shop_nr, string);
    exit(1);
  }

  return (1);
}

static int add_to_list(struct shop_buy_data *list, int type, int *len, int *val)
{
  (void)type;

  if (*val != NOTHING) {
    if (*len < MAX_SHOP_OBJ) {
      BUY_TYPE(list[*len]) = *val;
      BUY_WORD(list[(*len)++]) = NULL;
      return (FALSE);
    } else
      return (TRUE);
  }
  return (FALSE);
}

static int end_read_list(struct shop_buy_data *list, int len, int error)
{
  if (error)
    fprintf(stderr, "SYSERR: Raise MAX_SHOP_OBJ constant in shop.h to %d\n", len + error);
  BUY_WORD(list[len]) = NULL;
  BUY_TYPE(list[len++]) = NOTHING;
  return (len);
}

static int read_list(FILE *shop_f, struct shop_buy_data *list, int new_format,
                     int max, int type, int shop_nr)
{
  int count, temp, len = 0, error = 0;

  if (new_format) {
    for (;;) {
      read_line(shop_f, "%d", &temp, shop_nr);
      if (temp < 0)
        break;
      error += add_to_list(list, type, &len, &temp);
    }
  } else
    for (count = 0; count < max; count++) {
      read_line(shop_f, "%d", &temp, shop_nr);
      error += add_to_list(list, type, &len, &temp);
    }
  return (end_read_list(list, len, error));
}

static int read_type_list(FILE *shop_f, struct shop_buy_data *list, int new_format,
                          int max, int shop_nr)
{
  int tindex, num, len = 0, error = 0;
  char *ptr;
  char buf[MAX_STRING_LENGTH];

  if (!new_format)
    return (read_list(shop_f, list, 0, max, LIST_TRADE, shop_nr));

  do {
    if (!fgets(buf, sizeof(buf), shop_f)) {
      fprintf(stderr, "SYSERR: Error in shop #%d while reading type list.\n", shop_nr);
      exit(1);
    }
    if ((ptr = strchr(buf, ';')) != NULL)
      *ptr = '\0';
    else
      *(END_OF(buf) - 1) = '\0';

    num = NOTHING;

    if (strncmp(buf, "-1", 4) != 0)
      for (tindex = 0; *item_types[tindex] != '\n'; tindex++)
        if (!strn_cmp(item_types[tindex], buf, strlen(item_types[tindex]))) {
          num = tindex;
          break;
        }

    error += add_to_list(list, LIST_TRADE, &len, &num);
  } while (num >= 0);

  return (end_read_list(list, len, error));
}

static char *read_shop_message(int mnum, FILE *shop_f, const char *why)
{
  int cht, ss = 0, ds = 0, err = 0;
  char *tbuf;

  if (!(tbuf = fread_string_legacy(shop_f, why)))
    return (NULL);

  for (cht = 0; tbuf[cht]; cht++) {
    if (tbuf[cht] != '%')
      continue;

    if (tbuf[cht + 1] == 's')
      ss++;
    else if (tbuf[cht + 1] == 'd' && (mnum == 5 || mnum == 6)) {
      if (ss == 0) {
        err++;
      }
      ds++;
    } else if (tbuf[cht + 1] != '%')
      err++;
  }

  if (ss > 1 || ds > 1)
    err++;

  if (err) {
    free(tbuf);
    return (NULL);
  }
  return (tbuf);
}

static void convert_shp_file(const char *infile, const char *outfile)
{
  FILE *shop_f, *out;
  char *buf, buf2[256];
  int temp, count, new_format = FALSE;
  struct shop_buy_data list[MAX_SHOP_OBJ + 1];
  int done = FALSE;

  if (!(shop_f = fopen(infile, "r"))) {
    fprintf(stderr, "Unable to open %s\n", infile);
    exit(1);
  }
  if (!(out = fopen(outfile, "w"))) {
    fclose(shop_f);
    fprintf(stderr, "Unable to write %s\n", outfile);
    exit(1);
  }

  strlcpy(buf2, "beginning of shop file ", sizeof(buf2));
  strlcat(buf2, infile, sizeof(buf2));

  while (!done) {
    int shop_vnum_val;
    int list_len;
    int trade_count, trade_types[MAX_SHOP_OBJ];
    char *trade_words[MAX_SHOP_OBJ];
    float buy_profit, sell_profit;
    int broke_temper, bitvector, trade_with;
    sh_int keeper;
    int open1, close1, open2, close2;
    char *msgs[7];

    buf = fread_string_legacy(shop_f, buf2);
    if (*buf == '#') {
      sscanf(buf, "#%d\n", &temp);
      free(buf);

      shop_vnum_val = temp;

      fputs("[[shops]]\n", out);
      fprintf(out, "vnum = %d\n", shop_vnum_val);

      list_len = read_list(shop_f, list, new_format, MAX_PROD, LIST_PRODUCE, shop_vnum_val);
      fprintf(out, "producing = [");
      for (count = 0; count < list_len; count++) {
        if (count)
          fputs(", ", out);
        fprintf(out, "%d", BUY_TYPE(list[count]));
      }
      fputs("]\n", out);

      read_line(shop_f, "%f", &buy_profit, shop_vnum_val);
      read_line(shop_f, "%f", &sell_profit, shop_vnum_val);
      fprintf(out, "buy_profit = %0.2f\n", buy_profit);
      fprintf(out, "sell_profit = %0.2f\n", sell_profit);

      list_len = read_type_list(shop_f, list, new_format, MAX_TRADE, shop_vnum_val);
      trade_count = list_len;
      for (count = 0; count < trade_count; count++) {
        trade_types[count] = BUY_TYPE(list[count]);
        if (BUY_WORD(list[count]))
          trade_words[count] = strdup(BUY_WORD(list[count]));
        else
          trade_words[count] = NULL;
      }

      msgs[0] = read_shop_message(0, shop_f, buf2);
      msgs[1] = read_shop_message(1, shop_f, buf2);
      msgs[2] = read_shop_message(2, shop_f, buf2);
      msgs[3] = read_shop_message(3, shop_f, buf2);
      msgs[4] = read_shop_message(4, shop_f, buf2);
      msgs[5] = read_shop_message(5, shop_f, buf2);
      msgs[6] = read_shop_message(6, shop_f, buf2);

      read_line(shop_f, "%d", &broke_temper, shop_vnum_val);
      read_line(shop_f, "%d", &bitvector, shop_vnum_val);
      read_line(shop_f, "%hd", &keeper, shop_vnum_val);
      read_line(shop_f, "%d", &trade_with, shop_vnum_val);
      fprintf(out, "broke_temper = %d\n", broke_temper);
      fprintf(out, "bitvector = %d\n", bitvector);
      fprintf(out, "keeper = %d\n", keeper);
      fprintf(out, "trade_with = %d\n", trade_with);

      list_len = read_list(shop_f, list, new_format, 1, LIST_ROOM, shop_vnum_val);
      fprintf(out, "rooms = [");
      for (count = 0; count < list_len; count++) {
        if (count)
          fputs(", ", out);
        fprintf(out, "%d", BUY_TYPE(list[count]));
      }
      fputs("]\n", out);

      read_line(shop_f, "%d", &open1, shop_vnum_val);
      read_line(shop_f, "%d", &close1, shop_vnum_val);
      read_line(shop_f, "%d", &open2, shop_vnum_val);
      read_line(shop_f, "%d", &close2, shop_vnum_val);
      fprintf(out, "open1 = %d\n", open1);
      fprintf(out, "close1 = %d\n", close1);
      fprintf(out, "open2 = %d\n", open2);
      fprintf(out, "close2 = %d\n", close2);

      for (count = 0; count < trade_count; count++) {
        fputs("\n[[shops.trade]]\n", out);
        fprintf(out, "type = %d\n", trade_types[count]);
        if (trade_words[count]) {
          toml_write_key_string(out, "word", trade_words[count]);
          free(trade_words[count]);
        }
      }

      if (msgs[0] || msgs[1] || msgs[2] || msgs[3] || msgs[4] || msgs[5] || msgs[6]) {
        fputs("\n[shops.messages]\n", out);
      }
      if (msgs[0]) {
        toml_write_key_string(out, "no_such_item1", msgs[0]);
        free(msgs[0]);
      }
      if (msgs[1]) {
        toml_write_key_string(out, "no_such_item2", msgs[1]);
        free(msgs[1]);
      }
      if (msgs[2]) {
        toml_write_key_string(out, "do_not_buy", msgs[2]);
        free(msgs[2]);
      }
      if (msgs[3]) {
        toml_write_key_string(out, "missing_cash1", msgs[3]);
        free(msgs[3]);
      }
      if (msgs[4]) {
        toml_write_key_string(out, "missing_cash2", msgs[4]);
        free(msgs[4]);
      }
      if (msgs[5]) {
        toml_write_key_string(out, "message_buy", msgs[5]);
        free(msgs[5]);
      }
      if (msgs[6]) {
        toml_write_key_string(out, "message_sell", msgs[6]);
        free(msgs[6]);
      }
    } else {
      if (*buf == '$')
        done = TRUE;
      else if (strstr(buf, VERSION3_TAG))
        new_format = TRUE;
      free(buf);
    }
  }

  fclose(shop_f);
  fclose(out);
}

static void replace_ext(const char *in, char *out, size_t outsz, const char *ext)
{
  const char *dot = strrchr(in, '.');

  if (dot) {
    size_t len = (size_t)(dot - in);
    if (len + strlen(ext) + 2 > outsz)
      len = outsz - strlen(ext) - 2;
    strncpy(out, in, len);
    out[len] = '\0';
    snprintf(out + len, outsz - len, ".%s", ext);
  } else {
    snprintf(out, outsz, "%s.%s", in, ext);
  }
}

static int build_path(char *out, size_t outsz, const char *a, const char *b)
{
  size_t need = strlen(a) + strlen(b) + 1;

  if (need > outsz)
    return (0);

  strlcpy(out, a, outsz);
  strlcat(out, b, outsz);
  return (1);
}

static int build_path3(char *out, size_t outsz, const char *a, const char *b, const char *c)
{
  size_t need = strlen(a) + strlen(b) + strlen(c) + 1;

  if (need > outsz)
    return (0);

  strlcpy(out, a, outsz);
  strlcat(out, b, outsz);
  strlcat(out, c, outsz);
  return (1);
}

static int has_ext(const char *name, const char *ext)
{
  size_t len = strlen(name);
  size_t extlen = strlen(ext);

  if (len <= extlen + 1)
    return (0);
  if (name[len - extlen - 1] != '.')
    return (0);

  return (!strcmp(name + len - extlen, ext));
}

static const char *legacy_ext_for_mode(int mode)
{
  switch (mode) {
  case MODE_WLD:
    return ("wld");
  case MODE_MOB:
    return ("mob");
  case MODE_OBJ:
    return ("obj");
  case MODE_ZON:
    return ("zon");
  case MODE_SHP:
    return ("shp");
  default:
    return ("");
  }
}

static void convert_index_dir(const char *dir, const char *index_file,
                              int mode, const char *ext)
{
  char index_path[PATH_MAX], tmp_path[PATH_MAX];
  FILE *idx, *tmp;
  char entry[MAX_STRING_LENGTH];

  snprintf(index_path, sizeof(index_path), "%s%s", dir, index_file);
  snprintf(tmp_path, sizeof(tmp_path), "%s%s.tmp", dir, index_file);

  if (!(idx = fopen(index_path, "r"))) {
    fprintf(stderr, "Unable to open %s\n", index_path);
    exit(1);
  }
  if (!(tmp = fopen(tmp_path, "w"))) {
    fclose(idx);
    fprintf(stderr, "Unable to write %s\n", tmp_path);
    exit(1);
  }

  while (fscanf(idx, "%s", entry) == 1) {
    if (*entry == '$') {
      fprintf(tmp, "$\n");
      break;
    }

    {
      char infile[PATH_MAX], outfile[PATH_MAX], outentry[MAX_STRING_LENGTH];
      char infile_entry[MAX_STRING_LENGTH];

      if (has_ext(entry, "toml")) {
        replace_ext(entry, infile_entry, sizeof(infile_entry), legacy_ext_for_mode(mode));
      } else
        strlcpy(infile_entry, entry, sizeof(infile_entry));

      if (!build_path(infile, sizeof(infile), dir, infile_entry))
        continue;
      replace_ext(entry, outentry, sizeof(outentry), ext);
      if (!build_path(outfile, sizeof(outfile), dir, outentry))
        continue;

      switch (mode) {
      case MODE_WLD:
        convert_wld_file(infile, outfile);
        break;
      case MODE_MOB:
        convert_mob_file(infile, outfile);
        break;
      case MODE_OBJ:
        convert_obj_file(infile, outfile);
        break;
      case MODE_ZON:
        convert_zon_file(infile, outfile);
        break;
      case MODE_SHP:
        convert_shp_file(infile, outfile);
        break;
      }

      fprintf(tmp, "%s\n", outentry);
    }
  }

  fclose(idx);
  fclose(tmp);

  rename(index_path, index_path); /* no-op to ensure path exists */
  if (remove(index_path) < 0) {
    fprintf(stderr, "Unable to remove %s\n", index_path);
    exit(1);
  }
  if (rename(tmp_path, index_path) < 0) {
    fprintf(stderr, "Unable to replace %s\n", index_path);
    exit(1);
  }
}


static void write_obj_file_elem_toml(FILE *fp, struct obj_file_elem *object)
{
  int i;

  fputs("\n[[objects]]\n", fp);
  fprintf(fp, "item_number = %d\n", object->item_number);
#if USE_AUTOEQ
  fprintf(fp, "location = %d\n", object->location);
#endif
  fprintf(fp, "values = [%d, %d, %d, %d]\n",
          object->value[0], object->value[1], object->value[2], object->value[3]);
  fprintf(fp, "extra_flags = %d\n", object->extra_flags);
  fprintf(fp, "weight = %d\n", object->weight);
  fprintf(fp, "timer = %d\n", object->timer);
  fprintf(fp, "bitvector = %ld\n", object->bitvector);

  for (i = 0; i < MAX_OBJ_AFFECT; i++) {
    if (object->affected[i].location == APPLY_NONE && object->affected[i].modifier == 0)
      continue;
    fputs("\n[[objects.affects]]\n", fp);
    fprintf(fp, "location = %d\n", object->affected[i].location);
    fprintf(fp, "modifier = %d\n", object->affected[i].modifier);
  }
}

static void convert_objfile_to_toml(const char *infile, const char *outfile, int has_rent)
{
  FILE *fl, *out;
  struct rent_info rent;
  struct obj_file_elem object;

  if (!(fl = fopen(infile, "rb")))
    return;
  if (!(out = fopen(outfile, "w"))) {
    fclose(fl);
    return;
  }

  if (has_rent) {
    if (fread(&rent, sizeof(rent), 1, fl) != 1) {
      fclose(fl);
      fclose(out);
      return;
    }

    fputs("[rent]\n", out);
    fprintf(out, "time = %d\n", rent.time);
    fprintf(out, "rentcode = %d\n", rent.rentcode);
    fprintf(out, "net_cost_per_diem = %d\n", rent.net_cost_per_diem);
    fprintf(out, "gold = %d\n", rent.gold);
    fprintf(out, "account = %d\n", rent.account);
    fprintf(out, "nitems = %d\n", rent.nitems);
    fprintf(out, "spare0 = %d\n", rent.spare0);
    fprintf(out, "spare1 = %d\n", rent.spare1);
    fprintf(out, "spare2 = %d\n", rent.spare2);
    fprintf(out, "spare3 = %d\n", rent.spare3);
    fprintf(out, "spare4 = %d\n", rent.spare4);
    fprintf(out, "spare5 = %d\n", rent.spare5);
    fprintf(out, "spare6 = %d\n", rent.spare6);
    fprintf(out, "spare7 = %d\n", rent.spare7);
  }

  while (fread(&object, sizeof(object), 1, fl) == 1)
    write_obj_file_elem_toml(out, &object);

  fclose(fl);
  fclose(out);
}

static void convert_plrobjs(void)
{
  static const char *plr_dirs[] = {"A-E", "F-J", "K-O", "P-T", "U-Z", "ZZZ", NULL};
  int i;

  for (i = 0; plr_dirs[i]; i++) {
    DIR *dir;
    struct dirent *ent;
    char path[PATH_MAX];

    snprintf(path, sizeof(path), "%s%s", LIB_PLROBJS, plr_dirs[i]);
    if (!(dir = opendir(path)))
      continue;

    while ((ent = readdir(dir)) != NULL) {
      size_t len = strlen(ent->d_name);
      char infile[PATH_MAX], outfile[PATH_MAX], outentry[PATH_MAX];

      if (len <= 5 || strcmp(ent->d_name + len - 5, ".objs"))
        continue;

      if (!build_path3(infile, sizeof(infile), path, SLASH, ent->d_name))
        continue;
      replace_ext(ent->d_name, outentry, sizeof(outentry), "toml");
      if (!build_path3(outfile, sizeof(outfile), path, SLASH, outentry))
        continue;
      convert_objfile_to_toml(infile, outfile, 1);
    }
    closedir(dir);
  }
}

static void convert_house_files(void)
{
  DIR *dir;
  struct dirent *ent;

  if (!(dir = opendir(LIB_HOUSE)))
    return;

  while ((ent = readdir(dir)) != NULL) {
    size_t len = strlen(ent->d_name);
    char infile[PATH_MAX], outfile[PATH_MAX], outname[MAX_STRING_LENGTH];

    if (len <= 6 || strcmp(ent->d_name + len - 6, ".house"))
      continue;

    if (!build_path(infile, sizeof(infile), LIB_HOUSE, ent->d_name))
      continue;
    replace_ext(ent->d_name, outname, sizeof(outname), "toml");
    if (!build_path(outfile, sizeof(outfile), LIB_HOUSE, outname))
      continue;
    convert_objfile_to_toml(infile, outfile, 0);
  }

  closedir(dir);
}

int main(int argc, char **argv)
{
  (void)argc;
  (void)argv;

  convert_index_dir(WLD_PREFIX, INDEX_FILE, MODE_WLD, "toml");
  convert_index_dir(MOB_PREFIX, INDEX_FILE, MODE_MOB, "toml");
  convert_index_dir(OBJ_PREFIX, INDEX_FILE, MODE_OBJ, "toml");
  convert_index_dir(ZON_PREFIX, INDEX_FILE, MODE_ZON, "toml");
  convert_index_dir(SHP_PREFIX, INDEX_FILE, MODE_SHP, "toml");

  convert_plrobjs();
  convert_house_files();

  printf("TOML migration complete.\n");
  return (0);
}
