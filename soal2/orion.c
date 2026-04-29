#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <time.h>
#include <unistd.h>
#include "arena.h"

static int shm_id = -1;
static int msg_id = -1;
static int sem_id = -1;
static ArenaState *arena = NULL;
static volatile sig_atomic_t running = 1;

union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

static void sem_lock(void) {
    struct sembuf op = {0, -1, 0};
    if (semop(sem_id, &op, 1) < 0) {
        perror("semop lock");
        exit(1);
    }
}

static void sem_unlock(void) {
    struct sembuf op = {0, 1, 0};
    if (semop(sem_id, &op, 1) < 0) {
        perror("semop unlock");
        exit(1);
    }
}

static void safe_copy(char *dst, const char *src, size_t size) {
    if (size == 0) return;
    snprintf(dst, size, "%s", src);
}

static int find_account(const char *username) {
    for (int i = 0; i < arena->account_count; i++) {
        if (strcmp(arena->accounts[i].username, username) == 0) return i;
    }
    return -1;
}

static int account_damage(const Warrior *w) {
    return BASE_DAMAGE + (w->xp / 50) + w->weapon_bonus;
}

static int account_health(const Warrior *w) {
    return BASE_HEALTH + (w->xp / 10);
}

static void recalc_level(Warrior *w) {
    w->lvl = DEFAULT_LVL + (w->xp / 100);
}

static void push_history(Warrior *w, const char *line) {
    if (w->history_count < MAX_HISTORY) {
        safe_copy(w->history[w->history_count++], line, MAX_HISTORY_LINE);
        return;
    }
    for (int i = 1; i < MAX_HISTORY; i++) {
        safe_copy(w->history[i - 1], w->history[i], MAX_HISTORY_LINE);
    }
    safe_copy(w->history[MAX_HISTORY - 1], line, MAX_HISTORY_LINE);
}

static void push_battle_log(const char *line) {
    for (int i = BATTLE_LOGS - 1; i > 0; i--) {
        safe_copy(arena->battle.logs[i], arena->battle.logs[i - 1], MAX_HISTORY_LINE);
    }
    safe_copy(arena->battle.logs[0], line, MAX_HISTORY_LINE);
}

static void init_ipc(void) {
    shm_id = shmget(SHM_KEY, sizeof(ArenaState), IPC_CREAT | 0666);
    msg_id = msgget(MSG_KEY, IPC_CREAT | 0666);
    sem_id = semget(SEM_KEY, 1, IPC_CREAT | 0666);
    if (shm_id < 0 || msg_id < 0 || sem_id < 0) {
        perror("ipc");
        exit(1);
    }

    arena = shmat(shm_id, NULL, 0);
    if (arena == (void *)-1) {
        perror("shmat");
        exit(1);
    }

    union semun arg;
    arg.val = 1;
    semctl(sem_id, 0, SETVAL, arg);

    sem_lock();
    if (!arena->server_ready) {
        memset(arena, 0, sizeof(*arena));
    }
    arena->server_ready = 1;
    sem_unlock();
}

static void cleanup_server(void) {
    if (!arena) return;
    sem_lock();
    for (int i = 0; i < arena->account_count; i++) {
        arena->accounts[i].active = 0;
        arena->accounts[i].in_battle = 0;
        arena->accounts[i].session_pid = 0;
    }
    arena->server_ready = 0;
    arena->matchmaking_active = 0;
    memset(&arena->battle, 0, sizeof(arena->battle));
    sem_unlock();
    shmdt(arena);
}

static void handle_signal(int sig) {
    (void)sig;
    running = 0;
}

static void send_response(pid_t pid, ArenaResponse *res) {
    res->mtype = pid;
    if (msgsnd(msg_id, res, sizeof(*res) - sizeof(long), 0) < 0) {
        perror("msgsnd response");
    }
}

static void fill_profile(ArenaResponse *res, const Warrior *w) {
    res->gold = w->gold;
    res->lvl = w->lvl;
    res->xp = w->xp;
    res->weapon_bonus = w->weapon_bonus;
}

static int is_battle_participant(const char *username) {
    BattleState *b = &arena->battle;
    return strcmp(username, b->p1) == 0 || strcmp(username, b->p2) == 0;
}

static void clear_finished_battle_for(const char *username) {
    if (arena->battle.finished && is_battle_participant(username)) {
        memset(&arena->battle, 0, sizeof(arena->battle));
    }
}

static void fill_battle_response(ArenaResponse *res, const char *username) {
    BattleState *b = &arena->battle;
    int self_is_p1 = strcmp(username, b->p1) == 0;
    int i1 = find_account(b->p1);
    int i2 = b->versus_bot ? -1 : find_account(b->p2);

    res->battle_active = b->active;
    res->battle_finished = b->finished;
    res->self_hp = self_is_p1 ? b->hp1 : b->hp2;
    res->enemy_hp = self_is_p1 ? b->hp2 : b->hp1;
    res->self_damage = self_is_p1 ? b->dmg1 : b->dmg2;
    res->enemy_damage = self_is_p1 ? b->dmg2 : b->dmg1;
    res->self_max_hp = self_is_p1
        ? (i1 >= 0 ? account_health(&arena->accounts[i1]) : BASE_HEALTH)
        : (i2 >= 0 ? account_health(&arena->accounts[i2]) : 90);
    res->enemy_max_hp = self_is_p1
        ? (i2 >= 0 ? account_health(&arena->accounts[i2]) : 90)
        : (i1 >= 0 ? account_health(&arena->accounts[i1]) : BASE_HEALTH);
    res->self_lvl = self_is_p1
        ? (i1 >= 0 ? arena->accounts[i1].lvl : 1)
        : (i2 >= 0 ? arena->accounts[i2].lvl : 1);
    res->enemy_lvl = self_is_p1
        ? (i2 >= 0 ? arena->accounts[i2].lvl : 1)
        : (i1 >= 0 ? arena->accounts[i1].lvl : 1);
    res->self_weapon_bonus = self_is_p1
        ? (i1 >= 0 ? arena->accounts[i1].weapon_bonus : 0)
        : (i2 >= 0 ? arena->accounts[i2].weapon_bonus : 0);
    res->enemy_weapon_bonus = self_is_p1
        ? (i2 >= 0 ? arena->accounts[i2].weapon_bonus : 0)
        : (i1 >= 0 ? arena->accounts[i1].weapon_bonus : 0);
    safe_copy(res->enemy, self_is_p1 ? b->p2 : b->p1, sizeof(res->enemy));
    for (int i = 0; i < BATTLE_LOGS; i++) {
        safe_copy(res->logs[i], b->logs[i], MAX_HISTORY_LINE);
    }
}

static void start_battle(const char *p1, pid_t pid1, const char *p2, pid_t pid2, int bot) {
    int i1 = find_account(p1);
    int i2 = bot ? -1 : find_account(p2);
    if (i1 < 0) return;

    BattleState *b = &arena->battle;
    memset(b, 0, sizeof(*b));
    b->active = 1;
    b->versus_bot = bot;
    safe_copy(b->p1, p1, sizeof(b->p1));
    safe_copy(b->p2, p2, sizeof(b->p2));
    b->pid1 = pid1;
    b->pid2 = pid2;
    b->hp1 = account_health(&arena->accounts[i1]);
    b->dmg1 = account_damage(&arena->accounts[i1]);

    if (bot) {
        b->hp2 = 90;
        b->dmg2 = 8;
    } else if (i2 >= 0) {
        b->hp2 = account_health(&arena->accounts[i2]);
        b->dmg2 = account_damage(&arena->accounts[i2]);
        arena->accounts[i2].in_battle = 1;
    }

    arena->accounts[i1].in_battle = 1;
    push_battle_log("Battle started.");
}

static void finish_battle(int winner) {
    BattleState *b = &arena->battle;
    if (!b->active || b->finished) return;

    b->finished = 1;
    b->active = 0;
    b->winner = winner;
    char line[MAX_HISTORY_LINE];
    snprintf(line, sizeof(line), "%s wins the battle.", winner == 1 ? b->p1 : b->p2);
    push_battle_log(line);

    int i1 = find_account(b->p1);
    int i2 = b->versus_bot ? -1 : find_account(b->p2);

    if (i1 >= 0) {
        int won = winner == 1;
        arena->accounts[i1].xp += won ? WIN_XP : LOSE_XP;
        arena->accounts[i1].gold += won ? WIN_GOLD : LOSE_GOLD;
        recalc_level(&arena->accounts[i1]);
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        char match_time[8];
        strftime(match_time, sizeof(match_time), "%H:%M", t);
        snprintf(line, sizeof(line), "%s|%s|%s|+%d",
                 match_time, b->p2, won ? "WIN" : "LOSS", won ? WIN_XP : LOSE_XP);
        push_history(&arena->accounts[i1], line);
        arena->accounts[i1].in_battle = 0;
    }

    if (i2 >= 0) {
        int won = winner == 2;
        arena->accounts[i2].xp += won ? WIN_XP : LOSE_XP;
        arena->accounts[i2].gold += won ? WIN_GOLD : LOSE_GOLD;
        recalc_level(&arena->accounts[i2]);
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        char match_time[8];
        strftime(match_time, sizeof(match_time), "%H:%M", t);
        snprintf(line, sizeof(line), "%s|%s|%s|+%d",
                 match_time, b->p1, won ? "WIN" : "LOSS", won ? WIN_XP : LOSE_XP);
        push_history(&arena->accounts[i2], line);
        arena->accounts[i2].in_battle = 0;
    }
}

static void maybe_start_bot_match(void) {
    if (!arena->matchmaking_active || arena->battle.active) return;
    if (time(NULL) - arena->matchmaking_since >= MATCH_TIMEOUT) {
        start_battle(arena->matchmaking_user, arena->matchmaking_pid, "Monster", 0, 1);
        arena->matchmaking_active = 0;
    }
}

static void bot_tick(void) {
    BattleState *b = &arena->battle;
    if (!b->active || b->finished || !b->versus_bot) return;

    time_t now = time(NULL);
    if (now - b->last_attack2 < ATTACK_COOLDOWN + 1) return;
    b->last_attack2 = now;
    b->hp1 -= b->dmg2;

    char line[MAX_HISTORY_LINE];
    snprintf(line, sizeof(line), "Monster attacks %s for %d damage.", b->p1, b->dmg2);
    push_battle_log(line);
    if (b->hp1 <= 0) finish_battle(2);
}

static int require_login(const ArenaRequest *req, ArenaResponse *res) {
    int idx = find_account(req->username);
    if (idx < 0 || !arena->accounts[idx].active || arena->accounts[idx].session_pid != req->pid) {
        res->status = STATUS_ERR;
        safe_copy(res->message, "You must login first.", sizeof(res->message));
        return -1;
    }
    return idx;
}

static void reset_stale_session(Warrior *w) {
    if (w->active && kill(w->session_pid, 0) < 0 && errno == ESRCH) {
        w->active = 0;
        w->in_battle = 0;
        w->session_pid = 0;
    }
}

static void handle_request(const ArenaRequest *req) {
    ArenaResponse res;
    memset(&res, 0, sizeof(res));
    res.status = STATUS_OK;

    sem_lock();
    maybe_start_bot_match();
    bot_tick();

    int idx = -1;
    switch (req->action) {
        case ACT_REGISTER:
            if (find_account(req->username) >= 0) {
                res.status = STATUS_ERR;
                safe_copy(res.message, "Username already registered.", sizeof(res.message));
            } else if (arena->account_count >= MAX_ACCOUNTS) {
                res.status = STATUS_ERR;
                safe_copy(res.message, "Arena account storage is full.", sizeof(res.message));
            } else {
                Warrior *w = &arena->accounts[arena->account_count++];
                memset(w, 0, sizeof(*w));
                safe_copy(w->username, req->username, sizeof(w->username));
                safe_copy(w->password, req->password, sizeof(w->password));
                w->gold = DEFAULT_GOLD;
                w->lvl = DEFAULT_LVL;
                w->xp = DEFAULT_XP;
                safe_copy(res.message, "Register success.", sizeof(res.message));
            }
            break;
        case ACT_LOGIN:
            idx = find_account(req->username);
            if (idx < 0 || strcmp(arena->accounts[idx].password, req->password) != 0) {
                res.status = STATUS_ERR;
                safe_copy(res.message, "Invalid username or password.", sizeof(res.message));
            } else {
                reset_stale_session(&arena->accounts[idx]);
                if (arena->accounts[idx].active) {
                    res.status = STATUS_ERR;
                    safe_copy(res.message, "Account is already active in another session.", sizeof(res.message));
                } else {
                    arena->accounts[idx].active = 1;
                    arena->accounts[idx].session_pid = req->pid;
                    fill_profile(&res, &arena->accounts[idx]);
                    safe_copy(res.message, "Login success.", sizeof(res.message));
                }
            }
            break;
        case ACT_CANCEL_MATCH:
            idx = require_login(req, &res);
            if (idx >= 0) {
                if (arena->matchmaking_active && arena->matchmaking_pid == req->pid) {
                    arena->matchmaking_active = 0;
                    arena->matchmaking_user[0] = '\0';
                    arena->matchmaking_pid = 0;
                    safe_copy(res.message, "Matchmaking cancelled.", sizeof(res.message));
                } else {
                    safe_copy(res.message, "No matchmaking to cancel.", sizeof(res.message));
                }
            }
            break;
        case ACT_LOGOUT:
        case ACT_EXIT:
            idx = require_login(req, &res);
            if (idx >= 0) {
                arena->accounts[idx].active = 0;
                arena->accounts[idx].in_battle = 0;
                arena->accounts[idx].session_pid = 0;
                if (arena->matchmaking_active && arena->matchmaking_pid == req->pid) {
                    arena->matchmaking_active = 0;
                }
                safe_copy(res.message, "Logged out.", sizeof(res.message));
            }
            break;
        case ACT_PROFILE:
            idx = require_login(req, &res);
            if (idx >= 0) {
                fill_profile(&res, &arena->accounts[idx]);
                safe_copy(res.message, "Profile loaded.", sizeof(res.message));
            }
            break;
        case ACT_BUY_WEAPON:
            idx = require_login(req, &res);
            if (idx >= 0) {
                int choice = atoi(req->text);
                if (choice < 1 || choice > WEAPON_COUNT) {
                    res.status = STATUS_ERR;
                    safe_copy(res.message, "Invalid weapon choice.", sizeof(res.message));
                } else {
                    const Weapon *weapon = &WEAPONS[choice - 1];
                    if (arena->accounts[idx].gold < weapon->price) {
                        res.status = STATUS_ERR;
                        safe_copy(res.message, "Not enough gold.", sizeof(res.message));
                    } else {
                        arena->accounts[idx].gold -= weapon->price;
                        if (weapon->bonus_damage > arena->accounts[idx].weapon_bonus) {
                            arena->accounts[idx].weapon_bonus = weapon->bonus_damage;
                        }
                        fill_profile(&res, &arena->accounts[idx]);
                        snprintf(res.message, sizeof(res.message), "Bought %s.", weapon->name);
                    }
                }
            }
            break;
        case ACT_HISTORY:
            idx = require_login(req, &res);
            if (idx >= 0) {
                res.message[0] = '\0';
                for (int i = 0; i < arena->accounts[idx].history_count; i++) {
                    strncat(res.message, arena->accounts[idx].history[i],
                            sizeof(res.message) - strlen(res.message) - 1);
                    if (i + 1 < arena->accounts[idx].history_count) {
                        strncat(res.message, "\n", sizeof(res.message) - strlen(res.message) - 1);
                    }
                }
                if (arena->accounts[idx].history_count == 0) {
                    safe_copy(res.message, "No battle history yet.", sizeof(res.message));
                }
            }
            break;
        case ACT_MATCHMAKE:
            idx = require_login(req, &res);
            if (idx >= 0) {
                clear_finished_battle_for(req->username);
                if (arena->accounts[idx].in_battle) {
                    res.status = STATUS_BATTLE;
                    fill_battle_response(&res, req->username);
                } else if (arena->battle.active && !arena->battle.finished) {
                    res.status = STATUS_WAITING;
                    safe_copy(res.message, "Arena is busy. Try matchmaking again soon.", sizeof(res.message));
                } else if (arena->matchmaking_active && arena->matchmaking_pid != req->pid) {
                    start_battle(arena->matchmaking_user, arena->matchmaking_pid, req->username, req->pid, 0);
                    arena->matchmaking_active = 0;
                    res.status = STATUS_BATTLE;
                    fill_battle_response(&res, req->username);
                } else {
                    arena->matchmaking_active = 1;
                    safe_copy(arena->matchmaking_user, req->username, sizeof(arena->matchmaking_user));
                    arena->matchmaking_pid = req->pid;
                    arena->matchmaking_since = time(NULL);
                    res.status = STATUS_WAITING;
                    safe_copy(res.message, "Searching opponent for 35 seconds...", sizeof(res.message));
                }
            }
            break;
        case ACT_BATTLE_STATUS:
            idx = require_login(req, &res);
            if (idx >= 0) {
                maybe_start_bot_match();
                bot_tick();
                if (arena->matchmaking_active && arena->matchmaking_pid == req->pid) {
                    res.status = STATUS_WAITING;
                    snprintf(res.message, sizeof(res.message), "Searching... %ld/%d seconds",
                             (long)(time(NULL) - arena->matchmaking_since), MATCH_TIMEOUT);
                } else if (arena->accounts[idx].in_battle ||
                           (arena->battle.finished && is_battle_participant(req->username))) {
                    res.status = arena->battle.finished ? STATUS_FINISHED : STATUS_BATTLE;
                    fill_battle_response(&res, req->username);
                } else {
                    res.status = STATUS_OK;
                    safe_copy(res.message, "Not in matchmaking or battle.", sizeof(res.message));
                }
            }
            break;
        case ACT_ATTACK:
        case ACT_ULTIMATE:
            idx = require_login(req, &res);
            if (idx >= 0 && arena->battle.active && !arena->battle.finished &&
                is_battle_participant(req->username)) {
                BattleState *b = &arena->battle;
                int self_is_p1 = strcmp(req->username, b->p1) == 0;
                int *enemy_hp = self_is_p1 ? &b->hp2 : &b->hp1;
                int damage = self_is_p1 ? b->dmg1 : b->dmg2;
                time_t *last_attack = self_is_p1 ? &b->last_attack1 : &b->last_attack2;
                time_t now = time(NULL);

                if (now - *last_attack < ATTACK_COOLDOWN) {
                    res.status = STATUS_ERR;
                    safe_copy(res.message, "Attack is still on cooldown.", sizeof(res.message));
                } else if (req->action == ACT_ULTIMATE && arena->accounts[idx].weapon_bonus <= 0) {
                    res.status = STATUS_ERR;
                    safe_copy(res.message, "Ultimate requires a weapon.", sizeof(res.message));
                } else {
                    if (req->action == ACT_ULTIMATE) damage *= 3;
                    *last_attack = now;
                    *enemy_hp -= damage;

                    char line[MAX_HISTORY_LINE];
                    snprintf(line, sizeof(line), "%s uses %s for %d damage.",
                             req->username, req->action == ACT_ULTIMATE ? "Ultimate" : "Attack", damage);
                    push_battle_log(line);

                    if (*enemy_hp <= 0) finish_battle(self_is_p1 ? 1 : 2);
                    res.status = arena->battle.finished ? STATUS_FINISHED : STATUS_BATTLE;
                    fill_battle_response(&res, req->username);
                }
            } else if (idx >= 0) {
                res.status = STATUS_ERR;
                safe_copy(res.message, "You are not in an active battle.", sizeof(res.message));
            }
            break;
        default:
            res.status = STATUS_ERR;
            safe_copy(res.message, "Unknown action.", sizeof(res.message));
            break;
    }

    sem_unlock();
    send_response(req->pid, &res);
}

int main(void) {
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    init_ipc();

    printf("Orion is ready. Waiting for Eternal warriors...\n");

    while (running) {
        ArenaRequest req;
        ssize_t got = msgrcv(msg_id, &req, sizeof(req) - sizeof(long), SERVER_MTYPE, IPC_NOWAIT);
        if (got >= 0) {
            handle_request(&req);
            continue;
        }

        if (errno != ENOMSG && errno != EINTR) {
            perror("msgrcv");
            break;
        }

        sem_lock();
        maybe_start_bot_match();
        bot_tick();
        sem_unlock();
        usleep(100000);
    }

    printf("\nOrion is shutting down.\n");
    cleanup_server();
    return 0;
}
