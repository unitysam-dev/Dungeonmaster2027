/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "champion.h"
#include "dungeon.h"
#include "event.h"
#include "game.h"
#include "gameloop.h"
#include "group.h"
#include "inventory.h"
#include "map.h"
#include "move.h"
#include "party.h"
#include "party_context.h"
#include "record.h"
#include "recruit.h"
#include "command.h"
#include "control_internal.h"
#include "state_json.h"
#include "request.h"
#include "json.h"
#include "png.h"
#include "ui.h"
#include "view.h"
#include "menus.h"
#include "status.h"
#include "video.h"
#include "random.h"
#include "os.h"
#include "action.h"
#include "discard.h"
#include "timeline.h"
#include "sound.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
typedef struct {unsigned id,time; int sound,map,x,y;} SoundEvent;
static SoundEvent journal[256];static unsigned sound_id;
static void world_sound(int16_t sound,int16_t map,Location at){unsigned id=++sound_id;journal[(id-1)%256]=(SoundEvent){id,g_game_time,sound,map,at.x,at.y};}
extern unsigned party_art_serial[2][4],party_art_tick[2][4];
extern int party_art_action[2][4];
static int art_portraits[2][4];
static void select_party(int p);
static int16_t homes[2][2];
#include "level.generated.h"
static int levelmap;
/* Retain the stock ornament lists without shifting or overwriting Dungeon.DAT fields. */
static uint8_t menagerie_types[CREATURE_TYPE_COUNT + 3 * 15];
static void set_roster(void){
 const uint8_t *ornaments=rec_map_type_lists(levelmap)+rec_map_creature_types(levelmap);
 int n=0;
 for(int i=0;i<COOP_ENCOUNTER_COUNT;i++){
  uint8_t t=coop_encounters[i][2];
  if(!memchr(menagerie_types,t,n))menagerie_types[n++]=t;
 }
 memcpy(menagerie_types+n,ornaments,rec_map_wall_ornaments(levelmap)+rec_map_floor_ornaments(levelmap)+rec_map_door_ornaments(levelmap));
 rec_set_runtime_map_types(levelmap,menagerie_types,n);
}
static bool seals[2],finished[2];
/* Authoritative bridge latch controller: native movement -> occupancy -> native door square.
   Party-specific seals deliberately require two owners; no client objective setter. */
static void coop_update(void){
 int saved=g_active_party;
 for(int p=0;p<2;p++) { select_party(p); if(!g_party_connected[p]||g_session.party_dead)continue;
  if(g_party_map_x==coop_activators[p][0]&&g_party_map_y==coop_activators[p][1])seals[p]=true;
 }
 select_party(saved);
 uint8_t *sq=&g_dungeon_map_data[levelmap][coop_door[0]][coop_door[1]];
 *sq=(*sq&~DOOR_STATE_MASK)|((seals[0]&&seals[1])?DOOR_OPEN:DOOR_CLOSED);
 if(seals[0]&&seals[1])for(int p=0;p<2;p++){select_party(p);if(g_party_connected[p]&&!g_session.party_dead&&g_party_map_x==coop_exits[p][0]&&g_party_map_y==coop_exits[p][1])finished[p]=true;}
 select_party(saved);
}
static bool coop_import(void){
 if(COOP_W>g_current_map_width||COOP_H>g_current_map_height)return false;
 /* Detach every original thing BEFORE clearing square flags; no original sensors/groups remain. */
 group_remove_all_active_groups();
 uint16_t reusable_door=THING_NONE, spare_doors[64], spare_items[16][128]; int spare_count=0, item_count[16]={0};
 for(int y=0;y<g_current_map_height;y++)for(int x=0;x<g_current_map_width;x++){
  uint16_t t;while((t=map_get_square_first_thing(x,y))!=THING_ENDOFLIST){if(THING_TYPE(t)==THING_DOOR){if(reusable_door!=THING_NONE)spare_doors[spare_count++]=reusable_door;reusable_door=t;}if(THING_TYPE(t)>=5&&THING_TYPE(t)<=10&&item_count[THING_TYPE(t)]<128)spare_items[THING_TYPE(t)][item_count[THING_TYPE(t)]++]=t;map_unlink_thing_from_list(t,0,x,y);}
  g_dungeon_map_data[levelmap][x][y]=0;
 }
 /* Original floor events would target old geometry: remove all map-scoped pending events. */
 for(int i=0;i<g_timeline.high_water;i++)if(g_timeline.events[i].type && EVENT_MAP(&g_timeline.events[i])==levelmap)timeline_delete_event(i);
 for(int y=0;y<COOP_H;y++)for(int x=0;x<COOP_W;x++)g_dungeon_map_data[levelmap][x][y]=coop_rows[y][x]=='#'?0:SQUARE_CORRIDOR<<5;
 uint16_t d=reusable_door;if(d==THING_NONE)return false;
 rec_thing_clear(d); /* record flags zero: no button, no force/magic break shortcut */
 g_dungeon_map_data[levelmap][coop_door[0]][coop_door[1]]=(SQUARE_DOOR<<5)|DOOR_NORTH_SOUTH|DOOR_CLOSED;
 map_link_thing_to_list(d,0,coop_door[0],coop_door[1]);
 for(int i=0;i<COOP_BUTTONDOORS_COUNT;i++){
  uint16_t b=spare_count?spare_doors[--spare_count]:rec_find_unused_thing(THING_DOOR);if(b==THING_NONE)return false;
  rec_thing_clear(b);rec_write(b)[3]=0x40;
  int x=coop_buttonDoors[i][0],y=coop_buttonDoors[i][1];
  g_dungeon_map_data[levelmap][x][y]=(SQUARE_DOOR<<5)|DOOR_NORTH_SOUTH|DOOR_CLOSED;map_link_thing_to_list(b,0,x,y);
 }
 for(int i=0;i<COOP_SECRETWALLS_COUNT;i++)g_dungeon_map_data[levelmap][coop_secretWalls[i][0]][coop_secretWalls[i][1]]= (SQUARE_FAKE_WALL<<5)|FAKE_WALL_IMAGINARY;
 for(int i=0;i<COOP_SUPPLIES_COUNT;i++){
  const int *e=coop_supplies[i];uint16_t t=item_count[e[2]]?spare_items[e[2]][--item_count[e[2]]]:rec_find_unused_thing(e[2]);if(t==THING_NONE){fprintf(stderr,"SUPPLY POOL %d\n",i);return false;}
  rec_thing_clear(t);
  if(e[2]==THING_POTION){rec_potion_set_type(t,e[3]);rec_potion_set_power(t,e[4]);}
  else{rec_object_set_type(t,e[3]);if(e[2]==THING_WEAPON)rec_weapon_set_charges(t,e[4]);if(e[2]==THING_JUNK)rec_junk_set_charges(t,e[4]);}
  map_link_thing_to_list(t,0,e[0],e[1]);
 }
 /* Only unused native records; never scavenge detached original groups. */
 for(int i=0;i<COOP_ENCOUNTER_COUNT;i++){
  const int *e=coop_encounters[i];uint16_t g=rec_find_unused_thing(THING_GROUP);if(g==THING_NONE){fprintf(stderr,"GROUP POOL %d\n",i);return false;}
  rec_group_set_possessions(g,THING_ENDOFLIST);rec_group_set_creature_type(g,e[2]);
  if(!group_is_allowed_on_map(g,levelmap)){fprintf(stderr,"TYPE NOT ALLOWED %d\n",e[2]);return false;}
  rec_group_set_last_creature(g,e[3]-1);rec_group_set_direction(g,e[5]);
  rec_group_set_cells(g,e[3]==1?CELL_CENTERED:4);rec_group_set_behavior(g,BEHAVIOR_WANDER);
  for(int c=0;c<e[3];c++)rec_group_set_health(g,c,e[4]);
  map_link_thing_to_list(g,0,e[0],e[1]);group_add_active_group(g,e[0],e[1]);group_start_wandering(e[0],e[1]);
 }
 for(int p=0;p<2;p++){homes[p][0]=coop_starts[p][0];homes[p][1]=coop_starts[p][1];}
 return true;
}
static int16_t acting[2]={-1,-1}, inv[2]={-1,-1};
static ActionList actions[2];
static int16_t selected_icon[2]={-1,-1};
static bool held_eye[2];
static int eye_box[4]={320,200,-1,-1};
static void get_eye_box(void){if(eye_box[2]>=0)return;for(int y=0;y<200;y++)for(int x=0;x<320;x++)if(ui_command_from_mouse(TABLES_MOUSE_INVENTORY,x,y,UI_BUTTON_LEFT)==COMMAND_CLICK_EYE){if(x<eye_box[0])eye_box[0]=x;if(y<eye_box[1])eye_box[1]=y;if(x>eye_box[2])eye_box[2]=x;if(y>eye_box[3])eye_box[3]=y;}}
static bool mk(const char *p){struct stat st;return mkdir(p,0755)==0||stat(p,&st)==0;}
static void ls(const char *p,void(*f)(const char*,void*),void*d){(void)p;(void)f;(void)d;}
static void select_party(int p){
 held_eye[g_active_party]=command_eye_is_held();
 selected_icon[g_active_party]=command_get_dragged_icon();
 acting[g_active_party]=g_session.acting_champion_index;actions[g_active_party]=g_action_list;
 inv[g_active_party]=g_session.inventory_champion_index;
 /* Open containers cannot be shared between UI contexts: commit before switching. */
 inventory_close_chest();
 party_context_activate((int16_t)p);g_session.acting_champion_index=acting[p];g_action_list=actions[p];
 command_reset();command_set_dragged_icon(selected_icon[p]);g_session.inventory_champion_index=-1;
 if(inv[p]>=0) command_toggle_inventory(inv[p]);
 command_eye_restore(held_eye[p]&&inv[p]>=0);
 g_session.new_party_map=-1;map_set_current_map(levelmap);
}
static void redraw(void){
 uint32_t seed=random_get_seed();
 memset(g_screen.px,0,sizeof g_screen.px);ui_draw_interface();
 if(g_session.inventory_champion_index<0) gameloop_draw_view();
 command_eye_redraw();
 menus_draw_action_area();
 for(int c=0;c<g_party_champion_count;c++)if(g_champions[c].runtime.recent_damage>0&&g_game_time-g_champions[c].runtime.recent_damage_tick<=16)status_draw_damage(c,g_champions[c].runtime.recent_damage);
 int16_t damage;if(g_session.acting_champion_index<0&&action_recent_melee(&damage))menus_draw_action_damage(damage);
 random_set_seed(seed);
}
static bool free_square(int x,int y){
 if(x<0||y<0||x>=g_current_map_width||y>=g_current_map_height)return false;
 if(rec_square_type(map_get_square(x,y))!=SQUARE_CORRIDOR)return false;
 if(map_find_thing((Location){x,y},THING_GROUP)!=THING_ENDOFLIST)return false;
 if(map_find_thing((Location){x,y},THING_SENSOR)!=THING_ENDOFLIST)return false;
 return !party_context_any_on(x,y);
}
static void connection(int p,bool on){
 select_party(p);
 if(g_party_connected[p]==on)return;
 if(!on){
  held_eye[p]=false;command_eye_restore(false);
  move_get_move_result(&(Move){.thing=THING_NONE,.from={g_party_map_x,g_party_map_y},.leaves=true},NULL);
  g_party_connected[p]=false;
 }else{
  int x=g_party_map_x,y=g_party_map_y;
  if(!free_square(x,y)){
   bool found=false;
   for(int radius=0;radius<64&&!found;radius++)for(int xx=0;xx<g_current_map_width&&!found;xx++)for(int yy=0;yy<g_current_map_height&&!found;yy++)
    if(abs(xx-x)+abs(yy-y)==radius&&free_square(xx,yy)){x=xx;y=yy;found=true;}
  }
  g_party_map_x=(int16_t)x;g_party_map_y=(int16_t)y;g_party_connected[p]=true;
  move_get_move_result(&(Move){.thing=THING_NONE,.to={x,y},.enters=true},NULL);
 }
}
/* Explicit test fixtures only: never scavenge existing dungeon records. */
static bool test_item(int c,int slot,int kind,int type,int charges){
 if(slot<0){for(slot=SLOT_BACKPACK_FIRST;slot<SLOT_COUNT;slot++)if(g_champions[c].slots[slot]==THING_NONE)break;if(slot==SLOT_COUNT)return false;}
 if(g_champions[c].slots[slot]!=THING_NONE){int spare;for(spare=SLOT_BACKPACK_FIRST;spare<SLOT_COUNT;spare++)if(g_champions[c].slots[spare]==THING_NONE)break;if(spare==SLOT_COUNT)return false;uint16_t old=inventory_take_slot_object(c,slot);inventory_add_object_in_slot(c,old,spare);}
 uint16_t t=rec_find_unused_thing(kind);if(t==THING_NONE){fprintf(stderr,"NO RECORD p%d c%d kind%d type%d slot%d\n",g_active_party,c,kind,type,slot);return false;}
 if(kind==THING_POTION){rec_potion_set_type(t,type);rec_potion_set_power(t,charges);}
 else {rec_object_set_type(t,type);if(kind==THING_WEAPON)rec_weapon_set_charges(t,charges);if(kind==THING_JUNK)rec_junk_set_charges(t,charges);}
 const ObjectInfo*info=tables_get_object_info(inventory_get_object_info_index(t));
 if(!info||!(info->allowed_slots&tables_get_slot_mask(slot))){fprintf(stderr,"BAD SLOT p%d c%d kind%d type%d slot%d mask%u\n",g_active_party,c,kind,type,slot,info?info->allowed_slots:0);return false;}
 inventory_add_object_in_slot(c,t,slot);return true;
}
static bool test_kit(void){
 const int action[4]={10,18,25,34}; /* original SWORD, AXE, BOW, STAFF */
 for(int c=0;c<4;c++){
  if(!test_item(c,SLOT_HAND_ACTION,THING_WEAPON,action[c],0))return false;
  if(c==1&&!test_item(c,SLOT_HAND_READY,THING_ARMOUR,30,0))return false;
  if(c==2&&!test_item(c,SLOT_HAND_READY,THING_WEAPON,WEAPON_ARROW,0))return false;
  const int slots[4]={SLOT_HEAD,SLOT_TORSO,SLOT_LEGS,SLOT_FEET},armour[4]={26,16,17,4};
  for(int i=0;i<4;i++)if(g_champions[c].slots[slots[i]]==THING_NONE&&!test_item(c,slots[i],THING_ARMOUR,armour[i],0))return false;
  if(!test_item(c,-1,THING_JUNK,JUNK_WATERSKIN,3)||!test_item(c,-1,THING_JUNK,29,0)||!test_item(c,-1,THING_JUNK,31,0))return false;
  if(c==3&&(!test_item(c,-1,THING_POTION,POTION_VI,120)||!test_item(c,-1,THING_POTION,POTION_MON,120)||!test_item(c,-1,THING_POTION,POTION_EMPTY_FLASK,0)))return false;
  if(c==2){for(int i=0;i<4;i++)if(!test_item(c,(int[]){SLOT_QUIVER_LINE1_1,SLOT_QUIVER_LINE1_2,SLOT_QUIVER_LINE2_1,SLOT_QUIVER_LINE2_2}[i],THING_WEAPON,WEAPON_ARROW,0))return false;}
  if(c==3&&(!test_item(c,-1,THING_WEAPON,WEAPON_DAGGER,0)||!test_item(c,-1,THING_WEAPON,WEAPON_THROWING_STAR,0)||!test_item(c,-1,THING_JUNK,45,0)||!test_item(c,-1,THING_JUNK,0,0)))return false;
 }
 return true;
}
static bool init(const char *dir){
 rec_set_runtime_map_types(-1,NULL,0);
 g_os_make_folder_hook=mk;g_os_list_folder_hook=ls;
 if(!gameloop_load_data(dir)||!gameloop_start_new_game(dir))return false;
 party_context_init();party_context_set_live_count(2);
 int16_t mx[8],my[8],portrait[8];int found=0;
 for(int16_t m=0;m<dungeon_map_count()&&found<8;m++){
  map_set_current_map(m);
  for(int16_t x=0;x<g_current_map_width&&found<8;x++)for(int16_t y=0;y<g_current_map_height&&found<8;y++){
   uint16_t t=recruit_text_thing(m,x,y);RecruitData rd;
   if(t==THING_NONE||!recruit_parse(t,&rd))continue;
   mx[found]=x;my[found]=y;portrait[found]=-1;
   /* Champion text is under the party; its mirror sensor is on the adjacent wall. */
   const int dx[4]={0,1,0,-1},dy[4]={-1,0,1,0};
   for(int d=0;d<4;d++)for(t=map_get_first_thing_at((Position){m,x+dx[d],y+dy[d]});t!=THING_ENDOFLIST;t=rec_thing_next(t))
    if(THING_TYPE(t)==THING_SENSOR&&THING_CELL(t)==((d+2)&3)){int16_t i=recruit_portrait_index(t);if(i>=0)portrait[found]=i;}
   if(portrait[found]<0)continue;
   fprintf(stderr,"RECRUIT %s map%d at%d,%d portrait%d\n",rd.name,m,x,y,portrait[found]);
   found++;
  }
 }
 if(found<8)return false;
 for(int p=0;p<2;p++){
  party_context_activate(p);g_party_map_index=0;
  for(int c=0;c<4;c++){int i=p*4+c;art_portraits[p][c]=portrait[i];g_party_map_x=mx[i];g_party_map_y=my[i];champion_add_candidate_to_party(portrait[i]);g_session.candidate_champion_index=-1;}
  /* Test fixture: real original rank progression, one threshold at a time.
     Craftsman is original rank 6 = 8000 base experience, confirmed by loaded rank table. */
  for(int c=0;c<4;c++){
   uint32_t saved=random_get_seed();random_set_seed(0x43524146u+(unsigned)c);
   for(int skill=0;skill<SKILL_BASE_COUNT;skill++){
    while(champion_get_skill_level(c,skill,true,true)<6){
     int level=champion_get_skill_level(c,skill,true,true);
     unsigned threshold=500u<<(level-1);
     unsigned current=g_champions[c].skills[skill].experience;
     champion_add_skill_experience(c,skill,(uint16_t)(threshold-current));
    }
   }
   for(int skill=0;skill<SKILL_COUNT;skill++){
    if(skill>=SKILL_BASE_COUNT)g_champions[c].skills[skill].experience=8000;
    g_champions[c].skills[skill].temporary_experience=0;
   }
   Champion *ch=&g_champions[c];ch->current_health=ch->maximum_health;
   ch->current_stamina=ch->maximum_stamina;ch->current_mana=ch->maximum_mana;
   for(int stat=0;stat<STAT_COUNT;stat++)ch->statistics[stat][STAT_CURRENT]=ch->statistics[stat][STAT_MAXIMUM];
   random_set_seed(saved);
  }
  champion_set_leader(0);g_magic_caster_champion_index=0;g_session.inventory_champion_index=-1;
  uint16_t torch=discard_get_unused_thing(THING_WEAPON);rec_object_set_type(torch,WEAPON_TORCH);rec_weapon_set_charges(torch,15);inventory_add_object_in_slot(0,torch,SLOT_HAND_READY);
  uint16_t rock=discard_get_unused_thing(THING_WEAPON);rec_object_set_type(rock,WEAPON_ROCK);inventory_add_object_in_slot(0,rock,SLOT_HAND_ACTION);
  if(!test_kit()){fprintf(stderr,"Test kit allocation/slot failure\n");return false;}
  for(int c=0;c<4;c++){g_champions[c].current_health=g_champions[c].maximum_health;g_champions[c].current_stamina=g_champions[c].maximum_stamina;g_champions[c].current_mana=g_champions[c].maximum_mana;}
 }
 party_context_activate(0);group_remove_all_active_groups();
 /* Original descriptors number depth zero at the Hall of Champions. Fourth physical floor = depth 3. */
 levelmap=-1;for(int m=0;m<dungeon_map_count();m++)if(rec_map_level(m)==3){levelmap=m;break;}
 if(levelmap<0)return false;
 map_set_party_map(levelmap);set_roster();view_load_map(levelmap);
 g_party_connected[0]=false;g_party_connected[1]=false;
 if(!coop_import())return false;
 for(int p=0;p<2;p++){party_context_activate(p);g_party_map_index=levelmap;g_party_map_x=homes[p][0];g_party_map_y=homes[p][1];g_party_direction=coop_dirs[p];for(int c=0;c<4;c++){g_champions[c].direction=g_party_direction;g_champions[c].cell=(g_party_direction+c)&3;}g_party.freeze_life_ticks=0;g_session.party_dead=false;}
 select_party(0);g_sound_world_hook=world_sound;return true;
}
static void snapshot(Json*j){
 json_open(j,'{');json_key_bool(j,"ok",true);json_key_int(j,"time",g_game_time);json_key_int(j,"map",levelmap);json_key_int(j,"depth",rec_map_level(levelmap));
 json_key(j,"coop");json_open(j,'{');json_key_str(j,"name",COOP_NAME);json_key_str(j,"controller","authoritative bridge occupancy latches; native door");json_key_bool(j,"sealA",seals[0]);json_key_bool(j,"sealB",seals[1]);json_key_bool(j,"doorOpen",seals[0]&&seals[1]);json_key_bool(j,"exitA",finished[0]);json_key_bool(j,"exitB",finished[1]);json_key_bool(j,"won",finished[0]&&finished[1]);json_close(j,'}');
 json_key_int(j,"soundCursor",sound_id);
 json_key(j,"events");json_open(j,'[');
 for(unsigned id=sound_id>256?sound_id-255:1;id<=sound_id;id++){SoundEvent*e=&journal[(id-1)%256];json_open(j,'{');json_key_str(j,"type","sound");json_key_int(j,"id",e->id);json_key_int(j,"time",e->time);json_key_int(j,"sound",e->sound);json_key_int(j,"map",e->map);json_key_int(j,"x",e->x);json_key_int(j,"y",e->y);json_close(j,'}');}json_close(j,']');
 json_key(j,"monsters");json_open(j,'[');
 for(int y=0;y<g_current_map_height;y++)for(int x=0;x<g_current_map_width;x++){uint16_t g=map_find_thing((Location){x,y},THING_GROUP);if(g==THING_ENDOFLIST)continue;json_open(j,'{');json_key_int(j,"id",g);json_key_int(j,"x",x);json_key_int(j,"y",y);json_key_int(j,"type",rec_group_creature_type(g));json_key_int(j,"count",rec_group_last_creature(g)+1);json_key(j,"health");json_open(j,'[');for(int c=0;c<=rec_group_last_creature(g);c++)json_int(j,rec_group_health(g,c));json_close(j,']');json_close(j,'}');}json_close(j,']');
 json_key(j,"doors");json_open(j,'[');
 for(int y=0;y<g_current_map_height;y++)for(int x=0;x<g_current_map_width;x++)if(rec_square_type(map_get_square(x,y))==SQUARE_DOOR){json_open(j,'{');json_key_int(j,"x",x);json_key_int(j,"y",y);json_key_int(j,"state",map_get_square(x,y)&7);bool has_button=false;for(int b=0;b<COOP_BUTTONDOORS_COUNT;b++)if(x==coop_buttonDoors[b][0]&&y==coop_buttonDoors[b][1])has_button=true;json_key_bool(j,"button",has_button);json_close(j,'}');}
 json_close(j,']');
 json_key(j,"parties");json_open(j,'[');
 int saved=g_active_party;
 for(int p=0;p<2;p++){select_party(p);redraw();json_open(j,'{');json_key_int(j,"id",p);json_key_bool(j,"connected",g_party_connected[p]);json_key(j,"state");state_write_state(j);json_key(j,"view");state_write_view(j);
 get_eye_box();json_key_bool(j,"eyeHeld",command_eye_is_held());json_key(j,"eyeBox");json_open(j,'[');for(int k=0;k<4;k++)json_int(j,eye_box[k]);json_close(j,']');json_key_int(j,"formationSelection",command_get_dragged_icon());json_key(j,"art");json_open(j,'[');for(int c=0;c<g_party_champion_count;c++){json_open(j,'{');json_key_int(j,"portrait",art_portraits[p][c]);json_key_int(j,"cell",g_champions[c].cell);json_key_int(j,"direction",g_champions[c].direction);json_key_int(j,"serial",party_art_serial[p][c]);json_key_int(j,"tick",party_art_tick[p][c]);json_key_int(j,"action",party_art_action[p][c]);json_key(j,"equipment");json_open(j,'[');for(int k=0;k<6;k++){uint16_t t=g_champions[c].slots[k];if(t==THING_NONE)continue;int kind=THING_TYPE(t),type=-1;if(kind==THING_WEAPON)type=rec_weapon_type(t);if(kind==THING_ARMOUR)type=rec_armour_type(t);json_open(j,'{');json_key_int(j,"slot",k);json_key_int(j,"kind",kind);json_key_int(j,"type",type);json_close(j,'}');}json_close(j,']');json_close(j,'}');}json_close(j,']');
 json_key(j,"click_boxes");json_open(j,'[');for(int k=0;k<VIEW_CLICK_BOX_COUNT;k++){const Box*b=&view_get_result()->clickable_boxes[k];json_open(j,'{');json_key_int(j,"x",b->x);json_key_int(j,"y",b->y+33);json_key_int(j,"w",b->w);json_key_int(j,"h",b->h);json_close(j,'}');}json_close(j,']');
 json_key(j,"pointer");PointerImage pointer;if(gameloop_get_pointer(100,100,true,&pointer)){json_open(j,'{');json_key_int(j,"w",pointer.w);json_key_int(j,"h",pointer.h);json_key_int(j,"hot_x",pointer.hot_x);json_key_int(j,"hot_y",pointer.hot_y);json_key(j,"pixels");json_open(j,'[');for(int k=0;k<pointer.w*pointer.h;k++)json_int(j,pointer.px[k]);json_close(j,']');json_key(j,"palette");json_open(j,'[');const uint16_t*palette=gameloop_get_pointer_palette();for(int k=0;k<16;k++)json_int(j,video_convert_argb(palette[k]));json_close(j,']');json_close(j,'}');}else json_null(j);
 json_key(j,"ray");json_open(j,'[');
 for(int z=1;z<=4;z++){int16_t x=g_party_map_x,y=g_party_map_y;map_step(g_party_direction,z,0,&x,&y);json_int(j,(x<0||y<0||x>=g_current_map_width||y>=g_current_map_height)?0:map_get_square(x,y));}json_close(j,']');json_close(j,'}');}
 json_close(j,']');select_party(saved);json_close(j,'}');
}
int main(int argc,char**argv){
 if(argc<2||!init(argv[1])){fprintf(stderr,"initialisation failed\n");return 2;}
 setvbuf(stdout,NULL,_IOLBF,0);char line[8192];
 while(fgets(line,sizeof line,stdin)){
  char cmd[32];request_get_string(line,"cmd",cmd,sizeof cmd);int p=(int)request_get_int(line,"party",0);if(p<0||p>1)p=0;
  const char *error=NULL;
  if(!strcmp(cmd,"connect"))connection(p,true);
  else if(!strcmp(cmd,"leave"))connection(p,false);
  else if(!strcmp(cmd,"tick")){
   if(getenv("DM_TRACE"))for(uint16_t k=0;k<g_timeline.high_water;k++){TimelineEvent*e=&g_timeline.events[k];if((e->type==EVENT_DOOR||e->type==EVENT_DOOR_ANIMATION)&&event_square_x(e)==1&&event_square_y(e)==13)fprintf(stderr,"DOOREVENT now=%u due=%u type=%u owner=%u map=%u heapfirst_due=%u currentmap=%d active=%d\n",g_game_time,e->map_time&EVENT_TIME_MASK,e->type,e->owner,EVENT_MAP(e),g_timeline.events[g_timeline.heap[0]].map_time&EVENT_TIME_MASK,g_current_map_index,g_active_party);}
   if(g_party_connected[0]||g_party_connected[1]){int s=g_party_connected[0]?0:1;select_party(s);gameloop_tick();}
  }else if(!strcmp(cmd,"frame")){
   select_party(p);redraw();char path[512];if(request_get_string(line,"path",path,sizeof path))png_write(path);
  }else if(!strcmp(cmd,"action")){
   select_party(p);redraw();
   if(!g_party_connected[p]||g_session.party_dead)error="party offline or dead";
   else{
    char a[64];request_get_string(line,"action",a,sizeof a);
    if(getenv("DM_TRACE"))fprintf(stderr,"TRACE action=%s party=%d time=%u pos=%d,%d dir=%d empty=%d view=%d events=%u line=%s",a,p,g_game_time,g_party_map_x,g_party_map_y,g_party_direction,g_leader_empty_handed,view_get_result()->square_ahead_element,g_timeline.count,line);

    if(!strncmp(a,"move_",5)&&g_disabled_movement_ticks)error="party movement cooldown";
    if(!strncmp(a,"move_",5)){
     int16_t x=g_party_map_x,y=g_party_map_y;int f=!strcmp(a,"move_forward")?1:!strcmp(a,"move_back")?-1:0;int r=!strcmp(a,"move_right")?1:!strcmp(a,"move_left")?-1:0;map_step(g_party_direction,f,r,&x,&y);
     int type=rec_square_type(map_get_square(x,y));
     if(type==SQUARE_STAIRS||type==SQUARE_PIT||type==SQUARE_TELEPORTER)error="level 4 prototype boundary";
    }
    if(!error&&(!strcmp(a,"sleep")||!strcmp(a,"recruit")))error="unavailable in shared prototype";
    if(!error&&!strcmp(a,"mouse")){
     int x=request_get_int(line,"x",-1),y=request_get_int(line,"y",-1);
     if(x<0||x>=320||y<0||y>=200)error="invalid mouse";
     else {bool right=request_get_int(line,"button",0)==2;char phase[16]="click";request_get_string(line,"phase",phase,sizeof phase);if(strcmp(phase,"click")&&strcmp(phase,"down")&&strcmp(phase,"up")&&strcmp(phase,"cancel"))error="invalid mouse phase";else if(!strcmp(phase,"click")){command_process_click(x,y,right?CLICK_RIGHT_PRESS:CLICK_LEFT_PRESS);command_process_click(x,y,right?CLICK_RIGHT_RELEASE:CLICK_LEFT_RELEASE);gameloop_run_commands();}else if(right)error="held phase only for left eye";else if(!strcmp(phase,"down")){if(g_session.inventory_champion_index<0||ui_command_from_mouse(TABLES_MOUSE_INVENTORY,x,y,UI_BUTTON_LEFT)!=COMMAND_CLICK_EYE)error="held target is not inventory eye";else{command_process_click(x,y,CLICK_LEFT_PRESS);gameloop_run_commands();}}else{command_process_click(x,y,CLICK_LEFT_RELEASE);gameloop_run_commands();}}
    }else if(!error)error=control_run_action(a,line);
   }
  }
  if(getenv("DM_TRACE")&&!strcmp(cmd,"action"))fprintf(stderr,"TRACE after error=%s events=%u door1,13=%u\n",error?error:"none",g_timeline.count,map_get_square(1,13)&7);
  coop_update();
  Json j={0};if(error){json_open(&j,'{');json_key_bool(&j,"ok",false);json_key_str(&j,"error",error);json_close(&j,'}');}else snapshot(&j);
  puts(j.s);json_free(&j);
 }
 return 0;
}
