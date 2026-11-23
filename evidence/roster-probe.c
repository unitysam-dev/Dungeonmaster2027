#define main bridge_program_main
#include "bridge.c"
#undef main
int main(void){if(!init("private-game"))return 1;printf("map %d types %d ornaments %d/%d/%d\n",levelmap,rec_map_creature_types(levelmap),rec_map_wall_ornaments(levelmap),rec_map_floor_ornaments(levelmap),rec_map_door_ornaments(levelmap));for(int i=0;i<CREATURE_TYPE_COUNT;i++){const uint8_t *a=tables_get_creature_aspect(i);printf("%d size %d",i,creature_size(tables_get_creature_info(i)));for(int j=0;j<8;j++)printf(" %02x",a[j]);puts("");}return 0;}
