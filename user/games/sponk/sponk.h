#ifndef sponk_H
#define sponk_H

#include <stdint.h>
#include <stdbool.h>

// Game configuration
#define MAX_INPUT 128
#define MAX_INVENTORY 10

// Room IDs
typedef enum {
    ROOM_CAVE_ENTRANCE = 0,
    ROOM_DARK_TUNNEL,
    ROOM_TREASURE_ROOM,
    ROOM_UNDERGROUND_LAKE,
    ROOM_CRYSTAL_CHAMBER,
    ROOM_EXIT,
    ROOM_COUNT
} RoomID;

// Item IDs
typedef enum {
    ITEM_NONE = 0,
    ITEM_TORCH,
    ITEM_KEY,
    ITEM_SWORD,
    ITEM_TREASURE,
    ITEM_CRYSTAL,
    ITEM_COUNT
} ItemID;

// Room structure
typedef struct {
    RoomID id;
    const char *name;
    const char *description;
    RoomID north;
    RoomID south;
    RoomID east;
    RoomID west;
    ItemID item;
    bool visited;
    bool locked;
} Room;

// Game state
typedef struct {
    RoomID current_room;
    ItemID inventory[MAX_INVENTORY];
    int inventory_count;
    bool has_light;
    bool game_won;
    bool running;
} GameState;

// Function prototypes
void init_game(GameState *game);
void game_loop(void);
void process_command(GameState *game, const char *command);
void look_around(GameState *game);
void show_inventory(const GameState *game);
void move_player(GameState *game, RoomID direction);
void take_item(GameState *game);
void use_item(GameState *game, const char *item_name);
bool has_item(const GameState *game, ItemID item);
void add_item(GameState *game, ItemID item);
void remove_item(GameState *game, ItemID item);
const char *get_item_name(ItemID item);
Room *get_room(RoomID id);
void print_string(const char *str);
void print_line(const char *str);
void clear_screen(void);
int read_input(char *buffer, int max_len);
int string_compare(const char *s1, const char *s2);
bool string_starts_with(const char *str, const char *prefix);

#endif // sponk_H
