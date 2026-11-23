#define main bridge_program_main
#include "bridge.c"
#undef main
int main(int argc,char**argv){if(argc<2||!init(argv[1]))return 2;
for(int m=0;m<dungeon_map_count();m++)printf("MAP index=%d depth=%d physical-floor=%d dimensions=%dx%d difficulty=%d\n",m,rec_map_level(m),rec_map_level(m)+1,dungeon_map_width(m),dungeon_map_height(m),rec_map_difficulty(m));
map_set_current_map(levelmap);int counts[16]={0};
for(int y=0;y<g_current_map_height;y++){for(int x=0;x<g_current_map_width;x++){
int t=rec_square_type(map_get_square(x,y));char c="#.OSDTF"[t<7?t:0];
for(uint16_t th=map_get_square_first_thing(x,y);th!=THING_ENDOFLIST;th=rec_thing_next(th)){counts[THING_TYPE(th)]++;if(THING_TYPE(th)==THING_GROUP)c='M';}
printf("%c",c);}puts("");}
for(int i=0;i<16;i++)printf("THING_TYPE_%d=%d\n",i,counts[i]);
for(int y=0;y<g_current_map_height;y++)for(int x=0;x<g_current_map_width;x++)for(uint16_t th=map_get_square_first_thing(x,y);th!=THING_ENDOFLIST;th=rec_thing_next(th)){
if(THING_TYPE(th)==THING_TEXT){char text[256];if(dungeon_decode_text(text,sizeof text,th,TEXT_MODE_MESSAGE|TEXT_MODE_ANY_VISIBILITY))printf("TEXT %d,%d %s\n",x,y,text);}
if(THING_TYPE(th)==THING_DOOR)printf("DOOR %d,%d record=%u button=%d square=%u\n",x,y,th,rec_door_has_button(th),map_get_square(x,y));
if(THING_TYPE(th)==THING_GROUP)printf("GROUP %d,%d record=%u creature=%d\n",x,y,th,rec_group_creature_type(th));
}
return 0;}
