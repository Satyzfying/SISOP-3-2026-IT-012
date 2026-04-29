#ifndef ARENA_H
#define ARENA_H

#include <sys/types.h>
#include <time.h>

#define SHM_KEY 0x00001234
#define MSG_KEY 0x00005678
#define SEM_KEY 0x00009012

#define MAX_ACCOUNTS 64
#define MAX_USERNAME 32
#define MAX_PASSWORD 32
#define MAX_HISTORY 32
#define MAX_HISTORY_LINE 160
#define BATTLE_LOGS 5
#define MSG_TEXT 2048

#define DEFAULT_GOLD 150
#define DEFAULT_LVL 1
#define DEFAULT_XP 0
#define BASE_DAMAGE 10
#define BASE_HEALTH 100
#define WIN_XP 50
#define LOSE_XP 15
#define WIN_GOLD 120
#define LOSE_GOLD 30
#define MATCH_TIMEOUT 35
#define ATTACK_COOLDOWN 1

#define SERVER_MTYPE 1

typedef enum {
    ACT_REGISTER = 1,
    ACT_LOGIN,
    ACT_LOGOUT,
    ACT_PROFILE,
    ACT_BUY_WEAPON,
    ACT_HISTORY,
    ACT_MATCHMAKE,
    ACT_BATTLE_STATUS,
    ACT_ATTACK,
    ACT_ULTIMATE,
    ACT_CANCEL_MATCH,
    ACT_EXIT
} ActionType;

typedef enum {
    STATUS_OK = 0,
    STATUS_ERR,
    STATUS_WAITING,
    STATUS_BATTLE,
    STATUS_FINISHED
} ResponseStatus;

typedef struct {
    char name[32];
    int bonus_damage;
    int price;
} Weapon;

typedef struct {
    char username[MAX_USERNAME];
    char password[MAX_PASSWORD];
    int gold;
    int lvl;
    int xp;
    int weapon_bonus;
    int active;
    int in_battle;
    pid_t session_pid;
    char history[MAX_HISTORY][MAX_HISTORY_LINE];
    int history_count;
} Warrior;

typedef struct {
    int active;
    int finished;
    int versus_bot;
    char p1[MAX_USERNAME];
    char p2[MAX_USERNAME];
    pid_t pid1;
    pid_t pid2;
    int hp1;
    int hp2;
    int dmg1;
    int dmg2;
    time_t last_attack1;
    time_t last_attack2;
    char logs[BATTLE_LOGS][MAX_HISTORY_LINE];
    int winner;
} BattleState;

typedef struct {
    int server_ready;
    int account_count;
    Warrior accounts[MAX_ACCOUNTS];
    int matchmaking_active;
    char matchmaking_user[MAX_USERNAME];
    pid_t matchmaking_pid;
    time_t matchmaking_since;
    BattleState battle;
} ArenaState;

typedef struct {
    long mtype;
    pid_t pid;
    int action;
    char username[MAX_USERNAME];
    char password[MAX_PASSWORD];
    char text[MSG_TEXT];
} ArenaRequest;

typedef struct {
    long mtype;
    int status;
    int battle_active;
    int battle_finished;
    int self_hp;
    int enemy_hp;
    int self_max_hp;
    int enemy_max_hp;
    int self_damage;
    int enemy_damage;
    int self_lvl;
    int enemy_lvl;
    int self_weapon_bonus;
    int enemy_weapon_bonus;
    int gold;
    int lvl;
    int xp;
    int weapon_bonus;
    char message[MSG_TEXT];
    char enemy[MAX_USERNAME];
    char logs[BATTLE_LOGS][MAX_HISTORY_LINE];
} ArenaResponse;

static const Weapon WEAPONS[] = {
    {"Wood Sword", 5, 100},
    {"Iron Sword", 15, 300},
    {"Steel Axe", 30, 600},
    {"Demon Blade", 60, 1500},
    {"God Slayer", 150, 5000}
};

#define WEAPON_COUNT ((int)(sizeof(WEAPONS) / sizeof(WEAPONS[0])))

#endif
