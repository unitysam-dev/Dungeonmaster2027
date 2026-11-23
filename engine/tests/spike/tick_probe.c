/* Phase 3 probe: does the ACTUAL original gameloop_tick route run headless on real data? */
#include "adf.h"
#include "game.h"
#include "gameloop.h"
#include "map.h"
#include "move.h"
#include "os.h"
#include "party.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static bool hook_make_folder(const char *path)
{
    struct stat st;
    return mkdir(path, 0755) == 0 || stat(path, &st) == 0;
}

static void hook_list_folder(const char *path, void (*found)(const char *name, void *data), void *data)
{
    DIR *d = opendir(path);
    if (!d)
        return;
    struct dirent *e;
    while ((e = readdir(d)))
        if (strcmp(e->d_name, ".") && strcmp(e->d_name, ".."))
            found(e->d_name, data);
    closedir(d);
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s <game-data-dir> <original-adf>\n", argv[0]);
        return 2;
    }
    g_os_make_folder_hook = hook_make_folder;
    g_os_list_folder_hook = hook_list_folder;
    char path[1024];
    snprintf(path, sizeof path, "%s/KAOS.FTL", argv[1]);
    FILE *f = fopen(path, "rb");
    if (!f) {
        if (!adf_extract(argv[2], argv[1])) {
            printf("FAIL adf_extract\n");
            return 1;
        }
    } else {
        fclose(f);
    }
    if (!gameloop_load_data(argv[1]) || !gameloop_start_new_game(argv[1])) {
        printf("FAIL load/start\n");
        return 1;
    }
    printf("loaded: map=%d x=%d y=%d dir=%d time=%u\n", g_party_map_index, g_party_map_x, g_party_map_y,
           g_party_direction, g_game_time);
    uint32_t t0 = g_game_time;
    for (int i = 0; i < 5; i++)
        gameloop_tick();
    printf("after 5 gameloop_tick: time=%u (delta=%u)\n", g_game_time, g_game_time - t0);
    return 0;
}
