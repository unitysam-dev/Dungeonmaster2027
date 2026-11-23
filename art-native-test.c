#define main bridge_program_main
#include "bridge.c"
#include "ids.h"
#undef main
#include <assert.h>
static int count;static void check(bool x,const char*s){printf("%s %s\n",x?"PASS":"FAIL",s);assert(x);count++;fflush(stdout);}
int main(int argc,char**argv){if(argc<2||!init(argv[1]))return 2;connection(0,true);connection(1,true);
check(art_portraits[0][0]==23&&art_portraits[0][1]==1&&art_portraits[0][2]==10&&art_portraits[0][3]==13,"real recruitment IDs identify reviewed four");
for(int p=0;p<2;p++){select_party(p);g_party_map_x=p?9:8;g_party_map_y=1;g_party_direction=p?3:1;for(int c=0;c<4;c++)g_champions[c].direction=g_party_direction;}
select_party(0);unsigned old=party_art_serial[0][0];bool performed=action_is_action_performed(0,ACTION_SWING);check(performed&&party_art_serial[0][0]==old+1,"performed front-rank native sword journals exactly one event");check(party_art_tick[0][0]==g_game_time&&party_art_action[0][0]==ACTION_SWING,"event source tick and action exact");check(party_art_serial[1][0]==0,"other party action journal independent");
old=party_art_serial[0][0];g_champions[0].cell=3;g_champions[1].cell=2;performed=action_is_action_performed(0,ACTION_SWING);check(!performed&&party_art_serial[0][0]==old,"native rejected rear-rank reach emits no event");
g_champions[0].current_health=0;performed=action_is_action_performed(0,ACTION_SWING);check(!performed&&party_art_serial[0][0]==old,"dead native champion emits no event");
Json j={0};snapshot(&j);check(strstr(j.s,"\"art\"")&&strstr(j.s,"\"portrait\":23")&&strstr(j.s,"\"equipment\"")&&strstr(j.s,"\"serial\":1"),"real bridge serializes source art metadata");json_free(&j);printf("SUMMARY %d passed\n",count);return 0;}
