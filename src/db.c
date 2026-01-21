/* ************************************************************************
*   File: db.c                                          Part of CircleMUD *
*  Usage: Loading/saving chars, booting/resetting world, internal funcs   *
*                                                                         *
*  All rights reserved.  See license.doc for complete information.        *
*                                                                         *
*  Copyright (C) 1993, 94 by the Trustees of the Johns Hopkins University *
*  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
************************************************************************ */

#define __DB_C__

#include "conf.h"
#include "sysdep.h"

#include <dirent.h>
#include <sys/stat.h>

#include "structs.h"
#include "utils.h"
#include "db.h"
#include "comm.h"
#include "handler.h"
#include "spells.h"
#include "mail.h"
#include "interpreter.h"
#include "house.h"
#include "constants.h"
#include "toml.h"

/**************************************************************************
*  declarations of most of the 'global' variables                         *
**************************************************************************/

struct room_data *world = NULL;	/* array of rooms		 */
room_rnum top_of_world = 0;	/* ref to top element of world	 */

struct char_data *character_list = NULL;	/* global linked list of
						 * chars	 */
struct index_data *mob_index;	/* index table for mobile file	 */
struct char_data *mob_proto;	/* prototypes for mobs		 */
mob_rnum top_of_mobt = 0;	/* top of mobile index table	 */

struct obj_data *object_list = NULL;	/* global linked list of objs	 */
struct index_data *obj_index;	/* index table for object file	 */
struct obj_data *obj_proto;	/* prototypes for objs		 */
obj_rnum top_of_objt = 0;	/* top of object index table	 */

struct zone_data *zone_table;	/* zone table			 */
zone_rnum top_of_zone_table = 0;/* top element of zone tab	 */
struct message_list fight_messages[MAX_MESSAGES];	/* fighting messages	 */

struct player_index_element *player_table = NULL;	/* index to plr file	 */
FILE *player_fl = NULL;		/* file desc of player file	 */
int top_of_p_table = 0;		/* ref to top of table		 */
long top_idnum = 0;		/* highest idnum in use		 */

int no_mail = 0;		/* mail disabled?		 */
int mini_mud = 0;		/* mini-mud mode?		 */
int no_rent_check = 0;		/* skip rent check on boot?	 */
time_t boot_time = 0;		/* time of mud boot		 */
int circle_restrict = 0;	/* level of game restriction	 */
room_rnum r_mortal_start_room;	/* rnum of mortal start room	 */
room_rnum r_immort_start_room;	/* rnum of immort start room	 */
room_rnum r_frozen_start_room;	/* rnum of frozen start room	 */

char *credits = NULL;		/* game credits			 */
char *news = NULL;		/* mud news			 */
char *motd = NULL;		/* message of the day - mortals */
char *imotd = NULL;		/* message of the day - immorts */
char *GREETINGS = NULL;		/* opening credits screen	*/
char *help = NULL;		/* help screen			 */
char *info = NULL;		/* info page			 */
char *wizlist = NULL;		/* list of higher gods		 */
char *immlist = NULL;		/* list of peon gods		 */
char *background = NULL;	/* background story		 */
char *handbook = NULL;		/* handbook for new immortals	 */
char *policies = NULL;		/* policies page		 */

struct help_index_element *help_table = 0;	/* the help table	 */
int top_of_helpt = 0;		/* top of help index table	 */

struct time_info_data time_info;/* the infomation about the time    */
struct weather_data weather_info;	/* the infomation about the weather */
struct player_special_data dummy_mob;	/* dummy spec area for mobs	*/
struct reset_q_type reset_q;	/* queue of zones to be reset	 */

/* local functions */
int check_bitvector_names(bitvector_t bits, size_t namecount, const char *whatami, const char *whatbits);
int check_object_spell_number(struct obj_data *obj, int val);
int check_object_level(struct obj_data *obj, int val);
void setup_dir(FILE *fl, int room, int dir);
void index_boot(int mode);
void discrete_load(FILE *fl, int mode, char *filename);
int check_object(struct obj_data *);
void parse_room(FILE *fl, int virtual_nr);
void parse_room_toml(toml_table_t *room, const char *filename);
void parse_mobile(FILE *mob_f, int nr);
void parse_mobile_toml(toml_table_t *mob, const char *filename);
char *parse_object(FILE *obj_f, int nr);
void parse_object_toml(toml_table_t *obj, const char *filename);
void load_zones(FILE *fl, char *zonename);
void load_help(FILE *fl);
void assign_mobiles(void);
void assign_objects(void);
void assign_rooms(void);
void assign_the_shopkeepers(void);
void build_player_index(void);
int is_empty(zone_rnum zone_nr);
void reset_zone(zone_rnum zone);
int file_to_string(const char *name, char *buf);
int file_to_string_alloc(const char *name, char **buf);
void reboot_wizlists(void);
ACMD(do_reboot);
void boot_world(void);
int count_alias_records(FILE *fl);
int count_hash_records(FILE *fl);
int count_toml_records(FILE *fl, int mode, const char *filename);
bitvector_t asciiflag_conv(char *flag);
void parse_simple_mob(FILE *mob_f, int i, int nr);
void interpret_espec(const char *keyword, const char *value, int i, int nr);
void parse_espec(char *buf, int i, int nr);
void parse_enhanced_mob(FILE *mob_f, int i, int nr);
void get_one_line(FILE *fl, char *buf);
void save_etext(struct char_data *ch);
void check_start_rooms(void);
void renum_world(void);
void renum_zone_table(void);
void log_zone_error(zone_rnum zone, int cmd_no, const char *message);
void reset_time(void);
long get_ptable_by_name(const char *name);

/* external functions */
void paginate_string(char *str, struct descriptor_data *d);
struct time_info_data *mud_time_passed(time_t t2, time_t t1);
void free_alias(struct alias_data *a);
void load_messages(void);
void weather_and_time(int mode);
void mag_assign_spells(void);
void boot_social_messages(void);
void update_obj_file(void);	/* In objsave.c */
void sort_commands(void);
void sort_spells(void);
void load_banned(void);
void Read_Invalid_List(void);
void boot_the_shops(FILE *shop_f, char *filename, int rec_count);
int hsort(const void *a, const void *b);
static int read_player_toml_file(const char *filename, struct char_file_u *st);
void prune_crlf(char *txt);
void destroy_shops(void);

/* external vars */
extern int no_specials;
extern int scheck;
extern room_vnum mortal_start_room;
extern room_vnum immort_start_room;
extern room_vnum frozen_start_room;
extern struct descriptor_data *descriptor_list;
extern const char *unused_spellname;	/* spell_parser.c */

/*************************************************************************
*  routines for booting the system                                       *
*************************************************************************/

/* this is necessary for the autowiz system */
void reboot_wizlists(void)
{
  file_to_string_alloc(WIZLIST_FILE, &wizlist);
  file_to_string_alloc(IMMLIST_FILE, &immlist);
}


/* Wipe out all the loaded text files, for shutting down. */
void free_text_files(void)
{
  char **textfiles[] = {
	&wizlist, &immlist, &news, &credits, &motd, &imotd, &help, &info,
	&policies, &handbook, &background, &GREETINGS, NULL
  };
  int rf;

  for (rf = 0; textfiles[rf]; rf++)
    if (*textfiles[rf]) {
      free(*textfiles[rf]);
      *textfiles[rf] = NULL;
    }
}


/*
 * Too bad it doesn't check the return values to let the user
 * know about -1 values.  This will result in an 'Okay.' to a
 * 'reload' command even when the string was not replaced.
 * To fix later, if desired. -gg 6/24/99
 */
ACMD(do_reboot)
{
  char arg[MAX_INPUT_LENGTH];

  one_argument(argument, arg);

  if (!str_cmp(arg, "all") || *arg == '*') {
    if (file_to_string_alloc(GREETINGS_FILE, &GREETINGS) == 0)
      prune_crlf(GREETINGS);
    file_to_string_alloc(WIZLIST_FILE, &wizlist);
    file_to_string_alloc(IMMLIST_FILE, &immlist);
    file_to_string_alloc(NEWS_FILE, &news);
    file_to_string_alloc(CREDITS_FILE, &credits);
    file_to_string_alloc(MOTD_FILE, &motd);
    file_to_string_alloc(IMOTD_FILE, &imotd);
    file_to_string_alloc(HELP_PAGE_FILE, &help);
    file_to_string_alloc(INFO_FILE, &info);
    file_to_string_alloc(POLICIES_FILE, &policies);
    file_to_string_alloc(HANDBOOK_FILE, &handbook);
    file_to_string_alloc(BACKGROUND_FILE, &background);
  } else if (!str_cmp(arg, "wizlist"))
    file_to_string_alloc(WIZLIST_FILE, &wizlist);
  else if (!str_cmp(arg, "immlist"))
    file_to_string_alloc(IMMLIST_FILE, &immlist);
  else if (!str_cmp(arg, "news"))
    file_to_string_alloc(NEWS_FILE, &news);
  else if (!str_cmp(arg, "credits"))
    file_to_string_alloc(CREDITS_FILE, &credits);
  else if (!str_cmp(arg, "motd"))
    file_to_string_alloc(MOTD_FILE, &motd);
  else if (!str_cmp(arg, "imotd"))
    file_to_string_alloc(IMOTD_FILE, &imotd);
  else if (!str_cmp(arg, "help"))
    file_to_string_alloc(HELP_PAGE_FILE, &help);
  else if (!str_cmp(arg, "info"))
    file_to_string_alloc(INFO_FILE, &info);
  else if (!str_cmp(arg, "policy"))
    file_to_string_alloc(POLICIES_FILE, &policies);
  else if (!str_cmp(arg, "handbook"))
    file_to_string_alloc(HANDBOOK_FILE, &handbook);
  else if (!str_cmp(arg, "background"))
    file_to_string_alloc(BACKGROUND_FILE, &background);
  else if (!str_cmp(arg, "greetings")) {
    if (file_to_string_alloc(GREETINGS_FILE, &GREETINGS) == 0)
      prune_crlf(GREETINGS);
  } else if (!str_cmp(arg, "xhelp")) {
    if (help_table)
      free_help();
    index_boot(DB_BOOT_HLP);
  } else {
    send_to_char(ch, "Unknown reload option.\r\n");
    return;
  }

  send_to_char(ch, "%s", OK);
}


void boot_world(void)
{
  log("Loading zone table.");
  index_boot(DB_BOOT_ZON);

  log("Loading rooms.");
  index_boot(DB_BOOT_WLD);

  log("Renumbering rooms.");
  renum_world();

  log("Checking start rooms.");
  check_start_rooms();

  log("Loading mobs and generating index.");
  index_boot(DB_BOOT_MOB);

  log("Loading objs and generating index.");
  index_boot(DB_BOOT_OBJ);

  log("Renumbering zone table.");
  renum_zone_table();

  if (!no_specials) {
    log("Loading shops.");
    index_boot(DB_BOOT_SHP);
  }
}


void free_extra_descriptions(struct extra_descr_data *edesc)
{
  struct extra_descr_data *enext;

  for (; edesc; edesc = enext) {
    enext = edesc->next;

    free(edesc->keyword);
    free(edesc->description);
    free(edesc);
  }
}


/* Free the world, in a memory allocation sense. */
void destroy_db(void)
{
  ssize_t cnt, itr;
  struct char_data *chtmp;
  struct obj_data *objtmp;

  /* Active Mobiles & Players */
  while (character_list) {
    chtmp = character_list;
    character_list = character_list->next;
    free_char(chtmp);
  }

  /* Active Objects */
  while (object_list) {
    objtmp = object_list;
    object_list = object_list->next;
    free_obj(objtmp);
  }

  /* Rooms */
  for (cnt = 0; cnt <= top_of_world; cnt++) {
    if (world[cnt].name)
      free(world[cnt].name);
    if (world[cnt].description)
      free(world[cnt].description);
    free_extra_descriptions(world[cnt].ex_description);

    for (itr = 0; itr < NUM_OF_DIRS; itr++) {
      if (!world[cnt].dir_option[itr])
        continue;

      if (world[cnt].dir_option[itr]->general_description)
        free(world[cnt].dir_option[itr]->general_description);
      if (world[cnt].dir_option[itr]->keyword)
        free(world[cnt].dir_option[itr]->keyword);
      free(world[cnt].dir_option[itr]);
    }
  }
  free(world);

  /* Objects */
  for (cnt = 0; cnt <= top_of_objt; cnt++) {
    if (obj_proto[cnt].name)
      free(obj_proto[cnt].name);
    if (obj_proto[cnt].description)
      free(obj_proto[cnt].description);
    if (obj_proto[cnt].short_description)
      free(obj_proto[cnt].short_description);
    if (obj_proto[cnt].action_description)
      free(obj_proto[cnt].action_description);
    free_extra_descriptions(obj_proto[cnt].ex_description);
  }
  free(obj_proto);
  free(obj_index);

  /* Mobiles */
  for (cnt = 0; cnt <= top_of_mobt; cnt++) {
    if (mob_proto[cnt].player.name)
      free(mob_proto[cnt].player.name);
    if (mob_proto[cnt].player.title)
      free(mob_proto[cnt].player.title);
    if (mob_proto[cnt].player.short_descr)
      free(mob_proto[cnt].player.short_descr);
    if (mob_proto[cnt].player.long_descr)
      free(mob_proto[cnt].player.long_descr);
    if (mob_proto[cnt].player.description)
      free(mob_proto[cnt].player.description);

    while (mob_proto[cnt].affected)
      affect_remove(&mob_proto[cnt], mob_proto[cnt].affected);
  }
  free(mob_proto);
  free(mob_index);

  /* Shops */
  destroy_shops();

  /* Zones */
  for (cnt = 0; cnt <= top_of_zone_table; cnt++) {
    if (zone_table[cnt].name)
      free(zone_table[cnt].name);
    if (zone_table[cnt].cmd)
      free(zone_table[cnt].cmd);
  }
  free(zone_table);
}


/* body of the booting system */
void boot_db(void)
{
  zone_rnum i;

  log("Boot db -- BEGIN.");

  log("Resetting the game time:");
  reset_time();

  log("Reading news, credits, help, bground, info & motds.");
  file_to_string_alloc(NEWS_FILE, &news);
  file_to_string_alloc(CREDITS_FILE, &credits);
  file_to_string_alloc(MOTD_FILE, &motd);
  file_to_string_alloc(IMOTD_FILE, &imotd);
  file_to_string_alloc(HELP_PAGE_FILE, &help);
  file_to_string_alloc(INFO_FILE, &info);
  file_to_string_alloc(WIZLIST_FILE, &wizlist);
  file_to_string_alloc(IMMLIST_FILE, &immlist);
  file_to_string_alloc(POLICIES_FILE, &policies);
  file_to_string_alloc(HANDBOOK_FILE, &handbook);
  file_to_string_alloc(BACKGROUND_FILE, &background);
  if (file_to_string_alloc(GREETINGS_FILE, &GREETINGS) == 0)
    prune_crlf(GREETINGS);

  log("Loading spell definitions.");
  mag_assign_spells();

  boot_world();

  log("Loading help entries.");
  index_boot(DB_BOOT_HLP);

  log("Generating player index.");
  build_player_index();

  log("Loading fight messages.");
  load_messages();

  log("Loading social messages.");
  boot_social_messages();

  log("Assigning function pointers:");

  if (!no_specials) {
    log("   Mobiles.");
    assign_mobiles();
    log("   Shopkeepers.");
    assign_the_shopkeepers();
    log("   Objects.");
    assign_objects();
    log("   Rooms.");
    assign_rooms();
  }

  log("Assigning spell and skill levels.");
  init_spell_levels();

  log("Sorting command list and spells.");
  sort_commands();
  sort_spells();

  log("Booting mail system.");
  if (!scan_file()) {
    log("    Mail boot failed -- Mail system disabled");
    no_mail = 1;
  }
  log("Reading banned site and invalid-name list.");
  load_banned();
  Read_Invalid_List();

  if (!no_rent_check) {
    log("Deleting timed-out crash and rent files:");
    update_obj_file();
    log("   Done.");
  }

  /* Moved here so the object limit code works. -gg 6/24/98 */
  if (!mini_mud) {
    log("Booting houses.");
    House_boot();
  }

  for (i = 0; i <= top_of_zone_table; i++) {
    log("Resetting #%d: %s (rooms %d-%d).", zone_table[i].number,
	zone_table[i].name, zone_table[i].bot, zone_table[i].top);
    reset_zone(i);
  }

  reset_q.head = reset_q.tail = NULL;

  boot_time = time(0);

  log("Boot db -- DONE.");
}


/* reset the time in the game from file */
void reset_time(void)
{
  time_t beginning_of_time = 0;
  FILE *bgtime;

  if ((bgtime = fopen(TIME_FILE, "r")) == NULL)
    log("SYSERR: Can't read from '%s' time file.", TIME_FILE);
  else {
    if (fscanf(bgtime, "%ld\n", &beginning_of_time) != 1) {
      log("SYSERR: Can't read from '%s' time file.", TIME_FILE);
      beginning_of_time = 0;
    }
    fclose(bgtime);
  }
  if (beginning_of_time == 0)
    beginning_of_time = 650336715;

  time_info = *mud_time_passed(time(0), beginning_of_time);

  if (time_info.hours <= 4)
    weather_info.sunlight = SUN_DARK;
  else if (time_info.hours == 5)
    weather_info.sunlight = SUN_RISE;
  else if (time_info.hours <= 20)
    weather_info.sunlight = SUN_LIGHT;
  else if (time_info.hours == 21)
    weather_info.sunlight = SUN_SET;
  else
    weather_info.sunlight = SUN_DARK;

  log("   Current Gametime: %dH %dD %dM %dY.", time_info.hours,
	  time_info.day, time_info.month, time_info.year);

  weather_info.pressure = 960;
  if ((time_info.month >= 7) && (time_info.month <= 12))
    weather_info.pressure += dice(1, 50);
  else
    weather_info.pressure += dice(1, 80);

  weather_info.change = 0;

  if (weather_info.pressure <= 980)
    weather_info.sky = SKY_LIGHTNING;
  else if (weather_info.pressure <= 1000)
    weather_info.sky = SKY_RAINING;
  else if (weather_info.pressure <= 1020)
    weather_info.sky = SKY_CLOUDY;
  else
    weather_info.sky = SKY_CLOUDLESS;
}


/* Write the time in 'when' to the MUD-time file. */
void save_mud_time(struct time_info_data *when)
{
  FILE *bgtime;

  if ((bgtime = fopen(TIME_FILE, "w")) == NULL)
    log("SYSERR: Can't write to '%s' time file.", TIME_FILE);
  else {
    fprintf(bgtime, "%ld\n", mud_time_to_secs(when));
    fclose(bgtime);
  }
}


void free_player_index(void)
{
  int tp;

  if (!player_table)
    return;

  for (tp = 0; tp <= top_of_p_table; tp++)
    if (player_table[tp].name)
      free(player_table[tp].name);

  free(player_table);
  player_table = NULL;
  top_of_p_table = 0;
}

static const char *player_middle_dir(const char *name)
{
  switch (LOWER(*name)) {
  case 'a':  case 'b':  case 'c':  case 'd':  case 'e':
    return ("A-E");
  case 'f':  case 'g':  case 'h':  case 'i':  case 'j':
    return ("F-J");
  case 'k':  case 'l':  case 'm':  case 'n':  case 'o':
    return ("K-O");
  case 'p':  case 'q':  case 'r':  case 's':  case 't':
    return ("P-T");
  case 'u':  case 'v':  case 'w':  case 'x':  case 'y':  case 'z':
    return ("U-Z");
  default:
    return ("ZZZ");
  }
}

int get_player_filename(char *filename, size_t fbufsize, const char *orig_name)
{
  char name[PATH_MAX], *ptr;
  const char *middle;

  if (orig_name == NULL || *orig_name == '\0' || filename == NULL) {
    log("SYSERR: NULL pointer or empty string passed to get_player_filename(), %p or %p.",
		orig_name, filename);
    return (0);
  }

  strlcpy(name, orig_name, sizeof(name));
  for (ptr = name; *ptr; ptr++)
    *ptr = LOWER(*ptr);

  middle = player_middle_dir(name);

  snprintf(filename, fbufsize, "%s%s"SLASH"%s.%s", PLAYER_DIR, middle, name, SUF_PLAYER);
  return (1);
}

static void ensure_player_dirs(const char *name)
{
  const char *middle = player_middle_dir(name);
  char path[PATH_MAX];
  size_t len;

  snprintf(path, sizeof(path), "%s", PLAYER_DIR);
  len = strlen(path);
  if (len > 0 && path[len - 1] == SLASH[0])
    path[len - 1] = '\0';

  if (mkdir(path, 0755) < 0 && errno != EEXIST)
    log("SYSERR: unable to create player dir %s: %s", path, strerror(errno));

  snprintf(path, sizeof(path), "%s%s", PLAYER_DIR, middle);
  if (mkdir(path, 0755) < 0 && errno != EEXIST)
    log("SYSERR: unable to create player subdir %s: %s", path, strerror(errno));
}

static int count_player_files(void)
{
  static const char *plr_dirs[] = {"A-E", "F-J", "K-O", "P-T", "U-Z", "ZZZ", NULL};
  int count = 0;
  int i;

  for (i = 0; plr_dirs[i]; i++) {
    DIR *dir;
    struct dirent *ent;
    char path[PATH_MAX];

    snprintf(path, sizeof(path), "%s%s", PLAYER_DIR, plr_dirs[i]);
    if (!(dir = opendir(path)))
      continue;

    while ((ent = readdir(dir)) != NULL) {
      size_t len = strlen(ent->d_name);

      if (len > 5 && !strcmp(ent->d_name + len - 5, ".toml"))
	count++;
    }
    closedir(dir);
  }

  return (count);
}

static void add_player_index_entry(const struct char_file_u *st)
{
  int pos, i;

  if (top_of_p_table == -1)
    pos = top_of_p_table = 0;
  else
    pos = ++top_of_p_table;

  CREATE(player_table[pos].name, char, strlen(st->name) + 1);
  for (i = 0; (player_table[pos].name[i] = LOWER(st->name[i])); i++)
    ;
  player_table[pos].id = st->char_specials_saved.idnum;
  top_idnum = MAX(top_idnum, st->char_specials_saved.idnum);
}


/* generate index table for the player file */
void build_player_index(void)
{
  static const char *plr_dirs[] = {"A-E", "F-J", "K-O", "P-T", "U-Z", "ZZZ", NULL};
  int recs, i;

  top_of_p_table = -1;
  top_idnum = 0;

  recs = count_player_files();
  if (recs <= 0) {
    player_table = NULL;
    return;
  }

  log("   %d players in database.", recs);
  CREATE(player_table, struct player_index_element, recs);

  for (i = 0; plr_dirs[i]; i++) {
    DIR *dir;
    struct dirent *ent;
    char path[PATH_MAX];

    snprintf(path, sizeof(path), "%s%s", PLAYER_DIR, plr_dirs[i]);
    if (!(dir = opendir(path)))
      continue;

    while ((ent = readdir(dir)) != NULL) {
      size_t len = strlen(ent->d_name);
      struct char_file_u dummy;
      char filename[PATH_MAX];

      if (len <= 5 || strcmp(ent->d_name + len - 5, ".toml"))
	continue;

      snprintf(filename, sizeof(filename), "%s%s"SLASH"%s", PLAYER_DIR, plr_dirs[i], ent->d_name);
      if (!read_player_toml_file(filename, &dummy))
	continue;

      add_player_index_entry(&dummy);
    }
    closedir(dir);
  }
}

/*
 * Thanks to Andrey (andrey@alex-ua.com) for this bit of code, although I
 * did add the 'goto' and changed some "while()" into "do { } while()".
 *	-gg 6/24/98 (technically 6/25/98, but I care not.)
 */
int count_alias_records(FILE *fl)
{
  char key[READ_SIZE], next_key[READ_SIZE];
  char line[READ_SIZE], *scan;
  int total_keywords = 0;

  /* get the first keyword line */
  get_one_line(fl, key);

  while (*key != '$') {
    /* skip the text */
    do {
      get_one_line(fl, line);
      if (feof(fl))
	goto ackeof;
    } while (*line != '#');

    /* now count keywords */
    scan = key;
    do {
      scan = one_word(scan, next_key);
      if (*next_key)
        ++total_keywords;
    } while (*next_key);

    /* get next keyword line (or $) */
    get_one_line(fl, key);

    if (feof(fl))
      goto ackeof;
  }

  return (total_keywords);

  /* No, they are not evil. -gg 6/24/98 */
ackeof:	
  log("SYSERR: Unexpected end of help file.");
  exit(1);	/* Some day we hope to handle these things better... */
}

/* function to count how many hash-mark delimited records exist in a file */
int count_hash_records(FILE *fl)
{
  char buf[128];
  int count = 0;

  while (fgets(buf, 128, fl))
    if (*buf == '#')
      count++;

  return (count);
}

int count_toml_records(FILE *fl, int mode, const char *filename)
{
  char errbuf[256];
  toml_table_t *tab;
  toml_array_t *arr;
  const char *key;
  int count;

  tab = toml_parse_file(fl, errbuf, sizeof(errbuf));
  if (!tab) {
    log("SYSERR: TOML parse error in %s: %s", filename, errbuf);
    exit(1);
  }

  switch (mode) {
  case DB_BOOT_WLD:
    key = "rooms";
    break;
  case DB_BOOT_MOB:
    key = "mobs";
    break;
  case DB_BOOT_OBJ:
    key = "objects";
    break;
  case DB_BOOT_ZON:
    key = "zones";
    break;
  case DB_BOOT_SHP:
    key = "shops";
    break;
  default:
    toml_free(tab);
    return (0);
  }

  arr = toml_array_in(tab, key);
  if (!arr) {
    log("SYSERR: TOML file %s missing '%s' array.", filename, key);
    toml_free(tab);
    exit(1);
  }

  count = toml_array_nelem(arr);
  toml_free(tab);
  return (count);
}



void index_boot(int mode)
{
  const char *index_filename, *prefix = NULL;	/* NULL or egcs 1.1 complains */
  FILE *db_index, *db_file;
  int rec_count = 0, size[2];
  char buf2[PATH_MAX], buf1[MAX_STRING_LENGTH];

  switch (mode) {
  case DB_BOOT_WLD:
    prefix = WLD_PREFIX;
    break;
  case DB_BOOT_MOB:
    prefix = MOB_PREFIX;
    break;
  case DB_BOOT_OBJ:
    prefix = OBJ_PREFIX;
    break;
  case DB_BOOT_ZON:
    prefix = ZON_PREFIX;
    break;
  case DB_BOOT_SHP:
    prefix = SHP_PREFIX;
    break;
  case DB_BOOT_HLP:
    prefix = HLP_PREFIX;
    break;
  default:
    log("SYSERR: Unknown subcommand %d to index_boot!", mode);
    exit(1);
  }

  if (mini_mud)
    index_filename = MINDEX_FILE;
  else
    index_filename = INDEX_FILE;

  if (strlen(prefix) + strlen(index_filename) >= sizeof(buf2)) {
    log("SYSERR: Index filename too long.");
    exit(1);
  }
  strcpy(buf2, prefix);
  strcat(buf2, index_filename);
  if (!(db_index = fopen(buf2, "r"))) {
    log("SYSERR: opening index file '%s': %s", buf2, strerror(errno));
    exit(1);
  }

  /* first, count the number of records in the file so we can malloc */
  if (fscanf(db_index, "%s\n", buf1) != 1) {
    log("SYSERR: format error in index file '%s'.", buf2);
    exit(1);
  }
  while (*buf1 != '$') {
    if (strlen(prefix) + strlen(buf1) >= sizeof(buf2)) {
      log("SYSERR: Index filename too long.");
      exit(1);
    }
    strcpy(buf2, prefix);
    strcat(buf2, buf1);
    if (!(db_file = fopen(buf2, "r"))) {
      log("SYSERR: File '%s' listed in '%s/%s': %s", buf2, prefix,
	  index_filename, strerror(errno));
      if (fscanf(db_index, "%s\n", buf1) != 1) {
	log("SYSERR: format error in index file '%s'.", index_filename);
	exit(1);
      }
      continue;
    } else {
      if (mode == DB_BOOT_HLP)
	rec_count += count_alias_records(db_file);
      else
	rec_count += count_toml_records(db_file, mode, buf2);
    }

    fclose(db_file);
    if (fscanf(db_index, "%s\n", buf1) != 1) {
      log("SYSERR: format error in index file '%s'.", index_filename);
      exit(1);
    }
  }

  /* Exit if 0 records, unless this is shops */
  if (!rec_count) {
    if (mode == DB_BOOT_SHP)
      return;
    log("SYSERR: boot error - 0 records counted in %s/%s.", prefix,
	index_filename);
    exit(1);
  }

  /*
   * NOTE: "bytes" does _not_ include strings or other later malloc'd things.
   */
  switch (mode) {
  case DB_BOOT_WLD:
    CREATE(world, struct room_data, rec_count);
    size[0] = sizeof(struct room_data) * rec_count;
    log("   %d rooms, %d bytes.", rec_count, size[0]);
    break;
  case DB_BOOT_MOB:
    CREATE(mob_proto, struct char_data, rec_count);
    CREATE(mob_index, struct index_data, rec_count);
    size[0] = sizeof(struct index_data) * rec_count;
    size[1] = sizeof(struct char_data) * rec_count;
    log("   %d mobs, %d bytes in index, %d bytes in prototypes.", rec_count, size[0], size[1]);
    break;
  case DB_BOOT_OBJ:
    CREATE(obj_proto, struct obj_data, rec_count);
    CREATE(obj_index, struct index_data, rec_count);
    size[0] = sizeof(struct index_data) * rec_count;
    size[1] = sizeof(struct obj_data) * rec_count;
    log("   %d objs, %d bytes in index, %d bytes in prototypes.", rec_count, size[0], size[1]);
    break;
  case DB_BOOT_ZON:
    CREATE(zone_table, struct zone_data, rec_count);
    size[0] = sizeof(struct zone_data) * rec_count;
    log("   %d zones, %d bytes.", rec_count, size[0]);
    break;
  case DB_BOOT_HLP:
    CREATE(help_table, struct help_index_element, rec_count);
    size[0] = sizeof(struct help_index_element) * rec_count;
    log("   %d entries, %d bytes.", rec_count, size[0]);
    break;
  }

  rewind(db_index);
  if (fscanf(db_index, "%s\n", buf1) != 1) {
    log("SYSERR: format error in index file '%s'.", index_filename);
    exit(1);
  }
  while (*buf1 != '$') {
    if (strlen(prefix) + strlen(buf1) >= sizeof(buf2)) {
      log("SYSERR: Index filename too long.");
      exit(1);
    }
    strcpy(buf2, prefix);
    strcat(buf2, buf1);
    if (!(db_file = fopen(buf2, "r"))) {
      log("SYSERR: %s: %s", buf2, strerror(errno));
      exit(1);
    }
    switch (mode) {
    case DB_BOOT_WLD:
    case DB_BOOT_OBJ:
    case DB_BOOT_MOB:
      discrete_load(db_file, mode, buf2);
      break;
    case DB_BOOT_ZON:
      load_zones(db_file, buf2);
      break;
    case DB_BOOT_HLP:
      /*
       * If you think about it, we have a race here.  Although, this is the
       * "point-the-gun-at-your-own-foot" type of race.
       */
      load_help(db_file);
      break;
    case DB_BOOT_SHP:
      boot_the_shops(db_file, buf2, rec_count);
      break;
    }

    fclose(db_file);
    if (fscanf(db_index, "%s\n", buf1) != 1) {
      log("SYSERR: format error in index file '%s'.", index_filename);
      exit(1);
    }
  }
  fclose(db_index);

  /* sort the help index */
  if (mode == DB_BOOT_HLP) {
    qsort(help_table, top_of_helpt, sizeof(struct help_index_element), hsort);
    top_of_helpt--;
  }
}


static int toml_int_required(const toml_table_t *tab, const char *key,
			     const char *context, const char *filename)
{
  toml_datum_t val = toml_int_in(tab, key);

  if (!val.ok) {
    log("SYSERR: TOML file %s missing integer '%s' in %s.", filename, key, context);
    exit(1);
  }

  return ((int)val.u.i);
}

static char *toml_string_required(const toml_table_t *tab, const char *key,
				  const char *context, const char *filename)
{
  toml_datum_t val = toml_string_in(tab, key);

  if (!val.ok) {
    log("SYSERR: TOML file %s missing string '%s' in %s.", filename, key, context);
    exit(1);
  }

  if (val.u.s && !*val.u.s) {
    free(val.u.s);
    return (NULL);
  }

  return (val.u.s);
}

static char *toml_string_optional(const toml_table_t *tab, const char *key)
{
  toml_datum_t val = toml_string_in(tab, key);

  if (!val.ok)
    return (NULL);

  if (val.u.s && !*val.u.s) {
    free(val.u.s);
    return (NULL);
  }

  return (val.u.s);
}

static toml_array_t *toml_array_required(const toml_table_t *tab, const char *key,
					 const char *context, const char *filename)
{
  toml_array_t *arr = toml_array_in(tab, key);

  if (!arr) {
    log("SYSERR: TOML file %s missing array '%s' in %s.", filename, key, context);
    exit(1);
  }

  return (arr);
}

static void toml_copy_string_required(char *dest, size_t destsize,
				      const toml_table_t *tab, const char *key,
				      const char *context, const char *filename)
{
  char *val = toml_string_required(tab, key, context, filename);

  if (val) {
    if (strlen(val) >= destsize) {
      log("SYSERR: TOML string '%s' too long in %s.", key, filename);
      val[destsize - 1] = '\0';
    }
    strlcpy(dest, val, destsize);
    free(val);
  } else
    *dest = '\0';
}

static void toml_copy_string_optional(char *dest, size_t destsize,
				      const toml_table_t *tab, const char *key)
{
  char *val = toml_string_optional(tab, key);

  if (val) {
    if (strlen(val) >= destsize)
      val[destsize - 1] = '\0';
    strlcpy(dest, val, destsize);
    free(val);
  } else
    *dest = '\0';
}

static void toml_int_array_required(const toml_table_t *tab, const char *key,
				    int *out, int count,
				    const char *context, const char *filename)
{
  toml_array_t *arr = toml_array_required(tab, key, context, filename);
  int i, n = toml_array_nelem(arr);

  for (i = 0; i < count; i++)
    out[i] = 0;

  if (n < count)
    count = n;

  for (i = 0; i < count; i++) {
    toml_datum_t val = toml_int_at(arr, i);
    if (!val.ok) {
      log("SYSERR: TOML file %s has invalid integer in %s[%d].", filename, key, i);
      exit(1);
    }
    out[i] = (int)val.u.i;
  }
}

static void toml_bool_array_required(const toml_table_t *tab, const char *key,
				     bool *out, int count,
				     const char *context, const char *filename)
{
  toml_array_t *arr = toml_array_required(tab, key, context, filename);
  int i, n = toml_array_nelem(arr);

  for (i = 0; i < count; i++)
    out[i] = 0;

  if (n < count)
    count = n;

  for (i = 0; i < count; i++) {
    toml_datum_t val = toml_bool_at(arr, i);
    if (!val.ok) {
      log("SYSERR: TOML file %s has invalid bool in %s[%d].", filename, key, i);
      exit(1);
    }
    out[i] = val.u.b ? 1 : 0;
  }
}

static int toml_int_optional(const toml_table_t *tab, const char *key, int def)
{
  toml_datum_t val = toml_int_in(tab, key);

  if (!val.ok)
    return (def);

  return ((int)val.u.i);
}

void discrete_load(FILE *fl, int mode, char *filename)
{
  char errbuf[256];
  toml_table_t *tab;
  toml_array_t *arr;
  const char *key;
  int i, count;

  tab = toml_parse_file(fl, errbuf, sizeof(errbuf));
  if (!tab) {
    log("SYSERR: TOML parse error in %s: %s", filename, errbuf);
    exit(1);
  }

  switch (mode) {
  case DB_BOOT_WLD:
    key = "rooms";
    break;
  case DB_BOOT_MOB:
    key = "mobs";
    break;
  case DB_BOOT_OBJ:
    key = "objects";
    break;
  default:
    toml_free(tab);
    return;
  }

  arr = toml_array_required(tab, key, "top-level", filename);
  count = toml_array_nelem(arr);
  for (i = 0; i < count; i++) {
    toml_table_t *entry = toml_table_at(arr, i);
    if (!entry) {
      log("SYSERR: TOML file %s has non-table entry in %s[%d].", filename, key, i);
      toml_free(tab);
      exit(1);
    }
    switch (mode) {
    case DB_BOOT_WLD:
      parse_room_toml(entry, filename);
      break;
    case DB_BOOT_MOB:
      parse_mobile_toml(entry, filename);
      break;
    case DB_BOOT_OBJ:
      parse_object_toml(entry, filename);
      break;
    }
  }

  toml_free(tab);
}

bitvector_t asciiflag_conv(char *flag)
{
  bitvector_t flags = 0;
  int is_num = TRUE;
  char *p;

  for (p = flag; *p; p++) {
    if (islower(*p))
      flags |= 1 << (*p - 'a');
    else if (isupper(*p))
      flags |= 1 << (26 + (*p - 'A'));

    if (!isdigit(*p))
      is_num = FALSE;
  }

  if (is_num)
    flags = atol(flag);

  return (flags);
}

void parse_room_toml(toml_table_t *room, const char *filename)
{
  static int room_nr = 0, zone = 0;
  int vnum, sector, i, door;
  char buf[128], flags_buf[128];
  char *flags;
  toml_array_t *exits, *extra;
  struct extra_descr_data *new_descr;

  vnum = toml_int_required(room, "vnum", "room entry", filename);
  snprintf(buf, sizeof(buf), "room #%d", vnum);

  if (vnum < zone_table[zone].bot) {
    log("SYSERR: Room #%d is below zone %d.", vnum, zone);
    exit(1);
  }
  while (vnum > zone_table[zone].top)
    if (++zone > top_of_zone_table) {
      log("SYSERR: Room %d is outside of any zone.", vnum);
      exit(1);
    }
  world[room_nr].zone = zone;
  world[room_nr].number = vnum;
  world[room_nr].name = toml_string_required(room, "name", buf, filename);
  world[room_nr].description = toml_string_required(room, "description", buf, filename);

  flags = toml_string_required(room, "flags", buf, filename);
  world[room_nr].room_flags = asciiflag_conv(flags);
  snprintf(flags_buf, sizeof(flags_buf), "object #%d", vnum);
  check_bitvector_names(world[room_nr].room_flags, room_bits_count, flags_buf, "room");
  free(flags);

  sector = toml_int_required(room, "sector", buf, filename);
  world[room_nr].sector_type = sector;

  world[room_nr].func = NULL;
  world[room_nr].contents = NULL;
  world[room_nr].people = NULL;
  world[room_nr].light = 0;

  for (i = 0; i < NUM_OF_DIRS; i++)
    world[room_nr].dir_option[i] = NULL;

  world[room_nr].ex_description = NULL;

  exits = toml_array_in(room, "exits");
  if (exits) {
    int count = toml_array_nelem(exits);

    for (i = 0; i < count; i++) {
      toml_table_t *exit_tab = toml_table_at(exits, i);
      int dir, key, to_room;

      if (!exit_tab) {
	log("SYSERR: TOML file %s has non-table exit for %s.", filename, buf);
	exit(1);
      }

      dir = toml_int_required(exit_tab, "dir", buf, filename);
      if (dir < 0 || dir >= NUM_OF_DIRS) {
	log("SYSERR: TOML file %s has invalid dir %d for %s.", filename, dir, buf);
	exit(1);
      }

      CREATE(world[room_nr].dir_option[dir], struct room_direction_data, 1);
      world[room_nr].dir_option[dir]->general_description =
	toml_string_optional(exit_tab, "description");
      world[room_nr].dir_option[dir]->keyword =
	toml_string_optional(exit_tab, "keyword");

      door = toml_int_required(exit_tab, "door", buf, filename);
      if (door == 1)
	world[room_nr].dir_option[dir]->exit_info = EX_ISDOOR;
      else if (door == 2)
	world[room_nr].dir_option[dir]->exit_info = EX_ISDOOR | EX_PICKPROOF;
      else
	world[room_nr].dir_option[dir]->exit_info = 0;

      key = toml_int_required(exit_tab, "key", buf, filename);
      to_room = toml_int_required(exit_tab, "to_room", buf, filename);
      world[room_nr].dir_option[dir]->key = key;
      world[room_nr].dir_option[dir]->to_room = to_room;
    }
  }

  extra = toml_array_in(room, "extra");
  if (extra) {
    int count = toml_array_nelem(extra);

    for (i = 0; i < count; i++) {
      toml_table_t *extra_tab = toml_table_at(extra, i);

      if (!extra_tab) {
	log("SYSERR: TOML file %s has non-table extra description for %s.", filename, buf);
	exit(1);
      }

      CREATE(new_descr, struct extra_descr_data, 1);
      new_descr->keyword = toml_string_required(extra_tab, "keyword", buf, filename);
      new_descr->description = toml_string_required(extra_tab, "description", buf, filename);
      new_descr->next = world[room_nr].ex_description;
      world[room_nr].ex_description = new_descr;
    }
  }

  top_of_world = room_nr++;
}


/* load the rooms */
void parse_room(FILE *fl, int virtual_nr)
{
  static int room_nr = 0, zone = 0;
  int t[10], i;
  char line[READ_SIZE], flags[128], buf2[MAX_STRING_LENGTH], buf[128];
  struct extra_descr_data *new_descr;

  /* This really had better fit or there are other problems. */
  snprintf(buf2, sizeof(buf2), "room #%d", virtual_nr);

  if (virtual_nr < zone_table[zone].bot) {
    log("SYSERR: Room #%d is below zone %d.", virtual_nr, zone);
    exit(1);
  }
  while (virtual_nr > zone_table[zone].top)
    if (++zone > top_of_zone_table) {
      log("SYSERR: Room %d is outside of any zone.", virtual_nr);
      exit(1);
    }
  world[room_nr].zone = zone;
  world[room_nr].number = virtual_nr;
  world[room_nr].name = fread_string(fl, buf2);
  world[room_nr].description = fread_string(fl, buf2);

  if (!get_line(fl, line)) {
    log("SYSERR: Expecting roomflags/sector type of room #%d but file ended!",
	virtual_nr);
    exit(1);
  }

  if (sscanf(line, " %d %s %d ", t, flags, t + 2) != 3) {
    log("SYSERR: Format error in roomflags/sector type of room #%d",
	virtual_nr);
    exit(1);
  }
  /* t[0] is the zone number; ignored with the zone-file system */

  world[room_nr].room_flags = asciiflag_conv(flags);
  sprintf(flags, "object #%d", virtual_nr);	/* sprintf: OK (until 399-bit integers) */
  check_bitvector_names(world[room_nr].room_flags, room_bits_count, flags, "room");

  world[room_nr].sector_type = t[2];

  world[room_nr].func = NULL;
  world[room_nr].contents = NULL;
  world[room_nr].people = NULL;
  world[room_nr].light = 0;	/* Zero light sources */

  for (i = 0; i < NUM_OF_DIRS; i++)
    world[room_nr].dir_option[i] = NULL;

  world[room_nr].ex_description = NULL;

  snprintf(buf, sizeof(buf), "SYSERR: Format error in room #%d (expecting D/E/S)", virtual_nr);

  for (;;) {
    if (!get_line(fl, line)) {
      log("%s", buf);
      exit(1);
    }
    switch (*line) {
    case 'D':
      setup_dir(fl, room_nr, atoi(line + 1));
      break;
    case 'E':
      CREATE(new_descr, struct extra_descr_data, 1);
      new_descr->keyword = fread_string(fl, buf2);
      new_descr->description = fread_string(fl, buf2);
      new_descr->next = world[room_nr].ex_description;
      world[room_nr].ex_description = new_descr;
      break;
    case 'S':			/* end of room */
      top_of_world = room_nr++;
      return;
    default:
      log("%s", buf);
      exit(1);
    }
  }
}



/* read direction data */
void setup_dir(FILE *fl, int room, int dir)
{
  int t[5];
  char line[READ_SIZE], buf2[128];

  snprintf(buf2, sizeof(buf2), "room #%d, direction D%d", GET_ROOM_VNUM(room), dir);

  CREATE(world[room].dir_option[dir], struct room_direction_data, 1);
  world[room].dir_option[dir]->general_description = fread_string(fl, buf2);
  world[room].dir_option[dir]->keyword = fread_string(fl, buf2);

  if (!get_line(fl, line)) {
    log("SYSERR: Format error, %s", buf2);
    exit(1);
  }
  if (sscanf(line, " %d %d %d ", t, t + 1, t + 2) != 3) {
    log("SYSERR: Format error, %s", buf2);
    exit(1);
  }
  if (t[0] == 1)
    world[room].dir_option[dir]->exit_info = EX_ISDOOR;
  else if (t[0] == 2)
    world[room].dir_option[dir]->exit_info = EX_ISDOOR | EX_PICKPROOF;
  else
    world[room].dir_option[dir]->exit_info = 0;

  world[room].dir_option[dir]->key = t[1];
  world[room].dir_option[dir]->to_room = t[2];
}


/* make sure the start rooms exist & resolve their vnums to rnums */
void check_start_rooms(void)
{
  if ((r_mortal_start_room = real_room(mortal_start_room)) == NOWHERE) {
    log("SYSERR:  Mortal start room does not exist.  Change in config.c.");
    exit(1);
  }
  if ((r_immort_start_room = real_room(immort_start_room)) == NOWHERE) {
    if (!mini_mud)
      log("SYSERR:  Warning: Immort start room does not exist.  Change in config.c.");
    r_immort_start_room = r_mortal_start_room;
  }
  if ((r_frozen_start_room = real_room(frozen_start_room)) == NOWHERE) {
    if (!mini_mud)
      log("SYSERR:  Warning: Frozen start room does not exist.  Change in config.c.");
    r_frozen_start_room = r_mortal_start_room;
  }
}


/* resolve all vnums into rnums in the world */
void renum_world(void)
{
  int room, door;

  for (room = 0; room <= top_of_world; room++)
    for (door = 0; door < NUM_OF_DIRS; door++)
      if (world[room].dir_option[door])
	if (world[room].dir_option[door]->to_room != NOWHERE)
	  world[room].dir_option[door]->to_room =
	    real_room(world[room].dir_option[door]->to_room);
}


#define ZCMD zone_table[zone].cmd[cmd_no]

/*
 * "resulve vnums into rnums in the zone reset tables"
 *
 * Or in English: Once all of the zone reset tables have been loaded, we
 * resolve the virtual numbers into real numbers all at once so we don't have
 * to do it repeatedly while the game is running.  This does make adding any
 * room, mobile, or object a little more difficult while the game is running.
 *
 * NOTE 1: Assumes NOWHERE == NOBODY == NOTHING.
 * NOTE 2: Assumes sizeof(room_rnum) >= (sizeof(mob_rnum) and sizeof(obj_rnum))
 */
void renum_zone_table(void)
{
  int cmd_no;
  room_rnum a, b, c, olda, oldb, oldc;
  zone_rnum zone;
  char buf[128];

  for (zone = 0; zone <= top_of_zone_table; zone++)
    for (cmd_no = 0; ZCMD.command != 'S'; cmd_no++) {
      a = b = c = 0;
      olda = ZCMD.arg1;
      oldb = ZCMD.arg2;
      oldc = ZCMD.arg3;
      switch (ZCMD.command) {
      case 'M':
	a = ZCMD.arg1 = real_mobile(ZCMD.arg1);
	c = ZCMD.arg3 = real_room(ZCMD.arg3);
	break;
      case 'O':
	a = ZCMD.arg1 = real_object(ZCMD.arg1);
	if (ZCMD.arg3 != NOWHERE)
	  c = ZCMD.arg3 = real_room(ZCMD.arg3);
	break;
      case 'G':
	a = ZCMD.arg1 = real_object(ZCMD.arg1);
	break;
      case 'E':
	a = ZCMD.arg1 = real_object(ZCMD.arg1);
	break;
      case 'P':
	a = ZCMD.arg1 = real_object(ZCMD.arg1);
	c = ZCMD.arg3 = real_object(ZCMD.arg3);
	break;
      case 'D':
	a = ZCMD.arg1 = real_room(ZCMD.arg1);
	break;
      case 'R': /* rem obj from room */
        a = ZCMD.arg1 = real_room(ZCMD.arg1);
	b = ZCMD.arg2 = real_object(ZCMD.arg2);
        break;
      }
      if (a == NOWHERE || b == NOWHERE || c == NOWHERE) {
	if (!mini_mud) {
	  snprintf(buf, sizeof(buf), "Invalid vnum %d, cmd disabled",
			 a == NOWHERE ? olda : b == NOWHERE ? oldb : oldc);
	  log_zone_error(zone, cmd_no, buf);
	}
	ZCMD.command = '*';
      }
    }
}



void parse_simple_mob(FILE *mob_f, int i, int nr)
{
  int j, t[10];
  char line[READ_SIZE];

  mob_proto[i].real_abils.str = 11;
  mob_proto[i].real_abils.intel = 11;
  mob_proto[i].real_abils.wis = 11;
  mob_proto[i].real_abils.dex = 11;
  mob_proto[i].real_abils.con = 11;
  mob_proto[i].real_abils.cha = 11;

  if (!get_line(mob_f, line)) {
    log("SYSERR: Format error in mob #%d, file ended after S flag!", nr);
    exit(1);
  }

  if (sscanf(line, " %d %d %d %dd%d+%d %dd%d+%d ",
	  t, t + 1, t + 2, t + 3, t + 4, t + 5, t + 6, t + 7, t + 8) != 9) {
    log("SYSERR: Format error in mob #%d, first line after S flag\n"
	"...expecting line of form '# # # #d#+# #d#+#'", nr);
    exit(1);
  }

  GET_LEVEL(mob_proto + i) = t[0];
  GET_HITROLL(mob_proto + i) = 20 - t[1];
  GET_AC(mob_proto + i) = 10 * t[2];

  /* max hit = 0 is a flag that H, M, V is xdy+z */
  GET_MAX_HIT(mob_proto + i) = 0;
  GET_HIT(mob_proto + i) = t[3];
  GET_MANA(mob_proto + i) = t[4];
  GET_MOVE(mob_proto + i) = t[5];

  GET_MAX_MANA(mob_proto + i) = 10;
  GET_MAX_MOVE(mob_proto + i) = 50;

  mob_proto[i].mob_specials.damnodice = t[6];
  mob_proto[i].mob_specials.damsizedice = t[7];
  GET_DAMROLL(mob_proto + i) = t[8];

  if (!get_line(mob_f, line)) {
      log("SYSERR: Format error in mob #%d, second line after S flag\n"
	  "...expecting line of form '# #', but file ended!", nr);
      exit(1);
    }

  if (sscanf(line, " %d %d ", t, t + 1) != 2) {
    log("SYSERR: Format error in mob #%d, second line after S flag\n"
	"...expecting line of form '# #'", nr);
    exit(1);
  }

  GET_GOLD(mob_proto + i) = t[0];
  GET_EXP(mob_proto + i) = t[1];

  if (!get_line(mob_f, line)) {
    log("SYSERR: Format error in last line of mob #%d\n"
	"...expecting line of form '# # #', but file ended!", nr);
    exit(1);
  }

  if (sscanf(line, " %d %d %d ", t, t + 1, t + 2) != 3) {
    log("SYSERR: Format error in last line of mob #%d\n"
	"...expecting line of form '# # #'", nr);
    exit(1);
  }

  GET_POS(mob_proto + i) = t[0];
  GET_DEFAULT_POS(mob_proto + i) = t[1];
  GET_SEX(mob_proto + i) = t[2];

  GET_CLASS(mob_proto + i) = 0;
  GET_WEIGHT(mob_proto + i) = 200;
  GET_HEIGHT(mob_proto + i) = 198;

  /*
   * these are now save applies; base save numbers for MOBs are now from
   * the warrior save table.
   */
  for (j = 0; j < 5; j++)
    GET_SAVE(mob_proto + i, j) = 0;
}


/*
 * interpret_espec is the function that takes espec keywords and values
 * and assigns the correct value to the mob as appropriate.  Adding new
 * e-specs is absurdly easy -- just add a new CASE statement to this
 * function!  No other changes need to be made anywhere in the code.
 *
 * CASE		: Requires a parameter through 'value'.
 * BOOL_CASE	: Being specified at all is its value.
 */

#define CASE(test)	\
	if (value && !matched && !str_cmp(keyword, test) && (matched = TRUE))

#define BOOL_CASE(test)	\
	if (!value && !matched && !str_cmp(keyword, test) && (matched = TRUE))

#define RANGE(low, high)	\
	(num_arg = MAX((low), MIN((high), (num_arg))))

void interpret_espec(const char *keyword, const char *value, int i, int nr)
{
  int num_arg = 0, matched = FALSE;

  /*
   * If there isn't a colon, there is no value.  While Boolean options are
   * possible, we don't actually have any.  Feel free to make some.
  */
  if (value)
    num_arg = atoi(value);

  CASE("BareHandAttack") {
    RANGE(0, 99);
    mob_proto[i].mob_specials.attack_type = num_arg;
  }

  CASE("Str") {
    RANGE(3, 25);
    mob_proto[i].real_abils.str = num_arg;
  }

  CASE("StrAdd") {
    RANGE(0, 100);
    mob_proto[i].real_abils.str_add = num_arg;    
  }

  CASE("Int") {
    RANGE(3, 25);
    mob_proto[i].real_abils.intel = num_arg;
  }

  CASE("Wis") {
    RANGE(3, 25);
    mob_proto[i].real_abils.wis = num_arg;
  }

  CASE("Dex") {
    RANGE(3, 25);
    mob_proto[i].real_abils.dex = num_arg;
  }

  CASE("Con") {
    RANGE(3, 25);
    mob_proto[i].real_abils.con = num_arg;
  }

  CASE("Cha") {
    RANGE(3, 25);
    mob_proto[i].real_abils.cha = num_arg;
  }

  if (!matched) {
    log("SYSERR: Warning: unrecognized espec keyword %s in mob #%d",
	    keyword, nr);
  }    
}

#undef CASE
#undef BOOL_CASE
#undef RANGE

void parse_espec(char *buf, int i, int nr)
{
  char *ptr;

  if ((ptr = strchr(buf, ':')) != NULL) {
    *(ptr++) = '\0';
    while (isspace(*ptr))
      ptr++;
  }
  interpret_espec(buf, ptr, i, nr);
}


void parse_enhanced_mob(FILE *mob_f, int i, int nr)
{
  char line[READ_SIZE];

  parse_simple_mob(mob_f, i, nr);

  while (get_line(mob_f, line)) {
    if (!strcmp(line, "E"))	/* end of the enhanced section */
      return;
    else if (*line == '#') {	/* we've hit the next mob, maybe? */
      log("SYSERR: Unterminated E section in mob #%d", nr);
      exit(1);
    } else
      parse_espec(line, i, nr);
  }

  log("SYSERR: Unexpected end of file reached after mob #%d", nr);
  exit(1);
}

void parse_mobile_toml(toml_table_t *mob, const char *filename)
{
  static int i = 0;
  int j, vnum, letter;
  char *tmpptr, *f1, *f2, *typestr;
  char buf2[128];
  toml_table_t *espec;

  vnum = toml_int_required(mob, "vnum", "mob entry", filename);
  mob_index[i].vnum = vnum;
  mob_index[i].number = 0;
  mob_index[i].func = NULL;

  clear_char(mob_proto + i);

  mob_proto[i].player_specials = &dummy_mob;
  snprintf(buf2, sizeof(buf2), "mob vnum %d", vnum);

  mob_proto[i].player.name = toml_string_required(mob, "name", buf2, filename);
  tmpptr = mob_proto[i].player.short_descr = toml_string_required(mob, "short_descr", buf2, filename);
  if (tmpptr && *tmpptr)
    if (!str_cmp(fname(tmpptr), "a") || !str_cmp(fname(tmpptr), "an") ||
	!str_cmp(fname(tmpptr), "the"))
      *tmpptr = LOWER(*tmpptr);
  mob_proto[i].player.long_descr = toml_string_required(mob, "long_descr", buf2, filename);
  mob_proto[i].player.description = toml_string_required(mob, "description", buf2, filename);
  GET_TITLE(mob_proto + i) = NULL;

  f1 = toml_string_required(mob, "mob_flags", buf2, filename);
  f2 = toml_string_required(mob, "aff_flags", buf2, filename);

  MOB_FLAGS(mob_proto + i) = asciiflag_conv(f1);
  SET_BIT(MOB_FLAGS(mob_proto + i), MOB_ISNPC);
  if (MOB_FLAGGED(mob_proto + i, MOB_NOTDEADYET)) {
    log("SYSERR: Mob #%d has reserved bit MOB_NOTDEADYET set.", vnum);
    REMOVE_BIT(MOB_FLAGS(mob_proto + i), MOB_NOTDEADYET);
  }
  check_bitvector_names(MOB_FLAGS(mob_proto + i), action_bits_count, buf2, "mobile");

  AFF_FLAGS(mob_proto + i) = asciiflag_conv(f2);
  check_bitvector_names(AFF_FLAGS(mob_proto + i), affected_bits_count, buf2, "mobile affect");

  free(f1);
  free(f2);

  GET_ALIGNMENT(mob_proto + i) = toml_int_required(mob, "alignment", buf2, filename);

  if (MOB_FLAGGED(mob_proto + i, MOB_AGGRESSIVE) && MOB_FLAGGED(mob_proto + i, MOB_AGGR_GOOD | MOB_AGGR_EVIL | MOB_AGGR_NEUTRAL))
    log("SYSERR: Mob #%d both Aggressive and Aggressive_to_Alignment.", vnum);

  typestr = toml_string_required(mob, "type", buf2, filename);
  letter = UPPER(*typestr);
  free(typestr);

  mob_proto[i].real_abils.str = 11;
  mob_proto[i].real_abils.intel = 11;
  mob_proto[i].real_abils.wis = 11;
  mob_proto[i].real_abils.dex = 11;
  mob_proto[i].real_abils.con = 11;
  mob_proto[i].real_abils.cha = 11;

  GET_LEVEL(mob_proto + i) = toml_int_required(mob, "level", buf2, filename);
  GET_HITROLL(mob_proto + i) = toml_int_required(mob, "hitroll", buf2, filename);
  GET_AC(mob_proto + i) = toml_int_required(mob, "ac", buf2, filename);

  GET_MAX_HIT(mob_proto + i) = 0;
  GET_HIT(mob_proto + i) = toml_int_required(mob, "hit", buf2, filename);
  GET_MANA(mob_proto + i) = toml_int_required(mob, "mana", buf2, filename);
  GET_MOVE(mob_proto + i) = toml_int_required(mob, "move", buf2, filename);

  GET_MAX_MANA(mob_proto + i) = 10;
  GET_MAX_MOVE(mob_proto + i) = 50;

  mob_proto[i].mob_specials.damnodice = toml_int_required(mob, "damnodice", buf2, filename);
  mob_proto[i].mob_specials.damsizedice = toml_int_required(mob, "damsizedice", buf2, filename);
  GET_DAMROLL(mob_proto + i) = toml_int_required(mob, "damroll", buf2, filename);

  GET_GOLD(mob_proto + i) = toml_int_required(mob, "gold", buf2, filename);
  GET_EXP(mob_proto + i) = toml_int_required(mob, "exp", buf2, filename);
  GET_POS(mob_proto + i) = toml_int_required(mob, "position", buf2, filename);
  GET_DEFAULT_POS(mob_proto + i) = toml_int_required(mob, "default_pos", buf2, filename);
  GET_SEX(mob_proto + i) = toml_int_required(mob, "sex", buf2, filename);

  GET_CLASS(mob_proto + i) = 0;
  GET_WEIGHT(mob_proto + i) = 200;
  GET_HEIGHT(mob_proto + i) = 198;

  for (j = 0; j < 5; j++)
    GET_SAVE(mob_proto + i, j) = 0;

  if (letter == 'E') {
    int k, knum;

    espec = toml_table_in(mob, "espec");
    if (espec) {
      knum = toml_table_nkval(espec);
      for (k = 0; k < knum; k++) {
	const char *key = toml_key_in(espec, k);
	toml_datum_t sval = toml_string_in(espec, key);
	toml_datum_t ival;

	if (sval.ok) {
	  interpret_espec(key, sval.u.s, i, vnum);
	  free(sval.u.s);
	  continue;
	}

	ival = toml_int_in(espec, key);
	if (ival.ok) {
	  char numbuf[32];
	  snprintf(numbuf, sizeof(numbuf), "%lld", (long long)ival.u.i);
	  interpret_espec(key, numbuf, i, vnum);
	  continue;
	}

	log("SYSERR: Mob #%d has invalid espec value for %s.", vnum, key);
	exit(1);
      }
    }
  } else if (letter != 'S') {
    log("SYSERR: Unsupported mob type '%c' in mob #%d", letter, vnum);
    exit(1);
  }

  mob_proto[i].aff_abils = mob_proto[i].real_abils;

  for (j = 0; j < NUM_WEARS; j++)
    mob_proto[i].equipment[j] = NULL;

  mob_proto[i].nr = i;
  mob_proto[i].desc = NULL;

  top_of_mobt = i++;
}


void parse_mobile(FILE *mob_f, int nr)
{
  static int i = 0;
  int j, t[10];
  char line[READ_SIZE], *tmpptr, letter;
  char f1[128], f2[128], buf2[128];

  mob_index[i].vnum = nr;
  mob_index[i].number = 0;
  mob_index[i].func = NULL;

  clear_char(mob_proto + i);

  /*
   * Mobiles should NEVER use anything in the 'player_specials' structure.
   * The only reason we have every mob in the game share this copy of the
   * structure is to save newbie coders from themselves. -gg 2/25/98
   */
  mob_proto[i].player_specials = &dummy_mob;
  sprintf(buf2, "mob vnum %d", nr);	/* sprintf: OK (for 'buf2 >= 19') */

  /***** String data *****/
  mob_proto[i].player.name = fread_string(mob_f, buf2);
  tmpptr = mob_proto[i].player.short_descr = fread_string(mob_f, buf2);
  if (tmpptr && *tmpptr)
    if (!str_cmp(fname(tmpptr), "a") || !str_cmp(fname(tmpptr), "an") ||
	!str_cmp(fname(tmpptr), "the"))
      *tmpptr = LOWER(*tmpptr);
  mob_proto[i].player.long_descr = fread_string(mob_f, buf2);
  mob_proto[i].player.description = fread_string(mob_f, buf2);
  GET_TITLE(mob_proto + i) = NULL;

  /* *** Numeric data *** */
  if (!get_line(mob_f, line)) {
    log("SYSERR: Format error after string section of mob #%d\n"
	"...expecting line of form '# # # {S | E}', but file ended!", nr);
    exit(1);
  }

#ifdef CIRCLE_ACORN	/* Ugh. */
  if (sscanf(line, "%s %s %d %s", f1, f2, t + 2, &letter) != 4) {
#else
  if (sscanf(line, "%s %s %d %c", f1, f2, t + 2, &letter) != 4) {
#endif
    log("SYSERR: Format error after string section of mob #%d\n"
	"...expecting line of form '# # # {S | E}'", nr);
    exit(1);
  }

  MOB_FLAGS(mob_proto + i) = asciiflag_conv(f1);
  SET_BIT(MOB_FLAGS(mob_proto + i), MOB_ISNPC);
  if (MOB_FLAGGED(mob_proto + i, MOB_NOTDEADYET)) {
    /* Rather bad to load mobiles with this bit already set. */
    log("SYSERR: Mob #%d has reserved bit MOB_NOTDEADYET set.", nr);
    REMOVE_BIT(MOB_FLAGS(mob_proto + i), MOB_NOTDEADYET);
  }
  check_bitvector_names(MOB_FLAGS(mob_proto + i), action_bits_count, buf2, "mobile");

  AFF_FLAGS(mob_proto + i) = asciiflag_conv(f2);
  check_bitvector_names(AFF_FLAGS(mob_proto + i), affected_bits_count, buf2, "mobile affect");

  GET_ALIGNMENT(mob_proto + i) = t[2];

  /* AGGR_TO_ALIGN is ignored if the mob is AGGRESSIVE. */
  if (MOB_FLAGGED(mob_proto + i, MOB_AGGRESSIVE) && MOB_FLAGGED(mob_proto + i, MOB_AGGR_GOOD | MOB_AGGR_EVIL | MOB_AGGR_NEUTRAL))
    log("SYSERR: Mob #%d both Aggressive and Aggressive_to_Alignment.", nr);

  switch (UPPER(letter)) {
  case 'S':	/* Simple monsters */
    parse_simple_mob(mob_f, i, nr);
    break;
  case 'E':	/* Circle3 Enhanced monsters */
    parse_enhanced_mob(mob_f, i, nr);
    break;
  /* add new mob types here.. */
  default:
    log("SYSERR: Unsupported mob type '%c' in mob #%d", letter, nr);
    exit(1);
  }

  mob_proto[i].aff_abils = mob_proto[i].real_abils;

  for (j = 0; j < NUM_WEARS; j++)
    mob_proto[i].equipment[j] = NULL;

  mob_proto[i].nr = i;
  mob_proto[i].desc = NULL;

  top_of_mobt = i++;
}




/* read all objects from obj file; generate index and prototypes */
void parse_object_toml(toml_table_t *obj, const char *filename)
{
  static int i = 0;
  int j, vnum, count;
  char *tmpptr, *f1, *f2;
  char buf2[128];
  toml_array_t *values, *extra, *affects;
  struct extra_descr_data *new_descr;

  vnum = toml_int_required(obj, "vnum", "object entry", filename);

  obj_index[i].vnum = vnum;
  obj_index[i].number = 0;
  obj_index[i].func = NULL;

  clear_object(obj_proto + i);
  obj_proto[i].item_number = i;

  snprintf(buf2, sizeof(buf2), "object #%d", vnum);

  obj_proto[i].name = toml_string_required(obj, "name", buf2, filename);
  if (!obj_proto[i].name) {
    log("SYSERR: Null obj name or format error at or near %s", buf2);
    exit(1);
  }
  tmpptr = obj_proto[i].short_description = toml_string_required(obj, "short_descr", buf2, filename);
  if (tmpptr && *tmpptr)
    if (!str_cmp(fname(tmpptr), "a") || !str_cmp(fname(tmpptr), "an") ||
	!str_cmp(fname(tmpptr), "the"))
      *tmpptr = LOWER(*tmpptr);

  tmpptr = obj_proto[i].description = toml_string_required(obj, "description", buf2, filename);
  if (tmpptr && *tmpptr)
    CAP(tmpptr);
  obj_proto[i].action_description = toml_string_optional(obj, "action_description");

  GET_OBJ_TYPE(obj_proto + i) = toml_int_required(obj, "type", buf2, filename);
  f1 = toml_string_required(obj, "extra_flags", buf2, filename);
  f2 = toml_string_required(obj, "wear_flags", buf2, filename);
  GET_OBJ_EXTRA(obj_proto + i) = asciiflag_conv(f1);
  GET_OBJ_WEAR(obj_proto + i) = asciiflag_conv(f2);
  free(f1);
  free(f2);

  values = toml_array_required(obj, "values", buf2, filename);
  if (toml_array_nelem(values) < 4) {
    log("SYSERR: Object %s has fewer than 4 values.", buf2);
    exit(1);
  }
  for (j = 0; j < 4; j++) {
    toml_datum_t val = toml_int_at(values, j);
    if (!val.ok) {
      log("SYSERR: Object %s has invalid value[%d].", buf2, j);
      exit(1);
    }
    GET_OBJ_VAL(obj_proto + i, j) = (int)val.u.i;
  }

  GET_OBJ_WEIGHT(obj_proto + i) = toml_int_required(obj, "weight", buf2, filename);
  GET_OBJ_COST(obj_proto + i) = toml_int_required(obj, "cost", buf2, filename);
  GET_OBJ_RENT(obj_proto + i) = toml_int_required(obj, "rent", buf2, filename);

  if (GET_OBJ_TYPE(obj_proto + i) == ITEM_DRINKCON || GET_OBJ_TYPE(obj_proto + i) == ITEM_FOUNTAIN) {
    if (GET_OBJ_WEIGHT(obj_proto + i) < GET_OBJ_VAL(obj_proto + i, 1))
      GET_OBJ_WEIGHT(obj_proto + i) = GET_OBJ_VAL(obj_proto + i, 1) + 5;
  }

  for (j = 0; j < MAX_OBJ_AFFECT; j++) {
    obj_proto[i].affected[j].location = APPLY_NONE;
    obj_proto[i].affected[j].modifier = 0;
  }

  extra = toml_array_in(obj, "extra_desc");
  if (extra) {
    count = toml_array_nelem(extra);
    for (j = 0; j < count; j++) {
      toml_table_t *extra_tab = toml_table_at(extra, j);
      if (!extra_tab) {
	log("SYSERR: Object %s has non-table extra_desc entry.", buf2);
	exit(1);
      }
      CREATE(new_descr, struct extra_descr_data, 1);
      new_descr->keyword = toml_string_required(extra_tab, "keyword", buf2, filename);
      new_descr->description = toml_string_required(extra_tab, "description", buf2, filename);
      new_descr->next = obj_proto[i].ex_description;
      obj_proto[i].ex_description = new_descr;
    }
  }

  affects = toml_array_in(obj, "affects");
  if (affects) {
    count = toml_array_nelem(affects);
    if (count > MAX_OBJ_AFFECT) {
      log("SYSERR: Too many affects (%d max), %s", MAX_OBJ_AFFECT, buf2);
      exit(1);
    }
    for (j = 0; j < count; j++) {
      toml_table_t *aff_tab = toml_table_at(affects, j);
      if (!aff_tab) {
	log("SYSERR: Object %s has non-table affects entry.", buf2);
	exit(1);
      }
      obj_proto[i].affected[j].location = toml_int_required(aff_tab, "location", buf2, filename);
      obj_proto[i].affected[j].modifier = toml_int_required(aff_tab, "modifier", buf2, filename);
    }
  }

  check_object(obj_proto + i);
  top_of_objt = i++;
}

char *parse_object(FILE *obj_f, int nr)
{
  static int i = 0;
  static char line[READ_SIZE];
  int t[10], j, retval;
  char *tmpptr;
  char f1[READ_SIZE], f2[READ_SIZE], buf2[128];
  struct extra_descr_data *new_descr;

  obj_index[i].vnum = nr;
  obj_index[i].number = 0;
  obj_index[i].func = NULL;

  clear_object(obj_proto + i);
  obj_proto[i].item_number = i;

  sprintf(buf2, "object #%d", nr);	/* sprintf: OK (for 'buf2 >= 19') */

  /* *** string data *** */
  if ((obj_proto[i].name = fread_string(obj_f, buf2)) == NULL) {
    log("SYSERR: Null obj name or format error at or near %s", buf2);
    exit(1);
  }
  tmpptr = obj_proto[i].short_description = fread_string(obj_f, buf2);
  if (tmpptr && *tmpptr)
    if (!str_cmp(fname(tmpptr), "a") || !str_cmp(fname(tmpptr), "an") ||
	!str_cmp(fname(tmpptr), "the"))
      *tmpptr = LOWER(*tmpptr);

  tmpptr = obj_proto[i].description = fread_string(obj_f, buf2);
  if (tmpptr && *tmpptr)
    CAP(tmpptr);
  obj_proto[i].action_description = fread_string(obj_f, buf2);

  /* *** numeric data *** */
  if (!get_line(obj_f, line)) {
    log("SYSERR: Expecting first numeric line of %s, but file ended!", buf2);
    exit(1);
  }
  if ((retval = sscanf(line, " %d %s %s", t, f1, f2)) != 3) {
    log("SYSERR: Format error in first numeric line (expecting 3 args, got %d), %s", retval, buf2);
    exit(1);
  }

  /* Object flags checked in check_object(). */
  GET_OBJ_TYPE(obj_proto + i) = t[0];
  GET_OBJ_EXTRA(obj_proto + i) = asciiflag_conv(f1);
  GET_OBJ_WEAR(obj_proto + i) = asciiflag_conv(f2);

  if (!get_line(obj_f, line)) {
    log("SYSERR: Expecting second numeric line of %s, but file ended!", buf2);
    exit(1);
  }
  if ((retval = sscanf(line, "%d %d %d %d", t, t + 1, t + 2, t + 3)) != 4) {
    log("SYSERR: Format error in second numeric line (expecting 4 args, got %d), %s", retval, buf2);
    exit(1);
  }
  GET_OBJ_VAL(obj_proto + i, 0) = t[0];
  GET_OBJ_VAL(obj_proto + i, 1) = t[1];
  GET_OBJ_VAL(obj_proto + i, 2) = t[2];
  GET_OBJ_VAL(obj_proto + i, 3) = t[3];

  if (!get_line(obj_f, line)) {
    log("SYSERR: Expecting third numeric line of %s, but file ended!", buf2);
    exit(1);
  }
  if ((retval = sscanf(line, "%d %d %d", t, t + 1, t + 2)) != 3) {
    log("SYSERR: Format error in third numeric line (expecting 3 args, got %d), %s", retval, buf2);
    exit(1);
  }
  GET_OBJ_WEIGHT(obj_proto + i) = t[0];
  GET_OBJ_COST(obj_proto + i) = t[1];
  GET_OBJ_RENT(obj_proto + i) = t[2];

  /* check to make sure that weight of containers exceeds curr. quantity */
  if (GET_OBJ_TYPE(obj_proto + i) == ITEM_DRINKCON || GET_OBJ_TYPE(obj_proto + i) == ITEM_FOUNTAIN) {
    if (GET_OBJ_WEIGHT(obj_proto + i) < GET_OBJ_VAL(obj_proto + i, 1))
      GET_OBJ_WEIGHT(obj_proto + i) = GET_OBJ_VAL(obj_proto + i, 1) + 5;
  }

  /* *** extra descriptions and affect fields *** */

  for (j = 0; j < MAX_OBJ_AFFECT; j++) {
    obj_proto[i].affected[j].location = APPLY_NONE;
    obj_proto[i].affected[j].modifier = 0;
  }

  strcat(buf2, ", after numeric constants\n"	/* strcat: OK (for 'buf2 >= 87') */
	 "...expecting 'E', 'A', '$', or next object number");
  j = 0;

  for (;;) {
    if (!get_line(obj_f, line)) {
      log("SYSERR: Format error in %s", buf2);
      exit(1);
    }
    switch (*line) {
    case 'E':
      CREATE(new_descr, struct extra_descr_data, 1);
      new_descr->keyword = fread_string(obj_f, buf2);
      new_descr->description = fread_string(obj_f, buf2);
      new_descr->next = obj_proto[i].ex_description;
      obj_proto[i].ex_description = new_descr;
      break;
    case 'A':
      if (j >= MAX_OBJ_AFFECT) {
	log("SYSERR: Too many A fields (%d max), %s", MAX_OBJ_AFFECT, buf2);
	exit(1);
      }
      if (!get_line(obj_f, line)) {
	log("SYSERR: Format error in 'A' field, %s\n"
	    "...expecting 2 numeric constants but file ended!", buf2);
	exit(1);
      }

      if ((retval = sscanf(line, " %d %d ", t, t + 1)) != 2) {
	log("SYSERR: Format error in 'A' field, %s\n"
	    "...expecting 2 numeric arguments, got %d\n"
	    "...offending line: '%s'", buf2, retval, line);
	exit(1);
      }
      obj_proto[i].affected[j].location = t[0];
      obj_proto[i].affected[j].modifier = t[1];
      j++;
      break;
    case '$':
    case '#':
      check_object(obj_proto + i);
      top_of_objt = i++;
      return (line);
    default:
      log("SYSERR: Format error in (%c): %s", *line, buf2);
      exit(1);
    }
  }
}


#define Z	zone_table[zone]

/* load the zone table and command tables */
void load_zones(FILE *fl, char *zonename)
{
  static zone_rnum zone = 0;
  char errbuf[256];
  toml_table_t *tab;
  toml_array_t *zones;
  int zcount, znum;

  tab = toml_parse_file(fl, errbuf, sizeof(errbuf));
  if (!tab) {
    log("SYSERR: TOML parse error in %s: %s", zonename, errbuf);
    exit(1);
  }

  zones = toml_array_required(tab, "zones", "top-level", zonename);
  zcount = toml_array_nelem(zones);
  if (zcount == 0) {
    log("SYSERR: %s is empty!", zonename);
    toml_free(tab);
    exit(1);
  }

  for (znum = 0; znum < zcount; znum++) {
    toml_table_t *ztab = toml_table_at(zones, znum);
    toml_array_t *cmds;
    int cmd_no, cmd_count;
    char buf2[MAX_STRING_LENGTH];

    if (!ztab) {
      log("SYSERR: TOML file %s has non-table zone entry.", zonename);
      toml_free(tab);
      exit(1);
    }

    Z.number = toml_int_required(ztab, "vnum", "zone entry", zonename);
    snprintf(buf2, sizeof(buf2), "zone #%d", Z.number);

    Z.name = toml_string_required(ztab, "name", buf2, zonename);
    Z.bot = toml_int_required(ztab, "bot", buf2, zonename);
    Z.top = toml_int_required(ztab, "top", buf2, zonename);
    Z.lifespan = toml_int_required(ztab, "lifespan", buf2, zonename);
    Z.reset_mode = toml_int_required(ztab, "reset_mode", buf2, zonename);

    if (Z.bot > Z.top) {
      log("SYSERR: Zone %d bottom (%d) > top (%d).", Z.number, Z.bot, Z.top);
      toml_free(tab);
      exit(1);
    }

    cmds = toml_array_required(ztab, "commands", buf2, zonename);
    cmd_count = toml_array_nelem(cmds);
    if (cmd_count == 0) {
      log("SYSERR: Zone %d has no commands in %s.", Z.number, zonename);
      toml_free(tab);
      exit(1);
    }

    CREATE(Z.cmd, struct reset_com, cmd_count + 1);
    for (cmd_no = 0; cmd_no < cmd_count; cmd_no++) {
      toml_table_t *ctab = toml_table_at(cmds, cmd_no);
      char *cmdstr;
      toml_datum_t arg3;

      if (!ctab) {
	log("SYSERR: Zone %d has non-table command entry in %s.", Z.number, zonename);
	toml_free(tab);
	exit(1);
      }

      cmdstr = toml_string_required(ctab, "command", buf2, zonename);
      ZCMD.command = *cmdstr;
      free(cmdstr);

      ZCMD.if_flag = toml_int_required(ctab, "if_flag", buf2, zonename);
      ZCMD.arg1 = toml_int_required(ctab, "arg1", buf2, zonename);
      ZCMD.arg2 = toml_int_required(ctab, "arg2", buf2, zonename);
      arg3 = toml_int_in(ctab, "arg3");
      ZCMD.arg3 = arg3.ok ? (int)arg3.u.i : 0;
      ZCMD.line = cmd_no + 1;
    }

    Z.cmd[cmd_count].command = 'S';
    Z.cmd[cmd_count].if_flag = 0;
    Z.cmd[cmd_count].arg1 = 0;
    Z.cmd[cmd_count].arg2 = 0;
    Z.cmd[cmd_count].arg3 = 0;
    Z.cmd[cmd_count].line = 0;

    top_of_zone_table = zone++;
  }

  toml_free(tab);
}

#undef Z


void get_one_line(FILE *fl, char *buf)
{
  if (fgets(buf, READ_SIZE, fl) == NULL) {
    log("SYSERR: error reading help file: not terminated with $?");
    exit(1);
  }

  buf[strlen(buf) - 1] = '\0'; /* take off the trailing \n */
}


void free_help(void)
{
  int hp;

  if (!help_table)
     return;

  for (hp = 0; hp <= top_of_helpt; hp++) {
    if (help_table[hp].keyword)
      free(help_table[hp].keyword);
    if (help_table[hp].entry && !help_table[hp].duplicate)
      free(help_table[hp].entry);
  }

  free(help_table);
  help_table = NULL;
  top_of_helpt = 0;
}


void load_help(FILE *fl)
{
#if defined(CIRCLE_MACINTOSH)
  static char key[READ_SIZE + 1], next_key[READ_SIZE + 1], entry[32384]; /* too big for stack? */
#else
  char key[READ_SIZE + 1], next_key[READ_SIZE + 1], entry[32384];
#endif
  size_t entrylen;
  char line[READ_SIZE + 1], *scan;
  struct help_index_element el;

  /* get the first keyword line */
  get_one_line(fl, key);
  while (*key != '$') {
    strcat(key, "\r\n");	/* strcat: OK (READ_SIZE - "\n" + "\r\n" == READ_SIZE + 1) */
    entrylen = strlcpy(entry, key, sizeof(entry));

    /* read in the corresponding help entry */
    get_one_line(fl, line);
    while (*line != '#' && entrylen < sizeof(entry) - 1) {
      entrylen += strlcpy(entry + entrylen, line, sizeof(entry) - entrylen);

      if (entrylen + 2 < sizeof(entry) - 1) {
        strcpy(entry + entrylen, "\r\n");	/* strcpy: OK (size checked above) */
        entrylen += 2;
      }
      get_one_line(fl, line);
    }

    if (entrylen >= sizeof(entry) - 1) {
      int keysize;
      const char *truncmsg = "\r\n*TRUNCATED*\r\n";

      strcpy(entry + sizeof(entry) - strlen(truncmsg) - 1, truncmsg);	/* strcpy: OK (assuming sane 'entry' size) */

      keysize = strlen(key) - 2;
      log("SYSERR: Help entry exceeded buffer space: %.*s", keysize, key);

      /* If we ran out of buffer space, eat the rest of the entry. */
      while (*line != '#')
        get_one_line(fl, line);
    }

    /* now, add the entry to the index with each keyword on the keyword line */
    el.duplicate = 0;
    el.entry = strdup(entry);
    scan = one_word(key, next_key);
    while (*next_key) {
      el.keyword = strdup(next_key);
      help_table[top_of_helpt++] = el;
      el.duplicate++;
      scan = one_word(scan, next_key);
    }

    /* get next keyword line (or $) */
    get_one_line(fl, key);
  }
}


int hsort(const void *a, const void *b)
{
  const struct help_index_element *a1, *b1;

  a1 = (const struct help_index_element *) a;
  b1 = (const struct help_index_element *) b;

  return (str_cmp(a1->keyword, b1->keyword));
}


/*************************************************************************
*  procedures for resetting, both play-time and boot-time	 	 *
*************************************************************************/


int vnum_mobile(char *searchname, struct char_data *ch)
{
  int nr, found = 0;

  for (nr = 0; nr <= top_of_mobt; nr++)
    if (isname(searchname, mob_proto[nr].player.name))
      send_to_char(ch, "%3d. [%5d] %s\r\n", ++found, mob_index[nr].vnum, mob_proto[nr].player.short_descr);

  return (found);
}



int vnum_object(char *searchname, struct char_data *ch)
{
  int nr, found = 0;

  for (nr = 0; nr <= top_of_objt; nr++)
    if (isname(searchname, obj_proto[nr].name))
      send_to_char(ch, "%3d. [%5d] %s\r\n", ++found, obj_index[nr].vnum, obj_proto[nr].short_description);

  return (found);
}


/* create a character, and add it to the char list */
struct char_data *create_char(void)
{
  struct char_data *ch;

  CREATE(ch, struct char_data, 1);
  clear_char(ch);
  ch->next = character_list;
  character_list = ch;

  return (ch);
}


/* create a new mobile from a prototype */
struct char_data *read_mobile(mob_vnum nr, int type) /* and mob_rnum */
{
  mob_rnum i;
  struct char_data *mob;

  if (type == VIRTUAL) {
    if ((i = real_mobile(nr)) == NOBODY) {
      log("WARNING: Mobile vnum %d does not exist in database.", nr);
      return (NULL);
    }
  } else
    i = nr;

  CREATE(mob, struct char_data, 1);
  clear_char(mob);
  *mob = mob_proto[i];
  mob->next = character_list;
  character_list = mob;

  if (!mob->points.max_hit) {
    mob->points.max_hit = dice(mob->points.hit, mob->points.mana) +
      mob->points.move;
  } else
    mob->points.max_hit = rand_number(mob->points.hit, mob->points.mana);

  mob->points.hit = mob->points.max_hit;
  mob->points.mana = mob->points.max_mana;
  mob->points.move = mob->points.max_move;

  mob->player.time.birth = time(0);
  mob->player.time.played = 0;
  mob->player.time.logon = time(0);

  mob_index[i].number++;

  return (mob);
}


/* create an object, and add it to the object list */
struct obj_data *create_obj(void)
{
  struct obj_data *obj;

  CREATE(obj, struct obj_data, 1);
  clear_object(obj);
  obj->next = object_list;
  object_list = obj;

  return (obj);
}


/* create a new object from a prototype */
struct obj_data *read_object(obj_vnum nr, int type) /* and obj_rnum */
{
  struct obj_data *obj;
  obj_rnum i = type == VIRTUAL ? real_object(nr) : nr;

  if (i == NOTHING || i > top_of_objt) {
    log("Object (%c) %d does not exist in database.", type == VIRTUAL ? 'V' : 'R', nr);
    return (NULL);
  }

  CREATE(obj, struct obj_data, 1);
  clear_object(obj);
  *obj = obj_proto[i];
  obj->next = object_list;
  object_list = obj;

  obj_index[i].number++;

  return (obj);
}



#define ZO_DEAD  999

/* update zone ages, queue for reset if necessary, and dequeue when possible */
void zone_update(void)
{
  int i;
  struct reset_q_element *update_u, *temp;
  static int timer = 0;

  /* jelson 10/22/92 */
  if (((++timer * PULSE_ZONE) / PASSES_PER_SEC) >= 60) {
    /* one minute has passed */
    /*
     * NOT accurate unless PULSE_ZONE is a multiple of PASSES_PER_SEC or a
     * factor of 60
     */

    timer = 0;

    /* since one minute has passed, increment zone ages */
    for (i = 0; i <= top_of_zone_table; i++) {
      if (zone_table[i].age < zone_table[i].lifespan &&
	  zone_table[i].reset_mode)
	(zone_table[i].age)++;

      if (zone_table[i].age >= zone_table[i].lifespan &&
	  zone_table[i].age < ZO_DEAD && zone_table[i].reset_mode) {
	/* enqueue zone */

	CREATE(update_u, struct reset_q_element, 1);

	update_u->zone_to_reset = i;
	update_u->next = 0;

	if (!reset_q.head)
	  reset_q.head = reset_q.tail = update_u;
	else {
	  reset_q.tail->next = update_u;
	  reset_q.tail = update_u;
	}

	zone_table[i].age = ZO_DEAD;
      }
    }
  }	/* end - one minute has passed */


  /* dequeue zones (if possible) and reset */
  /* this code is executed every 10 seconds (i.e. PULSE_ZONE) */
  for (update_u = reset_q.head; update_u; update_u = update_u->next)
    if (zone_table[update_u->zone_to_reset].reset_mode == 2 ||
	is_empty(update_u->zone_to_reset)) {
      reset_zone(update_u->zone_to_reset);
      mudlog(CMP, LVL_GOD, FALSE, "Auto zone reset: %s", zone_table[update_u->zone_to_reset].name);
      /* dequeue */
      if (update_u == reset_q.head)
	reset_q.head = reset_q.head->next;
      else {
	for (temp = reset_q.head; temp->next != update_u;
	     temp = temp->next);

	if (!update_u->next)
	  reset_q.tail = temp;

	temp->next = update_u->next;
      }

      free(update_u);
      break;
    }
}

void log_zone_error(zone_rnum zone, int cmd_no, const char *message)
{
  mudlog(NRM, LVL_GOD, TRUE, "SYSERR: zone file: %s", message);
  mudlog(NRM, LVL_GOD, TRUE, "SYSERR: ...offending cmd: '%c' cmd in zone #%d, line %d",
	ZCMD.command, zone_table[zone].number, ZCMD.line);
}

#define ZONE_ERROR(message) \
	{ log_zone_error(zone, cmd_no, message); last_cmd = 0; }

/* execute the reset command table of a given zone */
void reset_zone(zone_rnum zone)
{
  int cmd_no, last_cmd = 0;
  struct char_data *mob = NULL;
  struct obj_data *obj, *obj_to;

  for (cmd_no = 0; ZCMD.command != 'S'; cmd_no++) {

    if (ZCMD.if_flag && !last_cmd)
      continue;

    /*  This is the list of actual zone commands.  If any new
     *  zone commands are added to the game, be certain to update
     *  the list of commands in load_zone() so that the counting
     *  will still be correct. - ae.
     */
    switch (ZCMD.command) {
    case '*':			/* ignore command */
      last_cmd = 0;
      break;

    case 'M':			/* read a mobile */
      if (mob_index[ZCMD.arg1].number < ZCMD.arg2) {
	mob = read_mobile(ZCMD.arg1, REAL);
	char_to_room(mob, ZCMD.arg3);
	last_cmd = 1;
      } else
	last_cmd = 0;
      break;

    case 'O':			/* read an object */
      if (obj_index[ZCMD.arg1].number < ZCMD.arg2) {
	if (ZCMD.arg3 != NOWHERE) {
	  obj = read_object(ZCMD.arg1, REAL);
	  obj_to_room(obj, ZCMD.arg3);
	  last_cmd = 1;
	} else {
	  obj = read_object(ZCMD.arg1, REAL);
	  IN_ROOM(obj) = NOWHERE;
	  last_cmd = 1;
	}
      } else
	last_cmd = 0;
      break;

    case 'P':			/* object to object */
      if (obj_index[ZCMD.arg1].number < ZCMD.arg2) {
	obj = read_object(ZCMD.arg1, REAL);
	if (!(obj_to = get_obj_num(ZCMD.arg3))) {
	  ZONE_ERROR("target obj not found, command disabled");
	  ZCMD.command = '*';
	  break;
	}
	obj_to_obj(obj, obj_to);
	last_cmd = 1;
      } else
	last_cmd = 0;
      break;

    case 'G':			/* obj_to_char */
      if (!mob) {
	ZONE_ERROR("attempt to give obj to non-existant mob, command disabled");
	ZCMD.command = '*';
	break;
      }
      if (obj_index[ZCMD.arg1].number < ZCMD.arg2) {
	obj = read_object(ZCMD.arg1, REAL);
	obj_to_char(obj, mob);
	last_cmd = 1;
      } else
	last_cmd = 0;
      break;

    case 'E':			/* object to equipment list */
      if (!mob) {
	ZONE_ERROR("trying to equip non-existant mob, command disabled");
	ZCMD.command = '*';
	break;
      }
      if (obj_index[ZCMD.arg1].number < ZCMD.arg2) {
	if (ZCMD.arg3 < 0 || ZCMD.arg3 >= NUM_WEARS) {
	  ZONE_ERROR("invalid equipment pos number");
	} else {
	  obj = read_object(ZCMD.arg1, REAL);
	  equip_char(mob, obj, ZCMD.arg3);
	  last_cmd = 1;
	}
      } else
	last_cmd = 0;
      break;

    case 'R': /* rem obj from room */
      if ((obj = get_obj_in_list_num(ZCMD.arg2, world[ZCMD.arg1].contents)) != NULL)
        extract_obj(obj);
      last_cmd = 1;
      break;


    case 'D':			/* set state of door */
      if (ZCMD.arg2 < 0 || ZCMD.arg2 >= NUM_OF_DIRS ||
	  (world[ZCMD.arg1].dir_option[ZCMD.arg2] == NULL)) {
	ZONE_ERROR("door does not exist, command disabled");
	ZCMD.command = '*';
      } else
	switch (ZCMD.arg3) {
	case 0:
	  REMOVE_BIT(world[ZCMD.arg1].dir_option[ZCMD.arg2]->exit_info,
		     EX_LOCKED);
	  REMOVE_BIT(world[ZCMD.arg1].dir_option[ZCMD.arg2]->exit_info,
		     EX_CLOSED);
	  break;
	case 1:
	  SET_BIT(world[ZCMD.arg1].dir_option[ZCMD.arg2]->exit_info,
		  EX_CLOSED);
	  REMOVE_BIT(world[ZCMD.arg1].dir_option[ZCMD.arg2]->exit_info,
		     EX_LOCKED);
	  break;
	case 2:
	  SET_BIT(world[ZCMD.arg1].dir_option[ZCMD.arg2]->exit_info,
		  EX_LOCKED);
	  SET_BIT(world[ZCMD.arg1].dir_option[ZCMD.arg2]->exit_info,
		  EX_CLOSED);
	  break;
	}
      last_cmd = 1;
      break;

    default:
      ZONE_ERROR("unknown cmd in reset table; cmd disabled");
      ZCMD.command = '*';
      break;
    }
  }

  zone_table[zone].age = 0;
}



/* for use in reset_zone; return TRUE if zone 'nr' is free of PC's  */
int is_empty(zone_rnum zone_nr)
{
  struct descriptor_data *i;

  for (i = descriptor_list; i; i = i->next) {
    if (STATE(i) != CON_PLAYING)
      continue;
    if (IN_ROOM(i->character) == NOWHERE)
      continue;
    if (GET_LEVEL(i->character) >= LVL_IMMORT)
      continue;
    if (world[IN_ROOM(i->character)].zone != zone_nr)
      continue;

    return (0);
  }

  return (1);
}





/*************************************************************************
*  stuff related to the save/load player system				 *
*************************************************************************/


long get_ptable_by_name(const char *name)
{
  int i;

  for (i = 0; i <= top_of_p_table; i++)
    if (!str_cmp(player_table[i].name, name))
      return (i);

  return (-1);
}


long get_id_by_name(const char *name)
{
  int i;

  for (i = 0; i <= top_of_p_table; i++)
    if (!str_cmp(player_table[i].name, name))
      return (player_table[i].id);

  return (-1);
}


char *get_name_by_id(long id)
{
  int i;

  for (i = 0; i <= top_of_p_table; i++)
    if (player_table[i].id == id)
      return (player_table[i].name);

  return (NULL);
}

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

static void toml_write_int_array(FILE *fp, const char *key, const int *vals, int count)
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

static void toml_write_bool_array(FILE *fp, const char *key, const bool *vals, int count)
{
  int i;

  fprintf(fp, "%s = [", key);
  for (i = 0; i < count; i++) {
    if (i)
      fputs(", ", fp);
    fputs(vals[i] ? "true" : "false", fp);
  }
  fputs("]\n", fp);
}

static int read_player_toml_table(toml_table_t *tab, struct char_file_u *st, const char *filename)
{
  toml_table_t *cs, *ps, *ab, *pt;
  toml_array_t *aff;
  int tmp[MAX_SKILLS + 1];
  int i;

  memset(st, 0, sizeof(*st));

  toml_copy_string_required(st->name, sizeof(st->name), tab, "name", "player", filename);
  toml_copy_string_optional(st->description, sizeof(st->description), tab, "description");
  toml_copy_string_optional(st->title, sizeof(st->title), tab, "title");
  st->sex = toml_int_required(tab, "sex", "player", filename);
  st->chclass = toml_int_required(tab, "class", "player", filename);
  st->level = toml_int_required(tab, "level", "player", filename);
  st->hometown = toml_int_required(tab, "hometown", "player", filename);
  st->birth = toml_int_required(tab, "birth", "player", filename);
  st->played = toml_int_required(tab, "played", "player", filename);
  st->weight = toml_int_required(tab, "weight", "player", filename);
  st->height = toml_int_required(tab, "height", "player", filename);
  toml_copy_string_required(st->pwd, sizeof(st->pwd), tab, "password", "player", filename);
  st->last_logon = toml_int_required(tab, "last_logon", "player", filename);
  toml_copy_string_optional(st->host, sizeof(st->host), tab, "host");

  cs = toml_table_in(tab, "char_specials");
  if (!cs) {
    log("SYSERR: TOML file %s missing [char_specials].", filename);
    return (0);
  }
  st->char_specials_saved.alignment = toml_int_required(cs, "alignment", "char_specials", filename);
  st->char_specials_saved.idnum = toml_int_required(cs, "idnum", "char_specials", filename);
  st->char_specials_saved.act = toml_int_required(cs, "act", "char_specials", filename);
  st->char_specials_saved.affected_by = toml_int_required(cs, "affected_by", "char_specials", filename);
  toml_int_array_required(cs, "saving_throw", tmp, 5, "char_specials", filename);
  for (i = 0; i < 5; i++)
    st->char_specials_saved.apply_saving_throw[i] = (sh_int)tmp[i];

  ps = toml_table_in(tab, "player_specials");
  if (!ps) {
    log("SYSERR: TOML file %s missing [player_specials].", filename);
    return (0);
  }
  toml_int_array_required(ps, "skills", tmp, MAX_SKILLS + 1, "player_specials", filename);
  for (i = 0; i <= MAX_SKILLS; i++)
    st->player_specials_saved.skills[i] = (byte)tmp[i];
  st->player_specials_saved.PADDING0 = toml_int_optional(ps, "padding0", 0);
  toml_bool_array_required(ps, "talks", st->player_specials_saved.talks, MAX_TONGUE, "player_specials", filename);
  st->player_specials_saved.wimp_level = toml_int_required(ps, "wimp_level", "player_specials", filename);
  st->player_specials_saved.freeze_level = toml_int_required(ps, "freeze_level", "player_specials", filename);
  st->player_specials_saved.invis_level = toml_int_required(ps, "invis_level", "player_specials", filename);
  st->player_specials_saved.load_room = toml_int_required(ps, "load_room", "player_specials", filename);
  st->player_specials_saved.pref = toml_int_required(ps, "pref", "player_specials", filename);
  st->player_specials_saved.bad_pws = toml_int_required(ps, "bad_pws", "player_specials", filename);
  toml_int_array_required(ps, "conditions", tmp, 3, "player_specials", filename);
  for (i = 0; i < 3; i++)
    st->player_specials_saved.conditions[i] = (sbyte)tmp[i];

  st->player_specials_saved.spare0 = toml_int_optional(ps, "spare0", 0);
  st->player_specials_saved.spare1 = toml_int_optional(ps, "spare1", 0);
  st->player_specials_saved.spare2 = toml_int_optional(ps, "spare2", 0);
  st->player_specials_saved.spare3 = toml_int_optional(ps, "spare3", 0);
  st->player_specials_saved.spare4 = toml_int_optional(ps, "spare4", 0);
  st->player_specials_saved.spare5 = toml_int_optional(ps, "spare5", 0);
  st->player_specials_saved.spells_to_learn = toml_int_optional(ps, "spells_to_learn", 0);
  st->player_specials_saved.spare7 = toml_int_optional(ps, "spare7", 0);
  st->player_specials_saved.spare8 = toml_int_optional(ps, "spare8", 0);
  st->player_specials_saved.spare9 = toml_int_optional(ps, "spare9", 0);
  st->player_specials_saved.spare10 = toml_int_optional(ps, "spare10", 0);
  st->player_specials_saved.spare11 = toml_int_optional(ps, "spare11", 0);
  st->player_specials_saved.spare12 = toml_int_optional(ps, "spare12", 0);
  st->player_specials_saved.spare13 = toml_int_optional(ps, "spare13", 0);
  st->player_specials_saved.spare14 = toml_int_optional(ps, "spare14", 0);
  st->player_specials_saved.spare15 = toml_int_optional(ps, "spare15", 0);
  st->player_specials_saved.spare16 = toml_int_optional(ps, "spare16", 0);
  st->player_specials_saved.spare17 = toml_int_optional(ps, "spare17", 0);
  st->player_specials_saved.spare18 = toml_int_optional(ps, "spare18", 0);
  st->player_specials_saved.spare19 = toml_int_optional(ps, "spare19", 0);
  st->player_specials_saved.spare20 = toml_int_optional(ps, "spare20", 0);
  st->player_specials_saved.spare21 = toml_int_optional(ps, "spare21", 0);

  ab = toml_table_in(tab, "abilities");
  if (!ab) {
    log("SYSERR: TOML file %s missing [abilities].", filename);
    return (0);
  }
  st->abilities.str = toml_int_required(ab, "str", "abilities", filename);
  st->abilities.str_add = toml_int_required(ab, "str_add", "abilities", filename);
  st->abilities.intel = toml_int_required(ab, "intel", "abilities", filename);
  st->abilities.wis = toml_int_required(ab, "wis", "abilities", filename);
  st->abilities.dex = toml_int_required(ab, "dex", "abilities", filename);
  st->abilities.con = toml_int_required(ab, "con", "abilities", filename);
  st->abilities.cha = toml_int_required(ab, "cha", "abilities", filename);

  pt = toml_table_in(tab, "points");
  if (!pt) {
    log("SYSERR: TOML file %s missing [points].", filename);
    return (0);
  }
  st->points.mana = toml_int_required(pt, "mana", "points", filename);
  st->points.max_mana = toml_int_required(pt, "max_mana", "points", filename);
  st->points.hit = toml_int_required(pt, "hit", "points", filename);
  st->points.max_hit = toml_int_required(pt, "max_hit", "points", filename);
  st->points.move = toml_int_required(pt, "move", "points", filename);
  st->points.max_move = toml_int_required(pt, "max_move", "points", filename);
  st->points.armor = toml_int_required(pt, "armor", "points", filename);
  st->points.gold = toml_int_required(pt, "gold", "points", filename);
  st->points.bank_gold = toml_int_required(pt, "bank_gold", "points", filename);
  st->points.exp = toml_int_required(pt, "exp", "points", filename);
  st->points.hitroll = toml_int_required(pt, "hitroll", "points", filename);
  st->points.damroll = toml_int_required(pt, "damroll", "points", filename);

  for (i = 0; i < MAX_AFFECT; i++) {
    st->affected[i].type = 0;
    st->affected[i].duration = 0;
    st->affected[i].modifier = 0;
    st->affected[i].location = 0;
    st->affected[i].bitvector = 0;
    st->affected[i].next = 0;
  }
  aff = toml_array_in(tab, "affected");
  if (aff) {
    int count = toml_array_nelem(aff);
    if (count > MAX_AFFECT)
      count = MAX_AFFECT;
    for (i = 0; i < count; i++) {
      toml_table_t *atab = toml_table_at(aff, i);
      if (!atab) {
	log("SYSERR: TOML file %s has non-table affected entry.", filename);
	return (0);
      }
      st->affected[i].type = toml_int_required(atab, "type", "affected", filename);
      st->affected[i].duration = toml_int_required(atab, "duration", "affected", filename);
      st->affected[i].modifier = toml_int_required(atab, "modifier", "affected", filename);
      st->affected[i].location = toml_int_required(atab, "location", "affected", filename);
      st->affected[i].bitvector = toml_int_required(atab, "bitvector", "affected", filename);
      st->affected[i].next = 0;
    }
  }

  return (1);
}

static int read_player_toml_file(const char *filename, struct char_file_u *st)
{
  char errbuf[256];
  toml_table_t *tab;
  FILE *fl;
  int ok;

  if (!(fl = fopen(filename, "r")))
    return (0);

  tab = toml_parse_file(fl, errbuf, sizeof(errbuf));
  fclose(fl);
  if (!tab) {
    log("SYSERR: TOML parse error in %s: %s", filename, errbuf);
    return (0);
  }

  ok = read_player_toml_table(tab, st, filename);
  toml_free(tab);
  return (ok);
}

static int write_player_toml_file(const char *filename, struct char_file_u *st)
{
  FILE *fl;
  int i;
  int tmp[MAX_SKILLS + 1];
  int cond[3];

  if (!(fl = fopen(filename, "w"))) {
    log("SYSERR: Unable to write player file %s: %s", filename, strerror(errno));
    return (0);
  }

  toml_write_key_string(fl, "name", st->name);
  toml_write_key_string(fl, "description", st->description);
  toml_write_key_string(fl, "title", st->title);
  fprintf(fl, "sex = %d\n", st->sex);
  fprintf(fl, "class = %d\n", st->chclass);
  fprintf(fl, "level = %d\n", st->level);
  fprintf(fl, "hometown = %d\n", st->hometown);
  fprintf(fl, "birth = %ld\n", (long)st->birth);
  fprintf(fl, "played = %d\n", st->played);
  fprintf(fl, "weight = %d\n", st->weight);
  fprintf(fl, "height = %d\n", st->height);
  toml_write_key_string(fl, "password", st->pwd);
  fprintf(fl, "last_logon = %ld\n", (long)st->last_logon);
  toml_write_key_string(fl, "host", st->host);

  fputs("\n[char_specials]\n", fl);
  fprintf(fl, "alignment = %d\n", st->char_specials_saved.alignment);
  fprintf(fl, "idnum = %ld\n", st->char_specials_saved.idnum);
  fprintf(fl, "act = %ld\n", st->char_specials_saved.act);
  fprintf(fl, "affected_by = %ld\n", st->char_specials_saved.affected_by);
  for (i = 0; i < 5; i++)
    tmp[i] = st->char_specials_saved.apply_saving_throw[i];
  toml_write_int_array(fl, "saving_throw", tmp, 5);

  fputs("\n[player_specials]\n", fl);
  for (i = 0; i <= MAX_SKILLS; i++)
    tmp[i] = st->player_specials_saved.skills[i];
  toml_write_int_array(fl, "skills", tmp, MAX_SKILLS + 1);
  fprintf(fl, "padding0 = %d\n", st->player_specials_saved.PADDING0);
  toml_write_bool_array(fl, "talks", st->player_specials_saved.talks, MAX_TONGUE);
  fprintf(fl, "wimp_level = %d\n", st->player_specials_saved.wimp_level);
  fprintf(fl, "freeze_level = %d\n", st->player_specials_saved.freeze_level);
  fprintf(fl, "invis_level = %d\n", st->player_specials_saved.invis_level);
  fprintf(fl, "load_room = %d\n", st->player_specials_saved.load_room);
  fprintf(fl, "pref = %ld\n", st->player_specials_saved.pref);
  fprintf(fl, "bad_pws = %d\n", st->player_specials_saved.bad_pws);
  cond[0] = st->player_specials_saved.conditions[0];
  cond[1] = st->player_specials_saved.conditions[1];
  cond[2] = st->player_specials_saved.conditions[2];
  toml_write_int_array(fl, "conditions", cond, 3);
  fprintf(fl, "spare0 = %d\n", st->player_specials_saved.spare0);
  fprintf(fl, "spare1 = %d\n", st->player_specials_saved.spare1);
  fprintf(fl, "spare2 = %d\n", st->player_specials_saved.spare2);
  fprintf(fl, "spare3 = %d\n", st->player_specials_saved.spare3);
  fprintf(fl, "spare4 = %d\n", st->player_specials_saved.spare4);
  fprintf(fl, "spare5 = %d\n", st->player_specials_saved.spare5);
  fprintf(fl, "spells_to_learn = %d\n", st->player_specials_saved.spells_to_learn);
  fprintf(fl, "spare7 = %d\n", st->player_specials_saved.spare7);
  fprintf(fl, "spare8 = %d\n", st->player_specials_saved.spare8);
  fprintf(fl, "spare9 = %d\n", st->player_specials_saved.spare9);
  fprintf(fl, "spare10 = %d\n", st->player_specials_saved.spare10);
  fprintf(fl, "spare11 = %d\n", st->player_specials_saved.spare11);
  fprintf(fl, "spare12 = %d\n", st->player_specials_saved.spare12);
  fprintf(fl, "spare13 = %d\n", st->player_specials_saved.spare13);
  fprintf(fl, "spare14 = %d\n", st->player_specials_saved.spare14);
  fprintf(fl, "spare15 = %d\n", st->player_specials_saved.spare15);
  fprintf(fl, "spare16 = %d\n", st->player_specials_saved.spare16);
  fprintf(fl, "spare17 = %ld\n", st->player_specials_saved.spare17);
  fprintf(fl, "spare18 = %ld\n", st->player_specials_saved.spare18);
  fprintf(fl, "spare19 = %ld\n", st->player_specials_saved.spare19);
  fprintf(fl, "spare20 = %ld\n", st->player_specials_saved.spare20);
  fprintf(fl, "spare21 = %ld\n", st->player_specials_saved.spare21);

  fputs("\n[abilities]\n", fl);
  fprintf(fl, "str = %d\n", st->abilities.str);
  fprintf(fl, "str_add = %d\n", st->abilities.str_add);
  fprintf(fl, "intel = %d\n", st->abilities.intel);
  fprintf(fl, "wis = %d\n", st->abilities.wis);
  fprintf(fl, "dex = %d\n", st->abilities.dex);
  fprintf(fl, "con = %d\n", st->abilities.con);
  fprintf(fl, "cha = %d\n", st->abilities.cha);

  fputs("\n[points]\n", fl);
  fprintf(fl, "mana = %d\n", st->points.mana);
  fprintf(fl, "max_mana = %d\n", st->points.max_mana);
  fprintf(fl, "hit = %d\n", st->points.hit);
  fprintf(fl, "max_hit = %d\n", st->points.max_hit);
  fprintf(fl, "move = %d\n", st->points.move);
  fprintf(fl, "max_move = %d\n", st->points.max_move);
  fprintf(fl, "armor = %d\n", st->points.armor);
  fprintf(fl, "gold = %d\n", st->points.gold);
  fprintf(fl, "bank_gold = %d\n", st->points.bank_gold);
  fprintf(fl, "exp = %d\n", st->points.exp);
  fprintf(fl, "hitroll = %d\n", st->points.hitroll);
  fprintf(fl, "damroll = %d\n", st->points.damroll);

  for (i = 0; i < MAX_AFFECT; i++) {
    if (!st->affected[i].type)
      continue;
    fputs("\n[[affected]]\n", fl);
    fprintf(fl, "type = %d\n", st->affected[i].type);
    fprintf(fl, "duration = %d\n", st->affected[i].duration);
    fprintf(fl, "modifier = %d\n", st->affected[i].modifier);
    fprintf(fl, "location = %d\n", st->affected[i].location);
    fprintf(fl, "bitvector = %ld\n", st->affected[i].bitvector);
  }

  fclose(fl);
  return (1);
}

int save_char_file(const char *name, struct char_file_u *st)
{
  char filename[PATH_MAX];

  if (!name || !*name)
    return (0);

  ensure_player_dirs(name);
  if (!get_player_filename(filename, sizeof(filename), name))
    return (0);

  return (write_player_toml_file(filename, st));
}


/* Load a char, TRUE if loaded, FALSE if not */
int load_char(const char *name, struct char_file_u *char_element)
{
  int player_i;

  if ((player_i = get_ptable_by_name(name)) >= 0) {
    char filename[PATH_MAX];
    if (!get_player_filename(filename, sizeof(filename), name))
      return (-1);
    if (!read_player_toml_file(filename, char_element))
      return (-1);
    return (player_i);
  } else
    return (-1);
}




/*
 * write the vital data of a player to the player file
 *
 * And that's it! No more fudging around with the load room.
 * Unfortunately, 'host' modifying is still here due to lack
 * of that variable in the char_data structure.
 */
void save_char(struct char_data *ch)
{
  struct char_file_u st;

  if (IS_NPC(ch) || !ch->desc || GET_PFILEPOS(ch) < 0)
    return;

  char_to_store(ch, &st);

  strncpy(st.host, ch->desc->host, HOST_LENGTH);	/* strncpy: OK (s.host:HOST_LENGTH+1) */
  st.host[HOST_LENGTH] = '\0';

  save_char_file(GET_NAME(ch), &st);
}



/* copy data from the file structure to a char struct */
void store_to_char(struct char_file_u *st, struct char_data *ch)
{
  int i;

  /* to save memory, only PC's -- not MOB's -- have player_specials */
  if (ch->player_specials == NULL)
    CREATE(ch->player_specials, struct player_special_data, 1);

  GET_SEX(ch) = st->sex;
  GET_CLASS(ch) = st->chclass;
  GET_LEVEL(ch) = st->level;

  ch->player.short_descr = NULL;
  ch->player.long_descr = NULL;
  ch->player.title = strdup(st->title);
  ch->player.description = strdup(st->description);

  ch->player.hometown = st->hometown;
  ch->player.time.birth = st->birth;
  ch->player.time.played = st->played;
  ch->player.time.logon = time(0);

  ch->player.weight = st->weight;
  ch->player.height = st->height;

  ch->real_abils = st->abilities;
  ch->aff_abils = st->abilities;
  ch->points = st->points;
  ch->char_specials.saved = st->char_specials_saved;
  ch->player_specials->saved = st->player_specials_saved;
  POOFIN(ch) = NULL;
  POOFOUT(ch) = NULL;
  GET_LAST_TELL(ch) = NOBODY;

  if (ch->points.max_mana < 100)
    ch->points.max_mana = 100;

  ch->char_specials.carry_weight = 0;
  ch->char_specials.carry_items = 0;
  ch->points.armor = 100;
  ch->points.hitroll = 0;
  ch->points.damroll = 0;

  if (ch->player.name)
    free(ch->player.name);
  ch->player.name = strdup(st->name);
  strlcpy(ch->player.passwd, st->pwd, sizeof(ch->player.passwd));

  /* Add all spell effects */
  for (i = 0; i < MAX_AFFECT; i++) {
    if (st->affected[i].type)
      affect_to_char(ch, &st->affected[i]);
  }

  /*
   * If you're not poisioned and you've been away for more than an hour of
   * real time, we'll set your HMV back to full
   */

  if (!AFF_FLAGGED(ch, AFF_POISON) &&
	time(0) - st->last_logon >= SECS_PER_REAL_HOUR) {
    GET_HIT(ch) = GET_MAX_HIT(ch);
    GET_MOVE(ch) = GET_MAX_MOVE(ch);
    GET_MANA(ch) = GET_MAX_MANA(ch);
  }
}				/* store_to_char */




/* copy vital data from a players char-structure to the file structure */
void char_to_store(struct char_data *ch, struct char_file_u *st)
{
  int i;
  struct affected_type *af;
  struct obj_data *char_eq[NUM_WEARS];

  /* Unaffect everything a character can be affected by */

  for (i = 0; i < NUM_WEARS; i++) {
    if (GET_EQ(ch, i))
      char_eq[i] = unequip_char(ch, i);
    else
      char_eq[i] = NULL;
  }

  for (af = ch->affected, i = 0; i < MAX_AFFECT; i++) {
    if (af) {
      st->affected[i] = *af;
      st->affected[i].next = 0;
      af = af->next;
    } else {
      st->affected[i].type = 0;	/* Zero signifies not used */
      st->affected[i].duration = 0;
      st->affected[i].modifier = 0;
      st->affected[i].location = 0;
      st->affected[i].bitvector = 0;
      st->affected[i].next = 0;
    }
  }


  /*
   * remove the affections so that the raw values are stored; otherwise the
   * effects are doubled when the char logs back in.
   */

  while (ch->affected)
    affect_remove(ch, ch->affected);

  if ((i >= MAX_AFFECT) && af && af->next)
    log("SYSERR: WARNING: OUT OF STORE ROOM FOR AFFECTED TYPES!!!");

  ch->aff_abils = ch->real_abils;

  st->birth = ch->player.time.birth;
  st->played = ch->player.time.played;
  st->played += time(0) - ch->player.time.logon;
  st->last_logon = time(0);

  ch->player.time.played = st->played;
  ch->player.time.logon = time(0);

  st->hometown = ch->player.hometown;
  st->weight = GET_WEIGHT(ch);
  st->height = GET_HEIGHT(ch);
  st->sex = GET_SEX(ch);
  st->chclass = GET_CLASS(ch);
  st->level = GET_LEVEL(ch);
  st->abilities = ch->real_abils;
  st->points = ch->points;
  st->char_specials_saved = ch->char_specials.saved;
  st->player_specials_saved = ch->player_specials->saved;

  st->points.armor = 100;
  st->points.hitroll = 0;
  st->points.damroll = 0;

  if (GET_TITLE(ch))
    strlcpy(st->title, GET_TITLE(ch), MAX_TITLE_LENGTH);
  else
    *st->title = '\0';

  if (ch->player.description) {
    if (strlen(ch->player.description) >= sizeof(st->description)) {
      log("SYSERR: char_to_store: %s's description length: %zu, max: %zu! "
         "Truncated.", GET_PC_NAME(ch), strlen(ch->player.description),
         sizeof(st->description));
      ch->player.description[sizeof(st->description) - 3] = '\0';
      strcat(ch->player.description, "\r\n");	/* strcat: OK (previous line makes room) */
    }
    strcpy(st->description, ch->player.description);	/* strcpy: OK (checked above) */
  } else
    *st->description = '\0';

  strcpy(st->name, GET_NAME(ch));	/* strcpy: OK (that's what GET_NAME came from) */
  strcpy(st->pwd, GET_PASSWD(ch));	/* strcpy: OK (that's what GET_PASSWD came from) */

  /* add spell and eq affections back in now */
  for (i = 0; i < MAX_AFFECT; i++) {
    if (st->affected[i].type)
      affect_to_char(ch, &st->affected[i]);
  }

  for (i = 0; i < NUM_WEARS; i++) {
    if (char_eq[i])
      equip_char(ch, char_eq[i], i);
  }
/*   affect_total(ch); unnecessary, I think !?! */
}				/* Char to store */



void save_etext(struct char_data *ch)
{
  (void)ch;
/* this will be really cool soon */
}


/*
 * Create a new entry in the in-memory index table for the player file.
 * If the name already exists, by overwriting a deleted character, then
 * we re-use the old position.
 */
int create_entry(char *name)
{
  int i, pos;

  if (top_of_p_table == -1) {	/* no table */
    CREATE(player_table, struct player_index_element, 1);
    pos = top_of_p_table = 0;
  } else if ((pos = get_ptable_by_name(name)) == -1) {	/* new name */
    i = ++top_of_p_table + 1;

    RECREATE(player_table, struct player_index_element, i);
    pos = top_of_p_table;
  }

  CREATE(player_table[pos].name, char, strlen(name) + 1);
  player_table[pos].id = 0;

  /* copy lowercase equivalent of name to table field */
  for (i = 0; (player_table[pos].name[i] = LOWER(name[i])); i++)
	/* Nothing */;

  return (pos);
}



/************************************************************************
*  funcs of a (more or less) general utility nature			*
************************************************************************/


/* read and allocate space for a '~'-terminated string from a given file */
char *fread_string(FILE *fl, const char *error)
{
  char buf[MAX_STRING_LENGTH], tmp[513];
  char *point;
  int done = 0, length = 0, templength;

  *buf = '\0';

  do {
    if (!fgets(tmp, 512, fl)) {
      log("SYSERR: fread_string: format error at or near %s", error);
      exit(1);
    }
    /* If there is a '~', end the string; else put an "\r\n" over the '\n'. */
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
      log("SYSERR: fread_string: string too large (db.c)");
      log("%s", error);
      exit(1);
    } else {
      strcat(buf + length, tmp);	/* strcat: OK (size checked above) */
      length += templength;
    }
  } while (!done);

  /* allocate space for the new string and copy it */
  return (strlen(buf) ? strdup(buf) : NULL);
}


/* release memory allocated for a char struct */
void free_char(struct char_data *ch)
{
  int i;
  struct alias_data *a;

  if (ch->player_specials != NULL && ch->player_specials != &dummy_mob) {
    while ((a = GET_ALIASES(ch)) != NULL) {
      GET_ALIASES(ch) = (GET_ALIASES(ch))->next;
      free_alias(a);
    }
    if (ch->player_specials->poofin)
      free(ch->player_specials->poofin);
    if (ch->player_specials->poofout)
      free(ch->player_specials->poofout);
    free(ch->player_specials);
    if (IS_NPC(ch))
      log("SYSERR: Mob %s (#%d) had player_specials allocated!", GET_NAME(ch), GET_MOB_VNUM(ch));
  }
  if (!IS_NPC(ch) || (IS_NPC(ch) && GET_MOB_RNUM(ch) == NOBODY)) {
    /* if this is a player, or a non-prototyped non-player, free all */
    if (GET_NAME(ch))
      free(GET_NAME(ch));
    if (ch->player.title)
      free(ch->player.title);
    if (ch->player.short_descr)
      free(ch->player.short_descr);
    if (ch->player.long_descr)
      free(ch->player.long_descr);
    if (ch->player.description)
      free(ch->player.description);
  } else if ((i = GET_MOB_RNUM(ch)) != NOBODY) {
    /* otherwise, free strings only if the string is not pointing at proto */
    if (ch->player.name && ch->player.name != mob_proto[i].player.name)
      free(ch->player.name);
    if (ch->player.title && ch->player.title != mob_proto[i].player.title)
      free(ch->player.title);
    if (ch->player.short_descr && ch->player.short_descr != mob_proto[i].player.short_descr)
      free(ch->player.short_descr);
    if (ch->player.long_descr && ch->player.long_descr != mob_proto[i].player.long_descr)
      free(ch->player.long_descr);
    if (ch->player.description && ch->player.description != mob_proto[i].player.description)
      free(ch->player.description);
  }
  while (ch->affected)
    affect_remove(ch, ch->affected);

  if (ch->desc)
    ch->desc->character = NULL;

  free(ch);
}




/* release memory allocated for an obj struct */
void free_obj(struct obj_data *obj)
{
  int nr;

  if ((nr = GET_OBJ_RNUM(obj)) == NOTHING) {
    if (obj->name)
      free(obj->name);
    if (obj->description)
      free(obj->description);
    if (obj->short_description)
      free(obj->short_description);
    if (obj->action_description)
      free(obj->action_description);
    if (obj->ex_description)
      free_extra_descriptions(obj->ex_description);
  } else {
    if (obj->name && obj->name != obj_proto[nr].name)
      free(obj->name);
    if (obj->description && obj->description != obj_proto[nr].description)
      free(obj->description);
    if (obj->short_description && obj->short_description != obj_proto[nr].short_description)
      free(obj->short_description);
    if (obj->action_description && obj->action_description != obj_proto[nr].action_description)
      free(obj->action_description);
    if (obj->ex_description && obj->ex_description != obj_proto[nr].ex_description)
      free_extra_descriptions(obj->ex_description);
  }

  free(obj);
}


/*
 * Steps:
 *   1: Read contents of a text file.
 *   2: Make sure no one is using the pointer in paging.
 *   3: Allocate space.
 *   4: Point 'buf' to it.
 *
 * We don't want to free() the string that someone may be
 * viewing in the pager.  page_string() keeps the internal
 * strdup()'d copy on ->showstr_head and it won't care
 * if we delete the original.  Otherwise, strings are kept
 * on ->showstr_vector but we'll only match if the pointer
 * is to the string we're interested in and not a copy.
 *
 * If someone is reading a global copy we're trying to
 * replace, give everybody using it a different copy so
 * as to avoid special cases.
 */
int file_to_string_alloc(const char *name, char **buf)
{
  int temppage;
  char temp[MAX_STRING_LENGTH];
  struct descriptor_data *in_use;

  for (in_use = descriptor_list; in_use; in_use = in_use->next)
    if (in_use->showstr_vector && *in_use->showstr_vector == *buf)
      return (-1);

  /* Lets not free() what used to be there unless we succeeded. */
  if (file_to_string(name, temp) < 0)
    return (-1);

  for (in_use = descriptor_list; in_use; in_use = in_use->next) {
    if (!in_use->showstr_count || *in_use->showstr_vector != *buf)
      continue;

    /* Let's be nice and leave them at the page they were on. */
    temppage = in_use->showstr_page;
    paginate_string((in_use->showstr_head = strdup(*in_use->showstr_vector)), in_use);
    in_use->showstr_page = temppage;
  }

  if (*buf)
    free(*buf);

  *buf = strdup(temp);
  return (0);
}


/* read contents of a text file, and place in buf */
int file_to_string(const char *name, char *buf)
{
  FILE *fl;
  char tmp[READ_SIZE + 3];
  int len;

  *buf = '\0';

  if (!(fl = fopen(name, "r"))) {
    log("SYSERR: reading %s: %s", name, strerror(errno));
    return (-1);
  }

  for (;;) {
    if (!fgets(tmp, READ_SIZE, fl))	/* EOF check */
      break;
    if ((len = strlen(tmp)) > 0)
      tmp[len - 1] = '\0'; /* take off the trailing \n */
    strcat(tmp, "\r\n");	/* strcat: OK (tmp:READ_SIZE+3) */

    if (strlen(buf) + strlen(tmp) + 1 > MAX_STRING_LENGTH) {
      log("SYSERR: %s: string too big (%d max)", name, MAX_STRING_LENGTH);
      *buf = '\0';
      fclose(fl);
      return (-1);
    }
    strcat(buf, tmp);	/* strcat: OK (size checked above) */
  }

  fclose(fl);

  return (0);
}



/* clear some of the the working variables of a char */
void reset_char(struct char_data *ch)
{
  int i;

  for (i = 0; i < NUM_WEARS; i++)
    GET_EQ(ch, i) = NULL;

  ch->followers = NULL;
  ch->master = NULL;
  IN_ROOM(ch) = NOWHERE;
  ch->carrying = NULL;
  ch->next = NULL;
  ch->next_fighting = NULL;
  ch->next_in_room = NULL;
  FIGHTING(ch) = NULL;
  ch->char_specials.position = POS_STANDING;
  ch->mob_specials.default_pos = POS_STANDING;
  ch->char_specials.carry_weight = 0;
  ch->char_specials.carry_items = 0;

  if (GET_HIT(ch) <= 0)
    GET_HIT(ch) = 1;
  if (GET_MOVE(ch) <= 0)
    GET_MOVE(ch) = 1;
  if (GET_MANA(ch) <= 0)
    GET_MANA(ch) = 1;

  GET_LAST_TELL(ch) = NOBODY;
}



/* clear ALL the working variables of a char; do NOT free any space alloc'ed */
void clear_char(struct char_data *ch)
{
  memset((char *) ch, 0, sizeof(struct char_data));

  IN_ROOM(ch) = NOWHERE;
  GET_PFILEPOS(ch) = -1;
  GET_MOB_RNUM(ch) = NOBODY;
  GET_WAS_IN(ch) = NOWHERE;
  GET_POS(ch) = POS_STANDING;
  ch->mob_specials.default_pos = POS_STANDING;

  GET_AC(ch) = 100;		/* Basic Armor */
  if (ch->points.max_mana < 100)
    ch->points.max_mana = 100;
}


void clear_object(struct obj_data *obj)
{
  memset((char *) obj, 0, sizeof(struct obj_data));

  obj->item_number = NOTHING;
  IN_ROOM(obj) = NOWHERE;
  obj->worn_on = NOWHERE;
}




/*
 * Called during character creation after picking character class
 * (and then never again for that character).
 */
void init_char(struct char_data *ch)
{
  int i;

  /* create a player_special structure */
  if (ch->player_specials == NULL)
    CREATE(ch->player_specials, struct player_special_data, 1);

  /* *** if this is our first player --- he be God *** */
  if (top_of_p_table == 0) {
    GET_LEVEL(ch) = LVL_IMPL;
    GET_EXP(ch) = 7000000;

    /* The implementor never goes through do_start(). */
    GET_MAX_HIT(ch) = 500;
    GET_MAX_MANA(ch) = 100;
    GET_MAX_MOVE(ch) = 82;
    GET_HIT(ch) = GET_MAX_HIT(ch);
    GET_MANA(ch) = GET_MAX_MANA(ch);
    GET_MOVE(ch) = GET_MAX_MOVE(ch);
  }

  set_title(ch, NULL);
  ch->player.short_descr = NULL;
  ch->player.long_descr = NULL;
  ch->player.description = NULL;

  ch->player.time.birth = time(0);
  ch->player.time.logon = time(0);
  ch->player.time.played = 0;

  GET_HOME(ch) = 1;
  GET_AC(ch) = 100;

  for (i = 0; i < MAX_TONGUE; i++)
    GET_TALK(ch, i) = 0;

  /*
   * make favors for sex -- or in English, we bias the height and weight of the
   * character depending on what gender they've chosen for themselves. While it
   * is possible to have a tall, heavy female it's not as likely as a male.
   *
   * Height is in centimeters. Weight is in pounds.  The only place they're
   * ever printed (in stock code) is SPELL_IDENTIFY.
   */
  if (GET_SEX(ch) == SEX_MALE) {
    GET_WEIGHT(ch) = rand_number(120, 180);
    GET_HEIGHT(ch) = rand_number(160, 200); /* 5'4" - 6'8" */
  } else {
    GET_WEIGHT(ch) = rand_number(100, 160);
    GET_HEIGHT(ch) = rand_number(150, 180); /* 5'0" - 6'0" */
  }

  if ((i = get_ptable_by_name(GET_NAME(ch))) != -1)
    player_table[i].id = GET_IDNUM(ch) = ++top_idnum;
  else
    log("SYSERR: init_char: Character '%s' not found in player table.", GET_NAME(ch));

  for (i = 1; i <= MAX_SKILLS; i++) {
    if (GET_LEVEL(ch) < LVL_IMPL)
      SET_SKILL(ch, i, 0);
    else
      SET_SKILL(ch, i, 100);
  }

  AFF_FLAGS(ch) = 0;

  for (i = 0; i < 5; i++)
    GET_SAVE(ch, i) = 0;

  ch->real_abils.intel = 25;
  ch->real_abils.wis = 25;
  ch->real_abils.dex = 25;
  ch->real_abils.str = 25;
  ch->real_abils.str_add = 100;
  ch->real_abils.con = 25;
  ch->real_abils.cha = 25;

  for (i = 0; i < 3; i++)
    GET_COND(ch, i) = (GET_LEVEL(ch) == LVL_IMPL ? -1 : 24);

  GET_LOADROOM(ch) = NOWHERE;
}



/* returns the real number of the room with given virtual number */
room_rnum real_room(room_vnum vnum)
{
  room_rnum bot, top, mid;

  bot = 0;
  top = top_of_world;

  /* perform binary search on world-table */
  for (;;) {
    mid = (bot + top) / 2;

    if ((world + mid)->number == vnum)
      return (mid);
    if (bot >= top)
      return (NOWHERE);
    if ((world + mid)->number > vnum)
      top = mid - 1;
    else
      bot = mid + 1;
  }
}



/* returns the real number of the monster with given virtual number */
mob_rnum real_mobile(mob_vnum vnum)
{
  mob_rnum bot, top, mid;

  bot = 0;
  top = top_of_mobt;

  /* perform binary search on mob-table */
  for (;;) {
    mid = (bot + top) / 2;

    if ((mob_index + mid)->vnum == vnum)
      return (mid);
    if (bot >= top)
      return (NOBODY);
    if ((mob_index + mid)->vnum > vnum)
      top = mid - 1;
    else
      bot = mid + 1;
  }
}


/* returns the real number of the object with given virtual number */
obj_rnum real_object(obj_vnum vnum)
{
  obj_rnum bot, top, mid;

  bot = 0;
  top = top_of_objt;

  /* perform binary search on obj-table */
  for (;;) {
    mid = (bot + top) / 2;

    if ((obj_index + mid)->vnum == vnum)
      return (mid);
    if (bot >= top)
      return (NOTHING);
    if ((obj_index + mid)->vnum > vnum)
      top = mid - 1;
    else
      bot = mid + 1;
  }
}


/* returns the real number of the zone with given virtual number */
room_rnum real_zone(room_vnum vnum)
{
  room_rnum bot, top, mid;

  bot = 0;
  top = top_of_zone_table;

  /* perform binary search on zone-table */
  for (;;) {
    mid = (bot + top) / 2;

    if ((zone_table + mid)->number == vnum)
      return (mid);
    if (bot >= top)
      return (NOWHERE);
    if ((zone_table + mid)->number > vnum)
      top = mid - 1;
    else
      bot = mid + 1;
  }
}


/*
 * Extend later to include more checks.
 *
 * TODO: Add checks for unknown bitvectors.
 */
int check_object(struct obj_data *obj)
{
  char objname[MAX_INPUT_LENGTH + 32];
  int error = FALSE;

  if (GET_OBJ_WEIGHT(obj) < 0 && (error = TRUE))
    log("SYSERR: Object #%d (%s) has negative weight (%d).",
	GET_OBJ_VNUM(obj), obj->short_description, GET_OBJ_WEIGHT(obj));

  if (GET_OBJ_RENT(obj) < 0 && (error = TRUE))
    log("SYSERR: Object #%d (%s) has negative cost/day (%d).",
	GET_OBJ_VNUM(obj), obj->short_description, GET_OBJ_RENT(obj));

  snprintf(objname, sizeof(objname), "Object #%d (%s)", GET_OBJ_VNUM(obj), obj->short_description);
  error |= check_bitvector_names(GET_OBJ_WEAR(obj), wear_bits_count, objname, "object wear");
  error |= check_bitvector_names(GET_OBJ_EXTRA(obj), extra_bits_count, objname, "object extra");
  error |= check_bitvector_names(GET_OBJ_AFFECT(obj), affected_bits_count, objname, "object affect");

  switch (GET_OBJ_TYPE(obj)) {
  case ITEM_DRINKCON:
  {
    char onealias[MAX_INPUT_LENGTH], *space = strrchr(obj->name, ' ');

    strlcpy(onealias, space ? space + 1 : obj->name, sizeof(onealias));
    if (search_block(onealias, drinknames, TRUE) < 0 && (error = TRUE))
      log("SYSERR: Object #%d (%s) doesn't have drink type as last alias. (%s)",
		GET_OBJ_VNUM(obj), obj->short_description, obj->name);
  }
  /* Fall through. */
  case ITEM_FOUNTAIN:
    if (GET_OBJ_VAL(obj, 1) > GET_OBJ_VAL(obj, 0) && (error = TRUE))
      log("SYSERR: Object #%d (%s) contains (%d) more than maximum (%d).",
		GET_OBJ_VNUM(obj), obj->short_description,
		GET_OBJ_VAL(obj, 1), GET_OBJ_VAL(obj, 0));
    break;
  case ITEM_SCROLL:
  case ITEM_POTION:
    error |= check_object_level(obj, 0);
    error |= check_object_spell_number(obj, 1);
    error |= check_object_spell_number(obj, 2);
    error |= check_object_spell_number(obj, 3);
    break;
  case ITEM_WAND:
  case ITEM_STAFF:
    error |= check_object_level(obj, 0);
    error |= check_object_spell_number(obj, 3);
    if (GET_OBJ_VAL(obj, 2) > GET_OBJ_VAL(obj, 1) && (error = TRUE))
      log("SYSERR: Object #%d (%s) has more charges (%d) than maximum (%d).",
		GET_OBJ_VNUM(obj), obj->short_description,
		GET_OBJ_VAL(obj, 2), GET_OBJ_VAL(obj, 1));
    break;
 }

  return (error);
}

int check_object_spell_number(struct obj_data *obj, int val)
{
  int error = FALSE;
  const char *spellname;

  if (GET_OBJ_VAL(obj, val) == -1)	/* i.e.: no spell */
    return (error);

  /*
   * Check for negative spells, spells beyond the top define, and any
   * spell which is actually a skill.
   */
  if (GET_OBJ_VAL(obj, val) < 0)
    error = TRUE;
  if (GET_OBJ_VAL(obj, val) > TOP_SPELL_DEFINE)
    error = TRUE;
  if (GET_OBJ_VAL(obj, val) > MAX_SPELLS && GET_OBJ_VAL(obj, val) <= MAX_SKILLS)
    error = TRUE;
  if (error)
    log("SYSERR: Object #%d (%s) has out of range spell #%d.",
	GET_OBJ_VNUM(obj), obj->short_description, GET_OBJ_VAL(obj, val));

  /*
   * This bug has been fixed, but if you don't like the special behavior...
   */
#if 0
  if (GET_OBJ_TYPE(obj) == ITEM_STAFF &&
	HAS_SPELL_ROUTINE(GET_OBJ_VAL(obj, val), MAG_AREAS | MAG_MASSES))
    log("... '%s' (#%d) uses %s spell '%s'.",
	obj->short_description,	GET_OBJ_VNUM(obj),
	HAS_SPELL_ROUTINE(GET_OBJ_VAL(obj, val), MAG_AREAS) ? "area" : "mass",
	skill_name(GET_OBJ_VAL(obj, val)));
#endif

  if (scheck)		/* Spell names don't exist in syntax check mode. */
    return (error);

  /* Now check for unnamed spells. */
  spellname = skill_name(GET_OBJ_VAL(obj, val));

  if ((spellname == unused_spellname || !str_cmp("UNDEFINED", spellname)) && (error = TRUE))
    log("SYSERR: Object #%d (%s) uses '%s' spell #%d.",
		GET_OBJ_VNUM(obj), obj->short_description, spellname,
		GET_OBJ_VAL(obj, val));

  return (error);
}

int check_object_level(struct obj_data *obj, int val)
{
  int error = FALSE;

  if ((GET_OBJ_VAL(obj, val) < 0 || GET_OBJ_VAL(obj, val) > LVL_IMPL) && (error = TRUE))
    log("SYSERR: Object #%d (%s) has out of range level #%d.",
	GET_OBJ_VNUM(obj), obj->short_description, GET_OBJ_VAL(obj, val));

  return (error);
}

int check_bitvector_names(bitvector_t bits, size_t namecount, const char *whatami, const char *whatbits)
{
  unsigned int flagnum;
  bool error = FALSE;

  /* See if any bits are set above the ones we know about. */
  if (bits <= (~(bitvector_t)0 >> (sizeof(bitvector_t) * 8 - namecount)))
    return (FALSE);

  for (flagnum = namecount; flagnum < sizeof(bitvector_t) * 8; flagnum++)
    if ((1 << flagnum) & bits) {
      log("SYSERR: %s has unknown %s flag, bit %d (0 through %zu known).", whatami, whatbits, flagnum, namecount - 1);
      error = TRUE;
    }

  return (error);
}
