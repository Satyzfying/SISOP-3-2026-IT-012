Nama: Gede Satya Putra Aryanta

NRP: 5027251012

---

# Soal 1 - The Wired

## Deskripsi Soal

Pada soal ini, aku membuat sistem komunikasi client-server bertema NAVI dan The Wired. Intinya, ada server pusat yang bisa menerima banyak client, lalu setiap client bisa mengirim dan menerima pesan. Selain chat biasa, program juga punya validasi nama unik, broadcast pesan, fitur admin lewat RPC, dan pencatatan aktivitas ke file `history.log`.

File yang dipakai:

- `soal1/protocol.h`: berisi konfigurasi protocol, port, alamat IP, struktur data client, struktur paket pesan, dan password admin.
- `soal1/wired.c`: program server The Wired.
- `soal1/navi.c`: program client NAVI.

## Cara Menjalankan Program

Compile server:

```bash
gcc soal1/wired.c -o wired
```

Compile client:

```bash
gcc soal1/navi.c -o navi -pthread
```

Jalankan server terlebih dahulu:

```bash
./wired
```

Lalu jalankan client di terminal lain:

```bash
./navi
```

Kalau ingin masuk sebagai admin, gunakan nama:

```text
The Knights
```

Password admin yang dipakai:

```text
admin123
```

Untuk keluar dari client biasa:

```text
/exit
```

## Struktur Data dan Protocol

Konfigurasi utama program aku taruh di `protocol.h`, supaya server dan client memakai aturan yang sama.

```c
#define PORT 8080
#define IP_ADDRESS "127.0.0.1"
#define MAX_CLIENTS 100
#define MAX_NAME_LEN 50
#define BUFFER_SIZE 1024
```

Ada dua struktur utama yang dipakai pada program ini.

```c
typedef struct {
    int socket_fd;
    char username[MAX_NAME_LEN];
    int is_admin;
} NaviIdentity;
```

`NaviIdentity` dipakai saat client pertama kali mendaftar ke server. Isinya adalah socket client, username, dan status apakah client tersebut admin atau bukan.

```c
typedef struct {
    char sender[MAX_NAME_LEN];
    char content[BUFFER_SIZE];
    int is_rpc;
} WiredPacket;
```

`WiredPacket` dipakai untuk pengiriman pesan. Field `is_rpc` digunakan untuk membedakan pesan chat biasa dan request RPC admin.

## Cara Pengerjaan

### 1. Koneksi Stabil NAVI ke Server

**Instruksi soal:** NAVI harus bisa terdaftar ke jaringan pusat melalui alamat dan port yang ditentukan di file protocol, tanpa mengganggu pengguna lain yang sudah terhubung.

**Implementasi pada kode:** alamat IP dan port aku simpan di `soal1/protocol.h`, sehingga `wired.c` dan `navi.c` memakai konfigurasi yang sama.

```c
#define PORT 8080
#define IP_ADDRESS "127.0.0.1"
```

Di `soal1/wired.c`, server membuat socket TCP, mengaktifkan `SO_REUSEADDR`, melakukan `bind()` ke port `8080`, lalu menjalankan `listen()` supaya siap menerima koneksi dari client.

```c
master_sock = socket(AF_INET, SOCK_STREAM, 0);
setsockopt(master_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
bind(master_sock, (struct sockaddr *)&address, sizeof(address));
listen(master_sock, 10);
```

Di `soal1/navi.c`, client mengambil alamat dan port dari `protocol.h`, lalu terhubung ke server dengan `connect()`.

```c
server.sin_addr.s_addr = inet_addr(IP_ADDRESS);
server.sin_family = AF_INET;
server.sin_port = htons(PORT);
connect(sock, (struct sockaddr *)&server, sizeof(server));
```

Koneksi baru tidak membuat client lain berhenti, karena server memantau koneksi dengan `select()`. Jika ada koneksi baru, `master_sock` akan terdeteksi aktif dan server menjalankan `accept()`.

### 2. Client Berjalan Asinkron Tanpa Fork

**Instruksi soal:** NAVI harus bisa melakukan dua hal secara asinkron, yaitu menerima transmisi dari The Wired dan mengirim input pengguna, tanpa menggunakan `fork()`.

**Implementasi pada kode:** di `soal1/navi.c`, aku menggunakan `pthread`. Jadi client tidak membuat proses baru dengan `fork()`, melainkan membuat thread tambahan untuk menerima pesan.

```c
pthread_create(&thread_id, NULL, receive_handler, (void*)&sock);
```

Fungsi `receive_handler()` terus menunggu pesan dari server menggunakan `recv()`.

```c
if (recv(sock, &pkg, sizeof(pkg), 0) <= 0) break;
printf("\n[%s]: %s\n> ", pkg.sender, pkg.content);
```

Sementara itu, thread utama tetap membaca input pengguna lalu mengirimkannya ke server.

```c
scanf(" %1023[^\n]", buf);
strcpy(pkg.content, buf);
send(sock, &pkg, sizeof(WiredPacket), 0);
```

Dengan cara ini, client tetap bisa menerima pesan broadcast walaupun pengguna sedang mengetik.

### 3. Server Menangani Banyak Client dengan `select()`

**Instruksi soal:** server The Wired harus bisa menangani banyak client, tidak terhambat oleh satu pengguna yang lambat, bisa membedakan koneksi baru dan pesan masuk, serta menangani diskoneksi client lewat `/exit` maupun interrupt signal.

**Implementasi pada kode:** di `soal1/wired.c`, server memakai `select()` untuk memantau banyak socket dalam satu loop. Socket server utama dan semua socket client aktif dimasukkan ke `fd_set`.

```c
FD_ZERO(&readfds);
FD_SET(master_sock, &readfds);
select(max_sd + 1, &readfds, NULL, NULL, NULL);
```

Kalau `master_sock` aktif, berarti ada client baru yang ingin masuk. Server kemudian menerima koneksi tersebut dengan `accept()`.

```c
if (FD_ISSET(master_sock, &readfds)) {
    new_sock = accept(master_sock, NULL, NULL);
}
```

Kalau socket client yang aktif, berarti client tersebut mengirim pesan atau terputus. Server membaca paketnya dengan `recv()`.

```c
int valread = recv(sd, &pkg, sizeof(pkg), 0);
```

Diskoneksi ditangani saat `recv()` menghasilkan nilai `<= 0`, atau saat client mengirim command `/exit`.

```c
if (valread <= 0 || strcmp(pkg.content, "/exit") == 0) {
    close(sd);
    clients[i].socket_fd = 0;
}
```

Untuk interrupt signal dari sisi client, `soal1/navi.c` memiliki handler `SIGINT`. Saat pengguna menekan `Ctrl+C`, client menutup socket dan keluar dengan lebih rapi.

```c
signal(SIGINT, handle_sigint);
if (active_sock >= 0) close(active_sock);
```

Output dari server dikirim kembali ke client menggunakan `send()`, baik untuk balasan RPC maupun broadcast chat.

### 4. Identitas Unik NAVI

**Instruksi soal:** setiap entitas yang masuk ke The Wired harus memiliki identitas digital berupa nama. Nama tersebut harus unik, sehingga tidak boleh ada dua client aktif dengan nama yang sama.

**Implementasi pada kode:** setelah koneksi berhasil, `soal1/navi.c` meminta pengguna memasukkan nama, lalu mengirimkan data tersebut ke server dalam bentuk `NaviIdentity`.

```c
printf("Enter your name: ");
scanf(" %49[^\n]", me.username);
send(sock, &me, sizeof(NaviIdentity), 0);
```

Di `soal1/wired.c`, server mengecek apakah nama tersebut sudah dipakai oleh client lain yang masih aktif.

```c
if(clients[i].socket_fd > 0 && strcmp(clients[i].username, temp.username) == 0) {
    exists = 1;
}
```

Jika nama sudah dipakai, server mengirim pesan penolakan:

```text
Username '<nama>' is already in use.
```

Jika nama masih tersedia, data client disimpan ke array `clients`, lalu server mengirim balasan `OK`.

```c
clients[i] = temp;
clients[i].socket_fd = new_sock;
strcpy(res.content, "OK");
send(new_sock, &res, sizeof(res), 0);
```

### 5. Broadcast Pesan

**Instruksi soal:** setiap pesan dari satu client harus diteruskan ke semua client lain yang sedang aktif. Proses broadcast harus dilakukan di sisi server.

**Implementasi pada kode:** fungsi `broadcast()` di `soal1/wired.c` melakukan loop ke seluruh array `clients`. Jika socket client aktif dan bukan socket pengirim, server mengirimkan paket pesan ke client tersebut.

```c
void broadcast(WiredPacket pkg, int sender_fd) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].socket_fd > 0 && clients[i].socket_fd != sender_fd) {
            send(clients[i].socket_fd, &pkg, sizeof(pkg), 0);
        }
    }
}
```

Saat server menerima pesan biasa, paket tersebut diproses sebagai chat dan dikirim lewat `broadcast()`.

```c
broadcast(pkg, sd);
```

Setelah itu, isi chat juga dicatat ke `history.log`.

```c
snprintf(detail, sizeof(detail), "[%s]: %s", pkg.sender, pkg.content);
write_log("User", detail);
```

### 6. RPC Admin The Knights

**Instruksi soal:** The Wired harus menyediakan prosedur jarak jauh yang hanya bisa dipakai oleh admin atau The Knights. Admin bisa mengecek jumlah NAVI aktif, mengecek uptime server, dan mematikan server. Request ini tidak boleh masuk ke jalur broadcast chat dan harus memakai autentikasi password.

**Implementasi pada kode:** admin dikenali dari username `The Knights` di `soal1/navi.c`. Jika nama tersebut dipakai, client meminta password dan membandingkannya dengan `KNIGHTS_PASSWORD` dari `protocol.h`.

```c
if (strcmp(me.username, "The Knights") == 0) {
    scanf("%49s", password);
    if (strcmp(password, KNIGHTS_PASSWORD) == 0) {
        is_admin = 1;
    }
}
```

Kalau autentikasi berhasil, field `me.is_admin` dikirim ke server.

```c
me.is_admin = is_admin;
send(sock, &me, sizeof(NaviIdentity), 0);
```

Fitur admin yang tersedia:

- `RPC_GET_USERS`: menampilkan jumlah client aktif non-admin.
- `RPC_GET_UPTIME`: menampilkan durasi server berjalan.
- `RPC_SHUTDOWN`: mematikan server.

Menu admin pada client:

```text
1. Check Active Entities (Users)
2. Check Server Uptime
3. Execute Emergency Shutdown
4. Disconnect
```

Saat admin memilih menu, client mengirim paket dengan `pkg.is_rpc = 1`.

```c
pkg.is_rpc = 1;
if (choice == 1) strcpy(pkg.content, "RPC_GET_USERS");
else if (choice == 2) strcpy(pkg.content, "RPC_GET_UPTIME");
else if (choice == 3) strcpy(pkg.content, "RPC_SHUTDOWN");
send(sock, &pkg, sizeof(WiredPacket), 0);
```

Di server, paket RPC dipisahkan dari chat biasa dengan mengecek `pkg.is_rpc`.

```c
if (pkg.is_rpc) {
    ...
}
```

Server juga memastikan bahwa socket pengirim benar-benar milik admin. Jika bukan admin, server mengirim pesan error.

```text
Error: Admin privileges required
```

Untuk command-nya, `RPC_GET_USERS` menghitung client aktif non-admin, `RPC_GET_UPTIME` menghitung durasi sejak server dinyalakan, dan `RPC_SHUTDOWN` mencatat shutdown lalu menjalankan `exit(0)`.

### 7. Logging ke `history.log`

**Instruksi soal:** setiap transmisi harus dicatat secara permanen di `history.log`. Tiap baris log harus memiliki format `[YYYY-MM-DD HH:MM:SS] [System/Admin/User] [Status/Command/Chat]`.

**Implementasi pada kode:** proses logging dilakukan oleh fungsi `write_log()` di `soal1/wired.c`. Fungsi ini membuka file `history.log` dalam mode append, membuat timestamp dengan `strftime()`, lalu menulis log sesuai format.

```c
fprintf(fp, "[%s] [%s] [%s]\n", timestamp, type, detail);
```

Format timestamp dibuat seperti ini:

```c
strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", t);
```

Aktivitas yang dicatat:

- Server online.
- User terhubung.
- User terputus.
- Chat yang berhasil diproses server.
- Command RPC admin.
- Emergency shutdown.

Contoh isi `history.log`:

```text
[2026-04-26 19:06:40] [System] [SERVER ONLINE]
[2026-04-26 19:06:46] [System] [User 'alice' connected]
[2026-04-26 19:06:50] [System] [User 'lain' connected]
[2026-04-26 19:06:56] [User] [[alice]: hello lain]
[2026-04-26 19:06:59] [User] [[lain]: hello alice]
[2026-04-26 19:07:11] [System] [User 'alice' disconnected]
[2026-04-26 19:07:27] [System] [User 'The Knights' connected]
[2026-04-26 19:07:29] [Admin] [RPC_GET_USERS]
[2026-04-26 19:07:29] [Admin] [RPC_GET_UPTIME]
[2026-04-26 19:07:31] [Admin] [RPC_SHUTDOWN]
[2026-04-26 19:07:31] [System] [EMERGENCY SHUTDOWN INITIATED]
```

## Dokumentasi Hasil Uji

### Screenshot 1 - Server The Wired Online

Server berhasil dijalankan dan sudah siap menerima koneksi client.

![alt text](assets/image.png)

### Screenshot 2 - Dua Client Berhasil Terhubung

Dua client berhasil masuk dengan username yang berbeda, sehingga keduanya diterima sebagai entitas aktif.

![alt text](assets/image-1.png)

![alt text](assets/image-2.png)

### Screenshot 3 - Validasi Username Duplikat

Saat client mencoba memakai nama yang sudah aktif, server menolak pendaftaran nama tersebut.

![alt text](assets/image-3.png)

### Screenshot 4 - Broadcast Chat

Pesan yang dikirim oleh satu client berhasil diterima oleh client lain melalui broadcast dari server.

![alt text](assets/image-4.png)

### Screenshot 5 - Admin RPC

Admin `The Knights` berhasil login dan menjalankan command RPC, seperti mengecek jumlah user aktif dan uptime server.

![alt text](assets/image-5.png)

### Screenshot 6 - History Log

File `history.log` berisi catatan aktivitas server, user, chat, dan command admin sesuai format yang diminta.

![alt text](assets/image-6.png)

## Kendala dan Error Selama Pengerjaan

### 1. Client Harus Bisa Kirim dan Terima Pesan Bersamaan

Kendala utama di client adalah bagaimana caranya tetap menerima pesan broadcast saat pengguna sedang mengetik input. Solusinya adalah memakai `pthread`, sehingga proses menerima pesan berjalan di thread terpisah, sementara thread utama tetap menangani input.

### 2. Server Tidak Boleh Terhambat Satu Client

Kalau server hanya memakai `recv()` biasa secara berurutan, satu client yang lambat bisa membuat server menunggu terlalu lama. Karena itu aku memakai `select()`, supaya server hanya membaca socket yang memang sedang aktif.

### 3. Pemisahan Chat dan RPC

Command admin tidak boleh ikut dibroadcast ke client lain. Untuk membedakannya, struktur `WiredPacket` diberi field `is_rpc`. Jika nilainya `1`, server memproses paket sebagai RPC. Jika nilainya `0`, server memprosesnya sebagai chat biasa.

### 4. Username Duplikat

Server perlu memastikan tidak ada dua client aktif dengan nama yang sama. Pengecekan dilakukan saat client baru mengirim `NaviIdentity`. Jika nama sudah ada di array `clients`, koneksi client baru langsung ditolak.

---

# Soal 2 - Battle of Eterion

## Deskripsi Soal

Pada soal ini, aku membuat sistem permainan battle berbasis IPC dengan tema Battle of Eterion. Program terdiri dari `orion.c` sebagai server dan `eternal.c` sebagai client. Keduanya tidak berkomunikasi melalui socket atau RPC, melainkan menggunakan IPC lokal berupa Message Queue, Shared Memory, dan Semaphore.

File yang dipakai:

- `soal2/arena.h`: berisi konfigurasi IPC, konstanta permainan, struktur akun, struktur battle, struktur request-response, dan daftar weapon.
- `soal2/orion.c`: server utama yang mengatur akun, login, matchmaking, battle, reward, armory, dan history.
- `soal2/eternal.c`: client yang menyediakan menu register, login, menu dunia Eterion, armory, battle, dan history.
- `soal2/Makefile`: membantu proses compile, clean binary, dan membersihkan IPC.

## Cara Menjalankan Program

Masuk ke folder `soal2`, lalu compile program:

```bash
cd soal2
make
```

Jalankan server terlebih dahulu:

```bash
./orion
```

Lalu jalankan client di terminal lain:

```bash
./eternal
```

Jika ingin menjalankan lebih dari satu prajurit, buka terminal baru lalu jalankan `./eternal` lagi. Untuk membersihkan binary:

```bash
make clean
```

Untuk membersihkan IPC yang masih tertinggal:

```bash
make clear_ipc
```

## Struktur Data dan IPC

Konfigurasi utama IPC diletakkan di `arena.h`.

```c
#define SHM_KEY 0x00001234
#define MSG_KEY 0x00005678
#define SEM_KEY 0x00009012
```

Shared Memory dipakai untuk menyimpan state utama arena, seperti daftar akun, status matchmaking, dan kondisi battle.

```c
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
```

Message Queue dipakai sebagai jalur komunikasi request-response antara `eternal` dan `orion`. Client mengirim request dengan `mtype = SERVER_MTYPE`, sedangkan server membalas dengan `mtype = pid` milik client.

```c
typedef struct {
    long mtype;
    pid_t pid;
    int action;
    char username[MAX_USERNAME];
    char password[MAX_PASSWORD];
    char text[MSG_TEXT];
} ArenaRequest;
```

Semaphore dipakai untuk mengunci akses ke Shared Memory supaya perubahan data akun, matchmaking, dan battle tidak mengalami race condition.

```c
static void sem_lock(void) {
    struct sembuf op = {0, -1, 0};
    semop(sem_id, &op, 1);
}
```

## Cara Pengerjaan

### 1. Struktur Arena dan Makefile

**Instruksi soal:** arena pertempuran terdiri dari `arena.h`, `orion.c`, `eternal.c`, dan `Makefile`.

**Implementasi pada kode:** seluruh konfigurasi bersama ditaruh di `soal2/arena.h`, sehingga server dan client memakai definisi yang sama. File ini berisi key IPC, batas maksimum data, formula default, action request, status response, struktur akun, struktur battle, dan daftar weapon.

```c
#define DEFAULT_GOLD 150
#define DEFAULT_LVL 1
#define DEFAULT_XP 0
#define BASE_DAMAGE 10
#define BASE_HEALTH 100
#define MATCH_TIMEOUT 35
#define ATTACK_COOLDOWN 1
```

Makefile menyediakan target `server`, `client`, `clean`, dan `clear_ipc`.

```makefile
all: server client

server: orion.c arena.h
	$(CC) $(CFLAGS) orion.c -o orion $(LDFLAGS)

client: eternal.c arena.h
	$(CC) $(CFLAGS) eternal.c -o eternal $(LDFLAGS)
```

### 2. Main Menu dan Koneksi Eternal ke Orion

**Instruksi soal:** saat `eternal` dijalankan, program harus menampilkan main menu. `orion` harus selalu siap menerima koneksi dari `eternal`, dan hubungan ini hanya berjalan melalui IPC.

**Implementasi pada kode:** `orion.c` membuat Shared Memory, Message Queue, dan Semaphore menggunakan key dari `arena.h`. Setelah berhasil, server menandai dirinya siap dengan `arena->server_ready = 1`.

```c
shm_id = shmget(SHM_KEY, sizeof(ArenaState), IPC_CREAT | 0666);
msg_id = msgget(MSG_KEY, IPC_CREAT | 0666);
sem_id = semget(SEM_KEY, 1, IPC_CREAT | 0666);
arena->server_ready = 1;
```

Di sisi client, `eternal.c` mencoba membuka Message Queue dan Shared Memory yang sama. Jika `orion` belum berjalan, client akan gagal masuk dan menampilkan pesan error.

```c
msg_id = msgget(MSG_KEY, 0666);
shm_id = shmget(SHM_KEY, sizeof(ArenaState), 0666);
if (msg_id < 0 || shm_id < 0) {
    printf("[System] Orion is not ready. Start ./orion first.\n");
    return -1;
}
```

Menu awal yang ditampilkan oleh `eternal`:

```text
=== Eternal Gate ===
1. Register
2. Login
3. Exit
Choose:
```

### 3. Komunikasi Message Queue dan Shared Memory

**Instruksi soal:** `eternal` dan `orion` harus saling bertukar request melalui Message Queue dan bertukar informasi melalui Shared Memory.

**Implementasi pada kode:** client mengirim `ArenaRequest` ke server menggunakan `msgsnd()`.

```c
req.mtype = SERVER_MTYPE;
req.pid = getpid();
req.action = action;
msgsnd(msg_id, &req, sizeof(req) - sizeof(long), 0);
```

Setelah itu client menunggu balasan dari server menggunakan `msgrcv()` dengan message type sesuai PID client.

```c
msgrcv(msg_id, res, sizeof(*res) - sizeof(long), getpid(), 0);
```

Server membaca request dari queue utama dengan `SERVER_MTYPE`.

```c
msgrcv(msg_id, &req, sizeof(req) - sizeof(long), SERVER_MTYPE, IPC_NOWAIT);
```

Shared Memory menyimpan data yang harus tetap bisa dipakai bersama, seperti akun, status aktif, matchmaking, dan battle. Setiap perubahan terhadap data tersebut dilindungi dengan semaphore.

### 4. Register, Login, dan Validasi Akun Aktif

**Instruksi soal:** setiap prajurit harus bisa register dan login menggunakan username serta password. Username harus unique, data harus tetap tersimpan walaupun `eternal` mati, dan akun yang sedang login tidak boleh login lagi dari sesi lain.

**Implementasi pada kode:** saat register, server mengecek apakah username sudah ada menggunakan `find_account()`.

```c
if (find_account(req->username) >= 0) {
    res.status = STATUS_ERR;
    safe_copy(res.message, "Username already registered.", sizeof(res.message));
}
```

Jika username belum terdaftar, server membuat akun baru dengan nilai default sesuai soal.

```c
w->gold = DEFAULT_GOLD;
w->lvl = DEFAULT_LVL;
w->xp = DEFAULT_XP;
```

Pada login, server memastikan username dan password sesuai.

```c
if (idx < 0 || strcmp(arena->accounts[idx].password, req->password) != 0) {
    res.status = STATUS_ERR;
    safe_copy(res.message, "Invalid username or password.", sizeof(res.message));
}
```

Jika akun masih aktif di sesi lain, login ditolak.

```c
if (arena->accounts[idx].active) {
    res.status = STATUS_ERR;
    safe_copy(res.message, "Account is already active in another session.", sizeof(res.message));
}
```

Data akun disimpan di Shared Memory, sehingga saat satu proses `eternal` mati, akun yang sudah terdaftar masih bisa dipakai selama IPC belum dibersihkan. Server juga mengecek sesi stale dengan `kill(pid, 0)` agar akun yang proses client-nya sudah mati bisa dipakai lagi.

### 5. Profile dan Nilai Default Prajurit

**Instruksi soal:** setiap akun baru memiliki nilai awal Gold 150, Lvl 1, dan XP 0.

**Implementasi pada kode:** nilai awal ditentukan oleh konstanta di `arena.h`.

```c
#define DEFAULT_GOLD 150
#define DEFAULT_LVL 1
#define DEFAULT_XP 0
```

Setelah login berhasil, `eternal` menampilkan profil prajurit.

```text
=== WARRIOR PROFILE ===
Gold   : 150
Lvl    : 1
XP     : 0
Weapon : +0 Dmg
```

Setelah masuk ke dunia Eterion, menu utama yang tersedia:

```text
=== Battle of Eterion ===
1. Profile
2. Battle
3. Armory
4. Battle History
5. Logout
Choose:
```

### 6. Matchmaking Selama 35 Detik

**Instruksi soal:** saat memilih Battle, prajurit masuk fase matchmaking selama 35 detik. Jika tidak menemukan lawan, prajurit akan melawan monster atau bot. Prajurit yang sedang battle tidak boleh terdeteksi matchmaking.

**Implementasi pada kode:** saat client memilih Battle, `eternal` mengirim action `ACT_MATCHMAKE`.

```c
request_server(ACT_MATCHMAKE, current_user, "", "", &res);
```

Jika belum ada pemain lain yang menunggu, server menyimpan pemain tersebut sebagai pencari lawan.

```c
arena->matchmaking_active = 1;
safe_copy(arena->matchmaking_user, req->username, sizeof(arena->matchmaking_user));
arena->matchmaking_pid = req->pid;
arena->matchmaking_since = time(NULL);
```

Jika ada pemain lain yang sedang menunggu, server langsung memulai battle antar dua prajurit.

```c
start_battle(arena->matchmaking_user, arena->matchmaking_pid,
             req->username, req->pid, 0);
arena->matchmaking_active = 0;
```

Jika waktu tunggu mencapai 35 detik, server membuat battle melawan bot bernama `Monster`.

```c
if (time(NULL) - arena->matchmaking_since >= MATCH_TIMEOUT) {
    start_battle(arena->matchmaking_user, arena->matchmaking_pid, "Monster", 0, 1);
    arena->matchmaking_active = 0;
}
```

Prajurit yang sedang berada dalam battle diberi flag `in_battle = 1`, sehingga tidak dianggap sebagai kandidat matchmaking lain.

### 7. Battle Realtime dan Asynchronous Attack

**Instruksi soal:** battle berjalan realtime, bukan turn-based. Pemain dapat menekan `a` untuk attack dan `u` untuk ultimate. Health kedua prajurit dan 5 log teratas harus tampil realtime. Attack memiliki cooldown 1 detik.

**Implementasi pada kode:** saat battle dimulai, server menghitung HP dan damage masing-masing pemain.

```c
b->hp1 = account_health(&arena->accounts[i1]);
b->dmg1 = account_damage(&arena->accounts[i1]);
```

Formula damage dan health:

```c
static int account_damage(const Warrior *w) {
    return BASE_DAMAGE + (w->xp / 50) + w->weapon_bonus;
}

static int account_health(const Warrior *w) {
    return BASE_HEALTH + (w->xp / 10);
}
```

Client memakai mode raw terminal dan `select()` supaya input keyboard bisa dibaca tanpa menunggu ENTER. Tombol `a` mengirim `ACT_ATTACK`, sedangkan tombol `u` mengirim `ACT_ULTIMATE`.

```c
if (c == 'a' || c == 'u') {
    int action = c == 'a' ? ACT_ATTACK : ACT_ULTIMATE;
    request_server(action, current_user, "", "", &res);
}
```

Server menerapkan cooldown dengan membandingkan waktu serangan terakhir.

```c
if (now - *last_attack < ATTACK_COOLDOWN) {
    res.status = STATUS_ERR;
    safe_copy(res.message, "Attack is still on cooldown.", sizeof(res.message));
}
```

Ultimate hanya dapat digunakan jika pemain memiliki weapon. Damage ultimate adalah total damage dikali 3.

```c
else if (req->action == ACT_ULTIMATE && arena->accounts[idx].weapon_bonus <= 0) {
    res.status = STATUS_ERR;
    safe_copy(res.message, "Ultimate requires a weapon.", sizeof(res.message));
} else {
    if (req->action == ACT_ULTIMATE) damage *= 3;
}
```

Lima log battle teratas disimpan di `BattleState.logs`.

```c
for (int i = BATTLE_LOGS - 1; i > 0; i--) {
    safe_copy(arena->battle.logs[i], arena->battle.logs[i - 1], MAX_HISTORY_LINE);
}
safe_copy(arena->battle.logs[0], line, MAX_HISTORY_LINE);
```

Di sisi client, status battle dirender ulang secara berkala agar HP dan combat log terlihat realtime.

### 8. Reward, XP, Level, Gold, Damage, dan Health

**Instruksi soal:** pemain mendapat XP dan Gold setelah battle selesai, baik menang maupun kalah. Level naik setiap XP mencapai kelipatan 100, XP tidak direset.

**Implementasi pada kode:** saat battle selesai, server memanggil `finish_battle()`. Pemain yang menang mendapat `+50 XP` dan `+120 Gold`, sedangkan pemain yang kalah mendapat `+15 XP` dan `+30 Gold`.

```c
arena->accounts[i1].xp += won ? WIN_XP : LOSE_XP;
arena->accounts[i1].gold += won ? WIN_GOLD : LOSE_GOLD;
recalc_level(&arena->accounts[i1]);
```

Level dihitung dari total XP tanpa mereset XP.

```c
static void recalc_level(Warrior *w) {
    w->lvl = DEFAULT_LVL + (w->xp / 100);
}
```

Formula damage dan health mengikuti ketentuan soal:

```text
Damage = BASE_DAMAGE + (total XP / 50) + total bonus damage weapon
Health = BASE_HEALTH + (total XP / 10)
```

### 9. Armory dan Weapon

**Instruksi soal:** prajurit dapat membeli weapon di armory. Sistem otomatis memakai weapon dengan damage terbesar. Jika memiliki weapon, prajurit dapat menggunakan ultimate.

**Implementasi pada kode:** daftar weapon disimpan di `arena.h`.

```c
static const Weapon WEAPONS[] = {
    {"Wood Sword", 5, 100},
    {"Iron Sword", 15, 300},
    {"Steel Axe", 30, 600},
    {"Demon Blade", 60, 1500},
    {"God Slayer", 150, 5000}
};
```

Saat membeli weapon, server mengecek gold pemain. Jika cukup, gold dikurangi sesuai harga.

```c
if (arena->accounts[idx].gold < weapon->price) {
    res.status = STATUS_ERR;
    safe_copy(res.message, "Not enough gold.", sizeof(res.message));
} else {
    arena->accounts[idx].gold -= weapon->price;
}
```

Weapon yang dipakai otomatis adalah bonus damage terbesar yang sudah pernah dibeli.

```c
if (weapon->bonus_damage > arena->accounts[idx].weapon_bonus) {
    arena->accounts[idx].weapon_bonus = weapon->bonus_damage;
}
```

### 10. Battle History

**Instruksi soal:** setiap prajurit memiliki history atau catatan battle yang pernah dilakukan.

**Implementasi pada kode:** setiap akun memiliki array `history` di struktur `Warrior`.

```c
char history[MAX_HISTORY][MAX_HISTORY_LINE];
int history_count;
```

Saat battle selesai, server membuat catatan berisi waktu, lawan, hasil, dan XP yang didapat.

```c
snprintf(line, sizeof(line), "%s|%s|%s|+%d",
         match_time, b->p2, won ? "WIN" : "LOSS", won ? WIN_XP : LOSE_XP);
push_history(&arena->accounts[i1], line);
```

Di sisi client, history ditampilkan dalam bentuk tabel.

```text
+----------+------------------+--------+--------+
| Time     | Opponent         | Res    | XP     |
+----------+------------------+--------+--------+
```

Jika history sudah penuh, data paling lama digeser dan data terbaru dimasukkan ke posisi terakhir.

### 11. Pencegahan Race Condition

**Instruksi soal:** semua proses harus aman dari race condition, sehingga perlu memakai Semaphore atau Mutex.

**Implementasi pada kode:** server menggunakan System V Semaphore untuk melindungi semua akses kritis ke Shared Memory. Fungsi `handle_request()` mengunci semaphore sebelum memproses request dan membuka kuncinya setelah selesai.

```c
sem_lock();
maybe_start_bot_match();
bot_tick();
...
sem_unlock();
```

Dengan cara ini, perubahan data akun, pembelian weapon, matchmaking, pengurangan HP, reward, dan history tidak ditulis bersamaan oleh dua request berbeda.

## Dokumentasi Hasil Uji

### 1. Orion Berhasil Dijalankan

Saat `./orion` dijalankan, server membuat IPC dan siap menerima request dari client.

```text
Orion is ready. Waiting for Eternal warriors...
```

### 2. Eternal Gagal Jika Orion Belum Siap

Jika `./eternal` dijalankan sebelum `./orion`, client tidak bisa membuka komunikasi IPC.

```text
[System] Orion is not ready. Start ./orion first.
```

### 3. Register dan Login

Register berhasil membuat akun baru dengan Gold 150, Lvl 1, XP 0, dan Weapon +0 Dmg. Username yang sama tidak bisa didaftarkan lagi, dan akun yang sedang aktif tidak bisa login di sesi lain.

### 4. Matchmaking dan Battle

Jika ada dua client masuk matchmaking, server mempertemukan keduanya dalam battle. Jika hanya satu client yang menunggu selama 35 detik, server membuat lawan bot bernama `Monster`.

### 5. Attack, Ultimate, dan Reward

Tombol `a` berhasil melakukan attack dengan cooldown 1 detik. Tombol `u` hanya berhasil jika pemain sudah memiliki weapon. Setelah battle selesai, XP, Gold, Level, dan History pemain diperbarui.

## Kendala dan Error Selama Pengerjaan

### 1. Data Harus Bisa Dibaca Banyak Proses

Karena `orion` dan banyak proses `eternal` berjalan terpisah, data tidak bisa hanya disimpan di variabel biasa milik client. Solusinya adalah memakai Shared Memory sebagai penyimpanan state utama arena.

### 2. Request Banyak Client Tidak Boleh Tercampur

Semua client mengirim request ke Message Queue yang sama, sehingga response harus dikirim ke client yang benar. Untuk itu response memakai `mtype` berdasarkan PID client.

### 3. Battle Realtime Tanpa Turn-Based

Client tidak boleh menunggu input dengan `scanf()` biasa saat battle, karena tampilan HP perlu terus diperbarui. Solusinya adalah memakai raw terminal dan `select()` agar input tombol bisa dibaca tanpa blocking.

### 4. Akun Aktif Setelah Client Mati Mendadak

Jika client mati tanpa logout, akun bisa tertinggal dalam status aktif. Untuk mengatasinya, server mengecek PID sesi dengan `kill(pid, 0)`. Jika proses sudah tidak ada, akun dianggap tidak aktif lagi.

### 5. Race Condition pada Shared Memory

Data akun, matchmaking, dan battle bisa rusak jika dua request mengubah Shared Memory bersamaan. Karena itu setiap proses perubahan state di server dibungkus dengan semaphore lock dan unlock.
