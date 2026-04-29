#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/select.h>
#include <sys/shm.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include "arena.h"

#define CLR_RESET "\033[0m"
#define CLR_PINK "\033[95m"
#define CLR_RED "\033[91m"
#define CLR_GREEN "\033[92m"
#define CLR_CYAN "\033[96m"
#define CLR_YELLOW "\033[93m"

static int msg_id = -1;
static int shm_id = -1;
static ArenaState *arena = NULL;
static char current_user[MAX_USERNAME] = "";
static int logged_in = 0;
static struct termios original_term;
static int raw_enabled = 0;

static void trim_newline(char *s) {
    s[strcspn(s, "\n")] = '\0';
}

static void read_input(const char *prompt, char *buf, size_t size) {
    printf("%s", prompt);
    fflush(stdout);
    if (!fgets(buf, size, stdin)) {
        buf[0] = '\0';
        return;
    }
    trim_newline(buf);
}

static void enable_raw(void) {
    if (raw_enabled) return;
    tcgetattr(STDIN_FILENO, &original_term);
    struct termios raw = original_term;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    raw_enabled = 1;
}

static void disable_raw(void) {
    if (!raw_enabled) return;
    tcsetattr(STDIN_FILENO, TCSANOW, &original_term);
    raw_enabled = 0;
}

static int connect_orion(void) {
    msg_id = msgget(MSG_KEY, 0666);
    shm_id = shmget(SHM_KEY, sizeof(ArenaState), 0666);
    if (msg_id < 0 || shm_id < 0) {
        printf("[System] Orion is not ready. Start ./orion first.\n");
        return -1;
    }

    arena = shmat(shm_id, NULL, 0);
    if (arena == (void *)-1) {
        perror("shmat");
        return -1;
    }

    if (!arena->server_ready) {
        printf("[System] Orion is not accepting communication right now.\n");
        return -1;
    }

    return 0;
}

static int request_server(int action, const char *username, const char *password,
                          const char *text, ArenaResponse *res) {
    ArenaRequest req;
    memset(&req, 0, sizeof(req));
    req.mtype = SERVER_MTYPE;
    req.pid = getpid();
    req.action = action;
    snprintf(req.username, sizeof(req.username), "%s", username ? username : "");
    snprintf(req.password, sizeof(req.password), "%s", password ? password : "");
    snprintf(req.text, sizeof(req.text), "%s", text ? text : "");

    if (msgsnd(msg_id, &req, sizeof(req) - sizeof(long), 0) < 0) {
        perror("msgsnd");
        return -1;
    }

    if (msgrcv(msg_id, res, sizeof(*res) - sizeof(long), getpid(), 0) < 0) {
        perror("msgrcv");
        return -1;
    }

    return 0;
}

static void cleanup_client(void) {
    disable_raw();
    if (logged_in) {
        ArenaResponse res;
        request_server(ACT_EXIT, current_user, "", "", &res);
        logged_in = 0;
    }
    if (arena && arena != (void *)-1) shmdt(arena);
}

static void handle_sigint(int sig) {
    (void)sig;
    cleanup_client();
    printf("\n[System] Leaving Battle of Eterion...\n");
    _exit(0);
}

static void show_profile(const ArenaResponse *res) {
    printf("\n%s=== WARRIOR PROFILE ===%s\n", CLR_PINK, CLR_RESET);
    printf("Gold   : %d\n", res->gold);
    printf("Lvl    : %d\n", res->lvl);
    printf("XP     : %d\n", res->xp);
    printf("Weapon : +%d Dmg\n", res->weapon_bonus);
}

static void register_menu(void) {
    char username[MAX_USERNAME];
    char password[MAX_PASSWORD];
    ArenaResponse res;

    read_input("Username: ", username, sizeof(username));
    read_input("Password: ", password, sizeof(password));
    if (username[0] == '\0' || password[0] == '\0') {
        printf("[System] Username and password cannot be empty.\n");
        return;
    }

    if (request_server(ACT_REGISTER, username, password, "", &res) == 0) {
        printf("[System] %s\n", res.message);
    }
}

static int login_menu(void) {
    char username[MAX_USERNAME];
    char password[MAX_PASSWORD];
    ArenaResponse res;

    read_input("Username: ", username, sizeof(username));
    read_input("Password: ", password, sizeof(password));
    if (request_server(ACT_LOGIN, username, password, "", &res) < 0) return 0;

    if (res.status != STATUS_OK) {
        printf("[System] %s\n", res.message);
        return 0;
    }

    snprintf(current_user, sizeof(current_user), "%s", username);
    logged_in = 1;
    printf("[System] %s Welcome, %s.\n", res.message, current_user);
    show_profile(&res);
    return 1;
}

static void armory_menu(void) {
    ArenaResponse profile;
    int gold = 0;
    if (request_server(ACT_PROFILE, current_user, "", "", &profile) == 0 && profile.status == STATUS_OK) {
        gold = profile.gold;
    }

    printf("\033[2J\033[H");
    printf("%s=== ARMORY ===%s\n", CLR_YELLOW, CLR_RESET);
    printf("Gold: %d\n", gold);
    for (int i = 0; i < WEAPON_COUNT; i++) {
        printf("%d. %-14s | %5d G | +%-3d Dmg\n",
               i + 1, WEAPONS[i].name, WEAPONS[i].price, WEAPONS[i].bonus_damage);
    }
    printf("0. Back | Choice: ");

    char choice[16];
    if (!fgets(choice, sizeof(choice), stdin)) return;
    trim_newline(choice);
    if (strcmp(choice, "0") == 0) return;

    ArenaResponse res;
    if (request_server(ACT_BUY_WEAPON, current_user, "", choice, &res) == 0) {
        printf("[System] %s\n", res.message);
        if (res.status == STATUS_OK) show_profile(&res);
    }
}

static void history_menu(void) {
    ArenaResponse res;
    if (request_server(ACT_HISTORY, current_user, "", "", &res) == 0) {
        printf("\033[2J\033[H");
        printf("%s=== MATCH HISTORY ===%s\n\n", CLR_PINK, CLR_RESET);
        printf("+----------+------------------+--------+--------+\n");
        printf("| Time     | Opponent         | Res    | XP     |\n");
        printf("+----------+------------------+--------+--------+\n");

        if (strcmp(res.message, "No battle history yet.") == 0) {
            printf("| %-42s |\n", "No battle history yet.");
        } else {
            char copy[MSG_TEXT];
            snprintf(copy, sizeof(copy), "%s", res.message);
            char *line_save = NULL;
            char *line = strtok_r(copy, "\n", &line_save);
            while (line) {
                char parsed[MAX_HISTORY_LINE];
                snprintf(parsed, sizeof(parsed), "%s", line);
                char *field_save = NULL;
                char *time_part = strtok_r(parsed, "|", &field_save);
                char *opponent = strtok_r(NULL, "|", &field_save);
                char *result = strtok_r(NULL, "|", &field_save);
                char *xp = strtok_r(NULL, "|", &field_save);

                if (time_part && opponent && result && xp) {
                    const char *color = strcmp(result, "WIN") == 0 ? CLR_GREEN : CLR_RED;
                    printf("| %-8s | %-16s | %s%-6s%s | %-6s |\n",
                           time_part, opponent, color, result, CLR_RESET, xp);
                } else {
                    printf("| %-42s |\n", line);
                }
                line = strtok_r(NULL, "\n", &line_save);
            }
        }

        printf("+----------+------------------+--------+--------+\n");
        printf("\nPress ENTER to continue...");
        char wait[8];
        fgets(wait, sizeof(wait), stdin);
    }
}

static void profile_menu(void) {
    ArenaResponse res;
    if (request_server(ACT_PROFILE, current_user, "", "", &res) == 0) {
        if (res.status == STATUS_OK) show_profile(&res);
        else printf("[System] %s\n", res.message);
    }
}

static void print_hp_bar(int hp, int max_hp, const char *color) {
    int width = 24;
    if (max_hp <= 0) max_hp = 1;
    if (hp < 0) hp = 0;
    int filled = (hp * width) / max_hp;
    if (filled > width) filled = width;

    printf("[");
    printf("%s", color);
    for (int i = 0; i < width; i++) {
        putchar(i < filled ? '#' : ' ');
    }
    printf("%s", CLR_RESET);
    printf("] %d/%d", hp, max_hp);
}

static void render_battle(const ArenaResponse *res) {
    printf("\033[2J\033[H");
    printf("%s+-------------------------- ARENA --------------------------+%s\n", CLR_RED, CLR_RESET);
    printf("%s%-16s%s Lvl %-2d | Weapon: +%d Dmg\n",
           CLR_CYAN, current_user, CLR_RESET, res->self_lvl, res->self_weapon_bonus);
    print_hp_bar(res->self_hp, res->self_max_hp, CLR_GREEN);
    printf("\n\n%sVS%s\n\n", CLR_PINK, CLR_RESET);
    printf("%s%-16s%s Lvl %-2d | Weapon: +%d Dmg\n",
           CLR_CYAN, res->enemy, CLR_RESET, res->enemy_lvl, res->enemy_weapon_bonus);
    print_hp_bar(res->enemy_hp, res->enemy_max_hp, CLR_RED);
    printf("\n\n%sCombat Log:%s\n", CLR_YELLOW, CLR_RESET);
    for (int i = 0; i < BATTLE_LOGS; i++) {
        printf("> %s\n", res->logs[i][0] != '\0' ? res->logs[i] : "");
    }
    printf("\n%sCD:%s Atk(%.1fs) | Ult(%.1fs)\n", CLR_CYAN, CLR_RESET, 0.0, 0.0);
    printf("%s+-----------------------------------------------------------+%s\n", CLR_RED, CLR_RESET);
    printf("Press 'a' to attack, 'u' for ultimate, 'q' to stop watching.\n");
    if (res->battle_finished) {
        int won = res->self_hp > 0 && res->enemy_hp <= 0;
        printf("\n%s=== %s ===%s\n", won ? CLR_GREEN : CLR_RED, won ? "VICTORY" : "DEFEAT", CLR_RESET);
        printf("Battle ended. Press any key to continue...");
    }
    fflush(stdout);
}

static void battle_loop(void) {
    ArenaResponse res;
    if (request_server(ACT_MATCHMAKE, current_user, "", "", &res) < 0) return;
    printf("[System] %s\n", res.message);

    enable_raw();
    int done = 0;
    int in_matchmaking = 1;
    time_t last_status = 0;

    while (!done) {
        time_t now = time(NULL);
        if (now != last_status) {
            last_status = now;
            if (request_server(ACT_BATTLE_STATUS, current_user, "", "", &res) < 0) break;

            if (res.status == STATUS_WAITING) {
                in_matchmaking = 1;
                printf("\r[System] %-50s", res.message);
                fflush(stdout);
            } else if (res.status == STATUS_BATTLE || res.status == STATUS_FINISHED) {
                in_matchmaking = 0;
                render_battle(&res);
                if (res.status == STATUS_FINISHED) {
                    char c;
                    read(STDIN_FILENO, &c, 1);
                    done = 1;
                }
            } else if (res.status == STATUS_OK || res.status == STATUS_ERR) {
                printf("\n[System] %s\n", res.message);
                done = 1;
            }
        }

        fd_set readfds;
        struct timeval tv;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        tv.tv_sec = 0;
        tv.tv_usec = 100000;

        if (select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv) > 0) {
            char c;
            if (read(STDIN_FILENO, &c, 1) <= 0) continue;
            if (c == 'q') {
                if (in_matchmaking) {
                    request_server(ACT_CANCEL_MATCH, current_user, "", "", &res);
                }
                done = 1;
            } else if (c == 'a' || c == 'u') {
                int action = c == 'a' ? ACT_ATTACK : ACT_ULTIMATE;
                if (request_server(action, current_user, "", "", &res) == 0) {
                    if (res.status == STATUS_ERR) {
                        printf("\n[System] %s\n", res.message);
                    } else {
                        render_battle(&res);
                    }
                }
            }
        }
    }

    disable_raw();
    printf("\n");
}

static void world_menu(void) {
    while (logged_in) {
        printf("\n=== Battle of Eterion ===\n");
        printf("1. Profile\n");
        printf("2. Battle\n");
        printf("3. Armory\n");
        printf("4. Battle History\n");
        printf("5. Logout\n");
        printf("Choose: ");

        char choice[16];
        if (!fgets(choice, sizeof(choice), stdin)) break;
        int selected = atoi(choice);

        if (selected == 1) {
            profile_menu();
        } else if (selected == 2) {
            battle_loop();
        } else if (selected == 3) {
            armory_menu();
        } else if (selected == 4) {
            history_menu();
        } else if (selected == 5) {
            ArenaResponse res;
            request_server(ACT_LOGOUT, current_user, "", "", &res);
            printf("[System] %s\n", res.message);
            logged_in = 0;
            current_user[0] = '\0';
        } else {
            printf("[System] Invalid choice.\n");
        }
    }
}

int main(void) {
    signal(SIGINT, handle_sigint);
    if (connect_orion() < 0) return 1;

    while (1) {
        printf("\n=== Eternal Gate ===\n");
        printf("1. Register\n");
        printf("2. Login\n");
        printf("3. Exit\n");
        printf("Choose: ");

        char choice[16];
        if (!fgets(choice, sizeof(choice), stdin)) break;
        int selected = atoi(choice);

        if (selected == 1) {
            register_menu();
        } else if (selected == 2) {
            if (login_menu()) world_menu();
        } else if (selected == 3) {
            break;
        } else {
            printf("[System] Invalid choice.\n");
        }
    }

    cleanup_client();
    printf("[System] Leaving Battle of Eterion...\n");
    return 0;
}
