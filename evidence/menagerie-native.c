#define main bridge_program_main
#include "bridge.c"
#undef main
#include <assert.h>
static unsigned hash_view(void){unsigned h=1;for(int y=33;y<135;y++)for(int x=0;x<224;x++)h=33*h+g_screen.px[y][x];return h;}
int main(void){
 assert(init("private-game"));assert(g_active_group_count==15&&rec_map_creature_types(levelmap)==15);connection(0,true);connection(1,true);
 unsigned seen=0;
 for(int i=0;i<COOP_ENCOUNTER_COUNT;i++){
  select_party(i%2);const int *e=coop_encounters[i];int x=e[0],y=e[1];Location at={x,y};uint16_t g=map_find_thing(at,THING_GROUP);assert(g!=THING_ENDOFLIST&&group_get_active(g)&&group_is_allowed_on_map(g,levelmap));assert(rec_group_creature_type(g)==e[2]&&rec_group_last_creature(g)==0&&rec_group_health(g,0)==e[4]);assert(!(seen&(1u<<e[2])));seen|=1u<<e[2];
  g_party_map_x=x;g_party_map_y=y-1;g_party_direction=2;redraw();unsigned with=hash_view();map_unlink_thing_from_list(g,0,x,y);redraw();unsigned without=hash_view();map_link_thing_to_list(g,0,x,y);assert(with!=without);printf("PASS type %d at %d,%d changes real framebuffer\n",e[2],x,y);
 }
 for(int p=0;p<2;p++){select_party(p);g_party_map_x=homes[p][0];g_party_map_y=homes[p][1];g_party_direction=coop_dirs[p];}
 for(int i=0;i<100;i++){select_party(i%2);gameloop_tick();coop_update();redraw();}
 assert(g_active_group_count==15);puts("PASS 15 native groups survive 100 shared game ticks and alternating redraws");
 return 0;
}
