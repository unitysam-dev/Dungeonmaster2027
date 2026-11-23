#define main bridge_program_main
#include "bridge.c"
#undef main
#include "tables.h"
#include <assert.h>
int main(int argc,char**argv){assert(argc>1&&init(argv[1]));connection(0,true);connection(1,true);select_party(0);unsigned n=sound_id;Location at={g_party_map_x,g_party_map_y};
sound_request_play(SOUND_SWING,at,SOUND_MODE_NONE,0);assert(sound_id==n);puts("PASS NONE ignored");
sound_request_play(SOUND_SWING,at,SOUND_MODE_EVENT,2);assert(sound_id==n);puts("PASS delayed scheduling silent");
for(int i=0;i<3;i++)gameloop_tick();int found=0;for(unsigned k=n+1;k<=sound_id;k++){SoundEvent*e=&journal[(k-1)%256];if(e->sound==SOUND_SWING){found++;assert(e->time>=2);}}assert(found==1);puts("PASS due delayed event emitted exactly once");
n=sound_id;Json j={0};snapshot(&j);json_free(&j);snapshot(&j);json_free(&j);assert(sound_id==n);puts("PASS redraw/snapshot no emission");
g_party_map_x=30;g_party_map_y=30;sound_request_play(SOUND_SWING,at,SOUND_MODE_AFTER_TICK,0);assert(sound_id==n+1);puts("PASS outside active listener range still journals");
g_party_map_x=8;g_party_map_y=1;for(int dir=0;dir<4;dir++){g_party_direction=dir;uint8_t v[2];assert(sound_get_volume(10,2,v));printf("GAIN %d %d %d\n",dir,v[0],v[1]);}
for(int i=0;i<300;i++)sound_request_play(SOUND_SWITCH,at,SOUND_MODE_NOW,0);assert(journal[(sound_id-1)%256].id==sound_id);puts("PASS bounded monotonic journal rollover");return 0;}
