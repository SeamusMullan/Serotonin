/**
 * @file sponk.h
 * @brief Text-based adventure game for Serotonin OS
 * 
 * Sponk is a simple cave exploration game where the player navigates
 * through rooms, collects items, and solves puzzles. The game features
 * a parser-based command system and manages game state including
 * inventory, room visits, and puzzle completion.
 */

#ifndef sponk_H
#define sponk_H

#include <stdint.h>
#include <stdbool.h>

/** Maximum input buffer size */
#define MAX_INPUT 128

/** Maximum inventory capacity */
#define MAX_INVENTORY 10

/**
 * @brief Room identifiers
 * 
 * Enumeration of all rooms in the game world.
 */
typedef enum {
    ROOM_CAVE_ENTRANCE = 0,  /**< Starting location */
    ROOM_DARK_TUNNEL,         /**< Dark passage requiring light */
    ROOM_TREASURE_ROOM,       /**< Contains the main treasure */
    ROOM_UNDERGROUND_LAKE,    /**< Lake area with key item */
    ROOM_CRYSTAL_CHAMBER,     /**< Chamber with glowing crystals */
    ROOM_EXIT,                /**< Exit location (win condition) */
    ROOM_COUNT                /**< Total number of rooms */
} RoomID;

/**
 * @brief Item identifiers
 * 
 * Enumeration of all collectible items in the game.
 */
typedef enum {
    ITEM_NONE = 0,      /**< No item present */
    ITEM_TORCH,         /**< Provides light in dark areas */
    ITEM_KEY,           /**< Opens locked doors */
    ITEM_SWORD,         /**< Weapon item */
    ITEM_TREASURE,      /**< Main treasure to collect */
    ITEM_CRYSTAL,       /**< Crystal from crystal chamber */
    ITEM_COUNT          /**< Total number of item types */
} ItemID;

/**
 * @brief Room data structure
 * 
 * Contains all information about a single room including
 * its connections, items, and state.
 */
typedef struct {
    RoomID id;                  /**< Unique room identifier */
    const char *name;           /**< Display name of the room */
    const char *description;    /**< Detailed room description */
    RoomID north;               /**< Room to the north (or self if no exit) */
    RoomID south;               /**< Room to the south (or self if no exit) */
    RoomID east;                /**< Room to the east (or self if no exit) */
    RoomID west;                /**< Room to the west (or self if no exit) */
    ItemID item;                /**< Item present in room (ITEM_NONE if empty) */
    bool visited;               /**< True if player has been here before */
    bool locked;                /**< True if room requires key to enter */
} Room;

/**
 * @brief Game state structure
 * 
 * Maintains all game state including player location,
 * inventory, and game flags.
 */
typedef struct {
    RoomID current_room;                /**< Current player location */
    ItemID inventory[MAX_INVENTORY];    /**< Player's inventory */
    int inventory_count;                /**< Number of items in inventory */
    bool has_light;                     /**< True if player has light source */
    bool game_won;                      /**< True if player has won */
    bool running;                       /**< True while game is running */
} GameState;

/* ============================================================================
 * Game Management Functions
 * ========================================================================== */

/**
 * @brief Initialize game state
 * @param game Pointer to game state structure
 */
void init_game(GameState *game);

/**
 * @brief Main game loop
 * 
 * Runs the game loop, processing commands until the game ends.
 */
void game_loop(void);

/**
 * @brief Process a player command
 * @param game Pointer to game state
 * @param command Command string to process
 */
void process_command(GameState *game, const char *command);

/* ============================================================================
 * Game Action Functions
 * ========================================================================== */

/**
 * @brief Display current room description
 * @param game Pointer to game state
 */
void look_around(GameState *game);

/**
 * @brief Display player's inventory
 * @param game Pointer to game state
 */
void show_inventory(const GameState *game);

/**
 * @brief Move player to adjacent room
 * @param game Pointer to game state
 * @param direction Direction to move (as RoomID)
 */
void move_player(GameState *game, RoomID direction);

/**
 * @brief Pick up item from current room
 * @param game Pointer to game state
 */
void take_item(GameState *game);

/**
 * @brief Use an item from inventory
 * @param game Pointer to game state
 * @param item_name Name of item to use
 */
void use_item(GameState *game, const char *item_name);

/* ============================================================================
 * Inventory Management Functions
 * ========================================================================== */

/**
 * @brief Check if player has specific item
 * @param game Pointer to game state
 * @param item Item ID to check for
 * @return true if item is in inventory, false otherwise
 */
bool has_item(const GameState *game, ItemID item);

/**
 * @brief Add item to player's inventory
 * @param game Pointer to game state
 * @param item Item ID to add
 */
void add_item(GameState *game, ItemID item);

/**
 * @brief Remove item from player's inventory
 * @param game Pointer to game state
 * @param item Item ID to remove
 */
void remove_item(GameState *game, ItemID item);

/**
 * @brief Get display name for item
 * @param item Item ID
 * @return String name of the item
 */
const char *get_item_name(ItemID item);

/**
 * @brief Get room structure by ID
 * @param id Room identifier
 * @return Pointer to room structure, or NULL if invalid
 */
Room *get_room(RoomID id);

/* ============================================================================
 * Utility Functions
 * ========================================================================== */

/**
 * @brief Print string to stdout
 * @param str String to print
 */
void print_string(const char *str);

/**
 * @brief Print string with newline
 * @param str String to print
 */
void print_line(const char *str);

/**
 * @brief Clear the screen
 */
void clear_screen(void);

/**
 * @brief Read input from stdin
 * @param buffer Buffer to store input
 * @param max_len Maximum buffer length
 * @return Number of characters read
 */
int read_input(char *buffer, int max_len);

/**
 * @brief Compare two strings
 * @param s1 First string
 * @param s2 Second string
 * @return 0 if equal, <0 if s1 < s2, >0 if s1 > s2
 */
int string_compare(const char *s1, const char *s2);

/**
 * @brief Check if string starts with prefix
 * @param str String to check
 * @param prefix Prefix to look for
 * @return true if str starts with prefix, false otherwise
 */
bool string_starts_with(const char *str, const char *prefix);

#endif // sponk_H
