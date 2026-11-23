#define main bridge_program_main
#include "bridge.c"
#undef main
static void target(int table,int cmd,const char*name){int xmin=320,xmax=-1,ymin=200,ymax=-1;for(int y=0;y<200;y++)for(int x=0;x<320;x++)if(ui_command_from_mouse(table,x,y,UI_BUTTON_LEFT)==cmd){if(x<xmin)xmin=x;if(x>xmax)xmax=x;if(y<ymin)ymin=y;if(y>ymax)ymax=y;}printf("%s %d %d bounds %d %d %d %d\n",name,(xmin+xmax)/2,(ymin+ymax)/2,xmin,ymin,xmax,ymax);}
int main(int n,char**a){if(n<2||!init(a[1]))return 2;select_party(0);redraw();for(int i=0;i<4;i++){char s[40];snprintf(s,sizeof s,"formation%d",i);target(TABLES_MOUSE_INTERFACE,COMMAND_CLICK_CHAMPION_ICON_FIRST+i,s);}for(int i=0;i<4;i++){char key[20];snprintf(key,sizeof key,"weapon%d",i);target(TABLES_MOUSE_ACTION_ICONS,COMMAND_SELECT_ACTING_CHAMPION_FIRST+i,key);}for(int i=0;i<3;i++){char s[40];snprintf(s,sizeof s,"action%d",i);target(TABLES_MOUSE_ACTION_NAMES,COMMAND_ACTION_FIRST+i,s);}return 0;}
