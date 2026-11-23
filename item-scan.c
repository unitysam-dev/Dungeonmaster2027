#define main bridge_program_main
#include "bridge.c"
#undef main
int main(int n,char**a){if(n<2||!init(a[1]))return 2;int kinds[]={THING_WEAPON,THING_ARMOUR,THING_JUNK};int counts[]={WEAPON_TYPE_COUNT,ARMOUR_TYPE_COUNT,JUNK_TYPE_COUNT};for(int k=0;k<3;k++){uint16_t t=discard_get_unused_thing(kinds[k]);for(int i=0;i<counts[k];i++){rec_object_set_type(t,i);const ObjectInfo*info=tables_get_object_info(inventory_get_object_info_index(t));printf("%d %d %s mask=%u weight=%d\n",kinds[k],i,gfx_get_object_name(object_get_icon_index(t)),info?info->allowed_slots:0,inventory_get_object_weight(t));}}return 0;}
