/**
 * @file sponk.c
 * @brief Implementation of the Sponk text adventure game
 * 
 * Contains all game logic, command processing, and utility functions
 * for the cave exploration adventure game.
 */

#include "sponk.h"
#include <unistd.h>
#include <string.h>

/** Room database with all game locations */
static Room rooms[ROOM_COUNT] = {
    [ROOM_CAVE_ENTRANCE] = {
        .id = ROOM_CAVE_ENTRANCE,
        .name = "Cave Entrance",
        .description = "You stand at the entrance of a dark cave. The air is cool and damp.\nA narrow tunnel leads deeper into the darkness to the north.",
        .north = ROOM_DARK_TUNNEL,
        .south = ROOM_CAVE_ENTRANCE,
        .east = ROOM_CAVE_ENTRANCE,
        .west = ROOM_CAVE_ENTRANCE,
        .item = ITEM_TORCH,
        .visited = false,
        .locked = false
    },
    [ROOM_DARK_TUNNEL] = {
        .id = ROOM_DARK_TUNNEL,
        .name = "Dark Tunnel",
        .description = "You are in a pitch-black tunnel. You can't see anything without light.\nYou hear water dripping in the distance.",
        .north = ROOM_UNDERGROUND_LAKE,
        .south = ROOM_CAVE_ENTRANCE,
        .east = ROOM_CRYSTAL_CHAMBER,
        .west = ROOM_DARK_TUNNEL,
        .item = ITEM_NONE,
        .visited = false,
        .locked = false
    },
    [ROOM_TREASURE_ROOM] = {
        .id = ROOM_TREASURE_ROOM,
        .name = "Treasure Room",
        .description = "A magnificent room filled with gold and jewels!\nA large treasure chest sits in the center, locked with an ornate mechanism.",
        .north = ROOM_TREASURE_ROOM,
        .south = ROOM_UNDERGROUND_LAKE,
        .east = ROOM_TREASURE_ROOM,
        .west = ROOM_TREASURE_ROOM,
        .item = ITEM_TREASURE,
        .visited = false,
        .locked = true
    },
    [ROOM_UNDERGROUND_LAKE] = {
        .id = ROOM_UNDERGROUND_LAKE,
        .name = "Underground Lake",
        .description = "You stand before a vast underground lake. The water is eerily still.\nA passage leads north to what looks like a treasure room.",
        .north = ROOM_TREASURE_ROOM,
        .south = ROOM_DARK_TUNNEL,
        .east = ROOM_UNDERGROUND_LAKE,
        .west = ROOM_UNDERGROUND_LAKE,
        .item = ITEM_KEY,
        .visited = false,
        .locked = false
    },
    [ROOM_CRYSTAL_CHAMBER] = {
        .id = ROOM_CRYSTAL_CHAMBER,
        .name = "Crystal Chamber",
        .description = "A beautiful chamber with glowing crystals embedded in the walls.\nThe crystals emit a soft blue light. You see an exit to the north.",
        .north = ROOM_EXIT,
        .south = ROOM_CRYSTAL_CHAMBER,
        .east = ROOM_CRYSTAL_CHAMBER,
        .west = ROOM_DARK_TUNNEL,
        .item = ITEM_CRYSTAL,
        .visited = false,
        .locked = false
    },
    [ROOM_EXIT] = {
        .id = ROOM_EXIT,
        .name = "Cave Exit",
        .description = "You emerge into daylight! The adventure is complete.",
        .north = ROOM_EXIT,
        .south = ROOM_CRYSTAL_CHAMBER,
        .east = ROOM_EXIT,
        .west = ROOM_EXIT,
        .item = ITEM_NONE,
        .visited = false,
        .locked = false
    }
};

/* ============================================================================
 * Utility Functions
 * ========================================================================== */

void print_string(const char *str) {
    write(1, str, strlen(str));
}

void print_line(const char *str) {
    print_string(str);
    print_string("\n");
}

void clear_screen(void) {
    print_string("\033[2J\033[H");
}

int read_input(char *buffer, int max_len) {
    int count = read(0, buffer, max_len - 1);
    if (count > 0) {
        buffer[count] = '\0';
        // Remove trailing newline
        if (buffer[count - 1] == '\n') {
            buffer[count - 1] = '\0';
            count--;
        }
    }
    return count;
}

int string_compare(const char *s1, const char *s2) {
    while (*s1 && *s2 && *s1 == *s2) {
        s1++;
        s2++;
    }
    return *s1 - *s2;
}

bool string_starts_with(const char *str, const char *prefix) {
    while (*prefix) {
        if (*str != *prefix) {
            return false;
        }
        str++;
        prefix++;
    }
    return true;
}

/**
 * @brief Convert string to lowercase (in-place)
 * @param str String to convert
 */
void to_lower(char *str) {
    while (*str) {
        if (*str >= 'A' && *str <= 'Z') {
            *str = *str + 32;
        }
        str++;
    }
}

/* ============================================================================
 * Game Functions
 * ========================================================================== */

Room *get_room(RoomID id) {
    if (id >= 0 && id < ROOM_COUNT) {
        return &rooms[id];
    }
    return NULL;
}

const char *get_item_name(ItemID item) {
    switch (item) {
        case ITEM_TORCH: return "torch";
        case ITEM_KEY: return "key";
        case ITEM_SWORD: return "sword";
        case ITEM_TREASURE: return "treasure";
        case ITEM_CRYSTAL: return "crystal";
        default: return "unknown";
    }
}

bool has_item(const GameState *game, ItemID item) {
    for (int i = 0; i < game->inventory_count; i++) {
        if (game->inventory[i] == item) {
            return true;
        }
    }
    return false;
}

void add_item(GameState *game, ItemID item) {
    if (game->inventory_count < MAX_INVENTORY) {
        game->inventory[game->inventory_count++] = item;
        if (item == ITEM_TORCH) {
            game->has_light = true;
        }
    }
}

void remove_item(GameState *game, ItemID item) {
    for (int i = 0; i < game->inventory_count; i++) {
        if (game->inventory[i] == item) {
            // Shift remaining items
            for (int j = i; j < game->inventory_count - 1; j++) {
                game->inventory[j] = game->inventory[j + 1];
            }
            game->inventory_count--;
            if (item == ITEM_TORCH) {
                game->has_light = false;
            }
            return;
        }
    }
}

void init_game(GameState *game) {
    game->current_room = ROOM_CAVE_ENTRANCE;
    game->inventory_count = 0;
    game->has_light = false;
    game->game_won = false;
    game->running = true;
    
    // Reset all rooms
    for (int i = 0; i < ROOM_COUNT; i++) {
        rooms[i].visited = false;
    }
}

// cppcheck-suppress constParameterPointer
void look_around(GameState *game) {
    Room *room = get_room(game->current_room);
    if (!room) return;
    
    room->visited = true;
    
    print_line("\n=====================================");
    print_string("Location: ");
    print_line(room->name);
    print_line("=====================================");
    
    // Check if room requires light
    if (game->current_room == ROOM_DARK_TUNNEL && !game->has_light) {
        print_line("It's too dark to see anything! You need a light source.");
    } else {
        print_line(room->description);
        
        // Show items in room
        if (room->item != ITEM_NONE) {
            print_string("\nYou see a ");
            print_string(get_item_name(room->item));
            print_line(" here.");
        }
    }
    
    print_line("\n");
}

void show_inventory(const GameState *game) {
    print_line("\n--- Inventory ---");
    if (game->inventory_count == 0) {
        print_line("You are carrying nothing.");
    } else {
        for (int i = 0; i < game->inventory_count; i++) {
            print_string("- ");
            print_line(get_item_name(game->inventory[i]));
        }
    }
    print_line("");
}

void move_player(GameState *game, RoomID direction) {
    // cppcheck-suppress constVariablePointer
    Room *current = get_room(game->current_room);
    if (!current) return;
    
    // Can't navigate in the dark
    if (game->current_room == ROOM_DARK_TUNNEL && !game->has_light) {
        print_line("It's too dark to navigate safely!");
        return;
    }
    
    if (direction == game->current_room) {
        print_line("You can't go that way.");
        return;
    }
    
    Room *next_room = get_room(direction);
    if (!next_room) {
        print_line("You can't go that way.");
        return;
    }
    
    // Check if locked
    if (next_room->locked) {
        if (has_item(game, ITEM_KEY)) {
            print_line("You use the key to unlock the door!");
            next_room->locked = false;
            remove_item(game, ITEM_KEY);
        } else {
            print_line("The door is locked. You need a key.");
            return;
        }
    }
    
    game->current_room = direction;
    
    // Check for win condition
    if (game->current_room == ROOM_EXIT && has_item(game, ITEM_TREASURE)) {
        game->game_won = true;
        game->running = false;
    }
    
    look_around(game);
}

void take_item(GameState *game) {
    Room *room = get_room(game->current_room);
    if (!room || room->item == ITEM_NONE) {
        print_line("There's nothing here to take.");
        return;
    }
    
    // Can't see items in the dark
    if (game->current_room == ROOM_DARK_TUNNEL && !game->has_light) {
        print_line("It's too dark to see anything!");
        return;
    }
    
    print_string("You take the ");
    print_string(get_item_name(room->item));
    print_line(".");
    
    add_item(game, room->item);
    room->item = ITEM_NONE;
}

void use_item(GameState *game, const char *item_name) {
    if (string_compare(item_name, "torch") == 0) {
        if (has_item(game, ITEM_TORCH)) {
            print_line("You light the torch. The darkness retreats!");
            game->has_light = true;
            look_around(game);
        } else {
            print_line("You don't have a torch.");
        }
    } else if (string_compare(item_name, "key") == 0) {
        print_line("You should use the key on a locked door by trying to move through it.");
    } else {
        print_line("You can't use that.");
    }
}

void show_help(void) {
    print_line("\n--- Available Commands ---");
    print_line("look / l        - Look around the current room");
    print_line("north / n       - Move north");
    print_line("south / s       - Move south");
    print_line("east / e        - Move east");
    print_line("west / w        - Move west");
    print_line("take / get      - Take an item");
    print_line("inventory / i   - Show your inventory");
    print_line("use <item>      - Use an item");
    print_line("help / h        - Show this help");
    print_line("quit / q        - Quit the game");
    print_line("");
}

void process_command(GameState *game, const char *command) {
    char cmd[MAX_INPUT];
    strncpy(cmd, command, MAX_INPUT - 1);
    cmd[MAX_INPUT - 1] = '\0';
    to_lower(cmd);

    // cppcheck-suppress constVariablePointer
    Room *current = get_room(game->current_room);
    if (!current) return;
    
    if (string_compare(cmd, "look") == 0 || string_compare(cmd, "l") == 0) {
        look_around(game);
    }
    else if (string_compare(cmd, "north") == 0 || string_compare(cmd, "n") == 0) {
        move_player(game, current->north);
    }
    else if (string_compare(cmd, "south") == 0 || string_compare(cmd, "s") == 0) {
        move_player(game, current->south);
    }
    else if (string_compare(cmd, "east") == 0 || string_compare(cmd, "e") == 0) {
        move_player(game, current->east);
    }
    else if (string_compare(cmd, "west") == 0 || string_compare(cmd, "w") == 0) {
        move_player(game, current->west);
    }
    else if (string_compare(cmd, "take") == 0 || string_compare(cmd, "get") == 0) {
        take_item(game);
    }
    else if (string_compare(cmd, "inventory") == 0 || string_compare(cmd, "i") == 0) {
        show_inventory(game);
    }
    else if (string_starts_with(cmd, "use ")) {
        use_item(game, cmd + 4);
    }
    else if (string_compare(cmd, "help") == 0 || string_compare(cmd, "h") == 0) {
        show_help();
    }
    else if (string_compare(cmd, "quit") == 0 || string_compare(cmd, "q") == 0) {
        game->running = false;
    }
    else {
        print_line("I don't understand that command. Type 'help' for available commands.");
    }
}

void game_loop(void) {
    GameState game;
    char input[MAX_INPUT];
    
    init_game(&game);
    clear_screen();
    print_line("\n"); 
    print_line("=====================================");
    print_line("   CAVE ADVENTURE");
    print_line("=====================================");
    print_line("\nYou are an adventurer seeking treasure in a mysterious cave.");
    print_line("Type 'help' to see available commands.\n");
    
    look_around(&game);
    
    while (game.running) {
        print_string("> ");
        
        int len = read_input(input, MAX_INPUT);
        if (len <= 0) {
            continue;
        }
        
        print_string("\n");  // Add newline after user input
        process_command(&game, input);
    }
    
    // End game screen
    clear_screen();
    print_line("\n=====================================");
    if (game.game_won) {
        print_line("   CONGRATULATIONS!");
        print_line("=====================================");
        print_line("\nYou escaped the cave with the treasure!");
        print_line("You are victorious!");
    } else {
        print_line("   THANKS FOR PLAYING!");
        print_line("=====================================");
        print_line("\nYour adventure ends here...");
    }
    print_line("\n");
}

int main(void) {
    game_loop();
    _exit(0);
    return 0;
}

