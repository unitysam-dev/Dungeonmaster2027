#define main bridge_program_main
#include "bridge.c"
#include "projectile.h"
#undef main
static int passes,failures;
static void check(bool yes,const char*s){printf("%s %s\n",yes?"PASS":"FAIL",s);if(yes)passes++;else failures++;}
static int hp(int p){party_context_activate(p);int n=0;for(int i=0;i<4;i++)n+=g_champions[i].current_health;return n;}
static void ticks(int n){for(int i=0;i<n;i++){select_party(g_party_connected[0]?0:1);gameloop_tick();}}
static uint16_t first_group(void){map_set_current_map(levelmap);for(int y=0;y<g_current_map_height;y++)for(int x=0;x<g_current_map_width;x++){uint16_t g=map_find_thing((Location){x,y},THING_GROUP);if(g!=THING_ENDOFLIST)return g;}return THING_NONE;}
int main(int argc,char**argv){if(argc<2||!init(argv[1]))return 2;
check(levelmap==3&&rec_map_level(levelmap)==3,"original fourth physical floor = map 3 depth 3");
check(g_party_champion_count==4&&g_parties[1-g_active_party].champion_count==4,"two four-champion parties");
check(first_group()!=THING_NONE,"original populated level retains creatures");
connection(0,true);connection(1,true);uint32_t once=g_game_time;gameloop_tick();check(g_game_time==once+1,"two connected parties advance original world clock exactly once per tick");select_party(0);int ax=g_party_map_x,ay=g_party_map_y;int bx=g_parties[1].map_x,by=g_parties[1].map_y;
move_party(COMMAND_MOVE_FORWARD);check(g_party_map_x==ax+1&&g_party_map_y==ay,"A moves through original handler");
check(g_parties[1].map_x==bx&&g_parties[1].map_y==by,"A movement leaves B position unchanged");
g_disabled_movement_ticks=0;move_party(COMMAND_MOVE_FORWARD);check(g_party_map_x==ax+1,"active B occupancy blocks A through native handler");
move_turn_party(COMMAND_TURN_LEFT);check(g_party_direction==0&&g_parties[1].direction==3,"independent original turn");
/* Shared object: remove actual equipped rock and drop on central floor, then opposite owner grabs. */
uint16_t rock=inventory_take_slot_object(0,SLOT_HAND_ACTION);inventory_put_hand_object(rock);inventory_drop_object(0,(Location){ax+1,ay},false);
check(g_leader_hand_object==THING_NONE,"drop consumes cursor ownership");
select_party(1);check(inventory_grab_object(rock,0,(Location){ax+1,ay}),"B grabs A dropped original object");
check(THING_STRIP_CELL(g_leader_hand_object)==THING_STRIP_CELL(rock),"B holds same unique record");
select_party(0);bool found=false;for(uint16_t t=map_get_square_first_thing(ax+1,ay);t!=THING_ENDOFLIST;t=rec_thing_next(t))if(THING_STRIP_CELL(t)==THING_STRIP_CELL(rock))found=true;
check(!found&&g_leader_hand_object==THING_NONE,"shared pickup conserved, A cannot duplicate floor object");
select_party(1);inventory_drop_object(0,(Location){bx,by},false);
/* Door: original button click -> original timeline animation, unchanged real door record. */
select_party(0);g_party_map_x=23;g_party_map_y=2;g_party_direction=2;redraw();
int before=map_get_square(23,3)&7;const char *err=control_run_action("click_wall_ahead","{}");check(err==NULL,"original real level door button clickable");ticks(16);check((map_get_square(23,3)&7)!=before,"original door animation changes shared square");
select_party(1);check((map_get_square(23,3)&7)!=before,"B observes same changed door record");
/* Bidirectional original thrown projectiles; fixture locations, not toy collision. */
for(int shooter=0;shooter<2;shooter++){
 select_party(0);g_party_map_x=ax;g_party_map_y=ay;g_party_direction=1;for(int c=0;c<4;c++)g_champions[c].cell=c;
 select_party(1);g_party_map_x=bx;g_party_map_y=by;g_party_direction=3;for(int c=0;c<4;c++)g_champions[c].cell=c;
 int target=1-shooter;int beforehp=hp(target);select_party(shooter);
 uint16_t shot=discard_get_unused_thing(THING_WEAPON);rec_object_set_type(shot,WEAPON_ROCK);inventory_put_hand_object(shot);
 int stamina=g_champions[0].current_stamina;bool thrown=action_is_hand_object_thrown(false);
 check(thrown,"original cursor throw launches projectile");ticks(20);
 check(hp(target)<beforehp,shooter?"B to A original friendly fire damage":"A to B original friendly fire damage");
 select_party(shooter);check(g_champions[0].current_stamina<stamina,"throw uses original stamina cost");
}
/* Actual creature event is processed ONCE against nearest connected party B while A selected. */
select_party(0);g_party_map_x=8;g_party_map_y=1;
select_party(1);g_party_map_x=1;g_party_map_y=14;g_party_direction=2;
/* Earlier real ticks may move the original Screamer away from its initial square.
   Test aggro at its CURRENT position, not a stale hard-coded location. */
map_set_current_map(levelmap);bool adjacent=false;
for(int y=1;y<g_current_map_height-1&&!adjacent;y++)for(int x=1;x<g_current_map_width-1&&!adjacent;x++){
 uint16_t group=map_find_thing((Location){x,y},THING_GROUP);if(group==THING_ENDOFLIST)continue;
 for(int d=0;d<4&&!adjacent;d++){int nx=x+(int[]){0,1,0,-1}[d],ny=y+(int[]){-1,0,1,0}[d];if(rec_square_type(map_get_square(nx,ny))==SQUARE_CORRIDOR&&map_find_thing((Location){nx,ny},THING_GROUP)==THING_ENDOFLIST&&!(nx==8&&ny==1)){g_party_map_x=nx;g_party_map_y=ny;adjacent=true;}}
}

select_party(0);int bhp=hp(1);select_party(0);uint32_t ct=g_game_time;
bool creature_hit=false;for(int n=0;n<1280&&!creature_hit;n++){int previous=hp(1);ticks(1);creature_hit=hp(1)<previous;}
fprintf(stderr,"CREATURE DEBUG beforehp%d afterhp%d worldtime%u started%u Bpos%d,%d active%d connected%d,%d\n",bhp,hp(1),g_game_time,ct,g_party_map_x,g_party_map_y,g_active_party,g_party_connected[0],g_party_connected[1]);
for(int yy=0;yy<g_current_map_height;yy++)for(int xx=0;xx<g_current_map_width;xx++){uint16_t gg=map_find_thing((Location){xx,yy},THING_GROUP);if(gg!=THING_ENDOFLIST)fprintf(stderr,"GROUP now %d,%d thing%u type%u\n",xx,yy,gg,rec_group_creature_type(gg));}
check(creature_hit,"unfrozen original nearby creature damages B with A selected (regen-safe delta)");
check(g_game_time>ct,"creature simulation advances one shared world timeline");
select_party(0);g_party_map_x=ax;g_party_map_y=ay;select_party(1);g_party_map_x=bx;g_party_map_y=by;
/* Withdraw B: no occupancy, no target, no tick resources; resume safe if A takes old tile. */
select_party(1);int oldx=g_party_map_x,oldy=g_party_map_y;int health=hp(1);int food=g_champions[0].food;connection(1,false);
select_party(0);check(!party_context_is_on(1,oldx,oldy),"offline party not world occupancy");int16_t victim=-1;g_party_map_x=ax;g_party_map_y=ay;int target=group_get_target_champion(oldx,oldy,0,&victim);check(target<0,"withdrawn B not creature target");
select_party(1);g_champions[3].shield_defense=12;TimelineEvent shield={.map_time=EVENT_MAP_TIME(levelmap,g_game_time+2),.type=EVENT_CHAMPION_SHIELD_END,.priority=3,.b=2};timeline_add_event(&shield);select_party(0);
select_party(0);g_party_direction=1;uint16_t offline_shot=discard_get_unused_thing(THING_WEAPON);rec_object_set_type(offline_shot,WEAPON_ROCK);projectile_shoot_from_champion(0,offline_shot,200,200,10);ticks(20);check(hp(1)==health,"original projectile passes withdrawn B without damage");
ticks(70);check(hp(1)==health&&g_champions[0].food==food,"offline B health and food unchanged through real ticks");
select_party(1);check(g_champions[3].shield_defense==12,"withdrawn B party-scoped timer does not expire");
select_party(0);g_party_map_x=oldx;g_party_map_y=oldy;int door=map_get_square(23,3);connection(1,true);check(g_party_map_x!=oldx||g_party_map_y!=oldy,"rejoin chooses free tile when old tile occupied");check(map_get_square(23,3)==door,"rejoin preserves world door progress");
ticks(3);select_party(1);check(g_champions[3].shield_defense==10,"preserved owner timer resumes on reconnect");
select_party(0);g_party_map_x=ax;g_party_map_y=ay;select_party(1);g_party_map_x=bx;g_party_map_y=by;select_party(0);
check(party_context_nearest(levelmap,bx+1,by)==1,"creature aggro chooses nearby B even A selected");
check(g_active_party==0,"aggro lookup restores selected owner");
select_party(0);for(int c=0;c<4;c++)g_champions[c].current_health=0;g_session.party_dead=true;select_party(1);uint32_t t=g_game_time;gameloop_tick();check(g_game_time==t+1,"A death does not halt B world tick");
connection(0,false);connection(1,false);t=g_game_time;check(!g_party_connected[0]&&!g_party_connected[1],"both parties safely withdrawn for host idle pause");
printf("SUMMARY %d pass %d fail\n",passes,failures);return failures?1:0;}
