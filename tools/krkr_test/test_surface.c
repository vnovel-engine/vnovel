// test_surface.c — Test that SDL_libretro_surface has pixel data
// Uses dlopen to find symbols in the running krkrsdl2 process
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>

typedef struct {
    Uint32 flags;
    int w, h;
    int pitch;
    void *pixels;
    void *userdata;
    int locked;
    void *lock_data;
    SDL_Rect clip_rect;
    void *map;
    int refcount;
} MySDL_Surface;

int main() {
    setenv("KRKRSDL2_LIBRETRO", "1", 1);
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    
    pid_t pid = fork();
    if (pid == 0) {
        execl("./krkrsdl2", "krkrsdl2", "-debugwin", "-readencoding=UTF-8", NULL);
        exit(1);
    }
    
    sleep(2);
    
    // Read /proc/PID/maps to find the executable base
    char maps_path[256];
    snprintf(maps_path, sizeof(maps_path), "/proc/%d/maps", pid);
    FILE *f = fopen(maps_path, "r");
    if (!f) { printf("Cannot open %s\n", maps_path); kill(pid,9); return 1; }
    
    char line[512];
    unsigned long base = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "krkrsdl2") && strstr(line, "r-xp")) {
            sscanf(line, "%lx", &base);
            printf("Found krkrsdl2 base: 0x%lx\n", base);
            break;
        }
    }
    fclose(f);
    
    if (!base) { printf("krkrsdl2 not found in maps\n"); kill(pid,9); return 1; }
    
    // Read the symbol address from nm output
    FILE *nm = popen("nm ./krkrsdl2 | grep SDL_libretro_surface$", "r");
    if (!nm) { printf("nm failed\n"); kill(pid,9); return 1; }
    unsigned long sym_offset = 0;
    char sym_type;
    char sym_name[256];
    if (fscanf(nm, "%lx %c %s", &sym_offset, &sym_type, sym_name) != 3) {
        printf("Symbol not found\n");
        pclose(nm);
        kill(pid,9);
        return 1;
    }
    pclose(nm);
    printf("Symbol: %s at offset 0x%lx\n", sym_name, sym_offset);
    
    // The symbol is in .bss or .data — need to find the right section
    // For PIE executables, we need the load offset
    // Let's use /proc/PID/maps for the data segment
    f = fopen(maps_path, "r");
    unsigned long data_base = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "krkrsdl2") && strstr(line, "r--p")) {
            sscanf(line, "%lx", &data_base);
            printf("Found data segment: 0x%lx\n", data_base);
            break;
        }
    }
    fclose(f);
    
    kill(pid, 9);
    waitpid(pid, NULL, 0);
    return 0;
}
