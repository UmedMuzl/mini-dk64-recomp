#include "modding.h"
#include "ultra64.h"
#include "enums.h"
#include "common_structs.h"

#define VERSION 0

#if VERSION == 0
    #define MAX_SIZE 0.6f
    #define MIN_SIZE 0.05f
    #define CB_MAX 500
#else
    #define MAX_SIZE 0.45f
    #define MIN_SIZE 0.03f
    #define CB_MAX 500
#endif

#define ARRAY_COUNT(arr) ((s32)(sizeof(arr) / sizeof((arr)[0])))

typedef struct character_progress {
    u8 moves;                            // 0x00
    u8 simian_slam;                      // 0x01
    u8 weapon;                           // 0x02
    u8 ammo_belt;                        // 0x03
    u8 instrument;                       // 0x04
    u8 unk5;                             // 0x05
    u16 coins;                           // 0x06
    u16 instrument_ammo;                 // 0x08
    u16 coloured_bananas[14];            // 0x0A
    u16 coloured_bananas_fed_to_tns[14]; // 0x26
    u16 golden_bananas[14];              // 0x42
} CharacterProgress;                     // sizeof == 0x5E

typedef struct player_progress {
    CharacterProgress character_progress[6]; // 0x000: 5 Kongs + Krusha (hack: MovesBase)
    u8 unk234[0x2F0 - 0x234];
    u16 standardAmmo;                        // 0x2F0
    u16 homingAmmo;                          // 0x2F2
    u16 oranges;                             // 0x2F4
    u16 crystals;                            // 0x2F6
    u16 film;                                // 0x2F8
    s8 unk2FA;                               // 0x2FA
    s8 health;                               // 0x2FB
    u8 melons;                               // 0x2FC
    s8 unk2FD;                               // 0x2FD
    u16 unk2FE[(0x306 - 0x2FE) / 2];
} PlayerProgress;

typedef struct loaded_actor {
    Actor *actor; // 0x00
    s32 unk4;     // 0x04
} LoadedActor;


typedef struct blocker_cheat {
    u8 requirement; // 0x00
    s8 kong;        // 0x01
} BLockerCheat;


typedef struct DelayedCSData DelayedCSData;
struct DelayedCSData {
    DelayedCSData *next;                    // 0x00
    s32 (*function)(s32 a, s32 b, s32 c);   // 0x04
    s32 action_frame;                       // 0x08
    s32 args[3];                            // 0x0C
};

extern Maps current_map;
extern Maps next_map;
extern s32 next_exit;
extern u8 game_mode;
extern s8 story_skip;
extern u32 object_timer;
extern u8 is_cutscene_active;
extern s16 D_global_asm_807476F4;               // CutsceneIndex
extern u16 D_global_asm_807FBB34;               // LoadedActorCount
extern LoadedActor D_global_asm_807FB930[];     // LoadedActorArray
extern PlayerProgress D_global_asm_807FC950[4]; // MovesBase + CollectableBase
extern u16 D_global_asm_807446C0[8];            // BossReqArray CBs to open each boss door
extern s16 D_global_asm_807446D0[8];            // BLockerArray GBs for each B. Locker
extern BLockerCheat D_global_asm_807446E0[8];   // BLockerCheatArray B. Locker cheat requirements
extern DelayedCSData *D_global_asm_807452A0;    // delayed cutscene action list head
extern u32 D_global_asm_8076A068;               // frame counter

extern u8 getLevelIndex(u8 map, u8 lobby_is_isles);           // hack: getWorld  (0x805FF030)
extern u8 isFlagSet(s16 flagIndex, u8 flagType);              // hack: checkFlag (0x8073110C)
extern void setFlag(s16 flagIndex, u8 newValue, u8 flagType); // hack: setFlag   (0x8073129C)
extern void _free(void *ptr);                                 // game heap free  (0x8061130C)

// perm flags
static const s16 file_init_flags[] = {
    PERMFLAG_PROGRESS_TRAINING_SPAWNED,
    PERMFLAG_PROGRESS_GIVEN_FIRST_SLAM,
    PERMFLAG_CUTSCENE_TRAINING_GROUNDS_WATERFALL,
    383,
    PERMFLAG_ITEM_MOVE_DIVING,
    PERMFLAG_ITEM_MOVE_VINES,
    PERMFLAG_ITEM_MOVE_ORANGETHROWING,
    PERMFLAG_ITEM_MOVE_BARRELTHROWING,
    PERMFLAG_PROGRESS_ALL_TRAINING_COMPLETE,
    PERMFLAG_PROGRESS_JAPES_LOBBY_OPEN,
    PERMFLAG_CUTSCENE_ISLES_FTCS,
    PERMFLAG_PROGRESS_JAPES_FIRST_GATE_OPENED,
    385, // 0x181, Free DK
    PERMFLAG_PROGRESS_LLAMA_FREE,
};

static const Actors actors_to_shrink[] = {
    ACTOR_DK, ACTOR_DIDDY, ACTOR_LANKY, ACTOR_TINY, ACTOR_CHUNKY, // Kongs
    ACTOR_KRUSHA, // Krusha, just for good measure
    ACTOR_RAMBI, ACTOR_ENGUARDE, // Rambi & Enguarde
    ACTOR_CUTSCENE_DK, ACTOR_CUTSCENE_DIDDY, ACTOR_CUTSCENE_LANKY, ACTOR_CUTSCENE_TINY, ACTOR_CUTSCENE_CHUNKY, // Cutscene Kongs
    ACTOR_TAGBARREL_KONG, // Tag Barrel Kong
    ACTOR_MINIGAME_KRAZYKONGKLAMOUR_KONG, // Kong: Krazy KK
    ACTOR_KONG_REFLECTION, ACTOR_REFLECTION_MUSEUM, // Reflections
};

static f32 scale = 0.15f;

static s32 isShrinkActor(Actor *test_actor) {
    for (s32 i = 0; i < ARRAY_COUNT(actors_to_shrink); i++) {
        if (test_actor->unk58 == actors_to_shrink[i]) {
            return 1;
        }
    }
    return 0;
}

static void mini_dk64_frame(void) {
    PlayerProgress *progress = &D_global_asm_807FC950[0];

    /*
        CB Scaling
        Min Size: 0.01 - 0 CBs
        Normal Size: 0.15 - 100 CBs
        Max Size: 1.5 - 500 CBs
    */
    s32 world = getLevelIndex(current_map, 1);
    if (current_map == MAP_MAIN_MENU) {
        world = 0;
    }
    if (world < 7) {
        s32 cb_count = 0;
        for (s32 i = 0; i < 5; i++) {
            cb_count += progress->character_progress[i].coloured_bananas[world];
        }
        f32 ratio = ((f32)cb_count) / ((f32)CB_MAX);
        f32 delta = MAX_SIZE - MIN_SIZE;
        scale = MIN_SIZE + (ratio * delta);
    }
    if (object_timer > 2) {
        if ((!is_cutscene_active) || (D_global_asm_807476F4 != 29)) {
            for (s32 a = 0; a < D_global_asm_807FBB34; a++) {
                Actor *actor = D_global_asm_807FB930[a].actor;
                if (actor && isShrinkActor(actor)) {
                    ActorAnimationState *render = actor->animation_state;
                    if (render) {
                        for (s32 i = 0; i < 3; i++) {
                            render->scale[i] = scale;
                        }
                    }
                }
            }
        }
    }
    story_skip = 1;

    if ((current_map == MAP_MAIN_MENU) && (next_map != MAP_MAIN_MENU) && (game_mode == GAME_MODE_ADVENTURE)) {
        if (!isFlagSet(file_init_flags[0], FLAG_TYPE_PERMANENT)) {
            for (s32 i = 0; i < ARRAY_COUNT(file_init_flags); i++) {
                setFlag(file_init_flags[i], 1, FLAG_TYPE_PERMANENT);
            }
            #if VERSION == 0
                for (s32 i = 0; i < 5; i++) {
                    progress->character_progress[i].simian_slam = 1;
                    progress->character_progress[i].moves = 3;           // special_moves
                    progress->character_progress[i].instrument = 1;      // instrument_bitfield
                    progress->character_progress[i].weapon = 1;          // weapon_bitfield
                    progress->character_progress[i].instrument_ammo = 5; // instrument_energy
                }
                progress->melons = 2;
                progress->health = 8;
                progress->standardAmmo = 50;
                setFlag(771, 1, FLAG_TYPE_PERMANENT); // Open Coin Door
            #else
                for (s32 i = 0; i < 5; i++) {
                    progress->character_progress[i].simian_slam = 1;
                }
            #endif
        }
        next_map = MAP_DK_ISLES_OVERWORLD;
        next_exit = 0;
    }

    if (object_timer < 2) {
        D_global_asm_807446C0[7] = 1; // Fix KRE
        #if VERSION == 0
            // CBs to unlock bosses
            D_global_asm_807446C0[0] = 30;
            D_global_asm_807446C0[1] = 60;
            D_global_asm_807446C0[2] = 100;
            D_global_asm_807446C0[3] = 125;
            D_global_asm_807446C0[4] = 150;
            D_global_asm_807446C0[5] = 175;
            D_global_asm_807446C0[6] = 200;
            // GBs to clear B. Locker
            D_global_asm_807446D0[0] = 1;
            D_global_asm_807446D0[1] = 3;
            D_global_asm_807446D0[2] = 8;
            D_global_asm_807446D0[3] = 15;
            D_global_asm_807446D0[4] = 25;
            D_global_asm_807446D0[5] = 32;
            D_global_asm_807446D0[6] = 40;
            D_global_asm_807446D0[7] = 50;
            // GBs to clear B. Locker (Cheaty McCheaterface)
            D_global_asm_807446E0[0].requirement = 1;
            D_global_asm_807446E0[1].requirement = 2;
            D_global_asm_807446E0[2].requirement = 5;
            D_global_asm_807446E0[3].requirement = 10;
            D_global_asm_807446E0[4].requirement = 17;
            D_global_asm_807446E0[5].requirement = 25;
            D_global_asm_807446E0[6].requirement = 32;
            D_global_asm_807446E0[7].requirement = 40;
        #endif
    }
}


RECOMP_PATCH void func_global_asm_80600B10(void) {
    s32 var_s1;
    DelayedCSData *var_s0;

    var_s0 = D_global_asm_807452A0;
    var_s1 = 0;
    while (var_s0 != NULL && !var_s1) {
        if (D_global_asm_8076A068 >= (u32)(var_s0->action_frame & 0x7FFFFFFF)) {
            var_s0->function(var_s0->args[0], var_s0->args[1], var_s0->args[2]);
            _free(var_s0);
            var_s0 = var_s0->next;
        } else {
            var_s1 = 1;
        }
    }
    D_global_asm_807452A0 = var_s0;

    if ((is_cutscene_active != 3) && (is_cutscene_active != 4) && (is_cutscene_active != 5) && (is_cutscene_active != 6)) {
        mini_dk64_frame();
    }
}
