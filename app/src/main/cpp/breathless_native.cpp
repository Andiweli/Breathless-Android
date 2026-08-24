#include <jni.h>
#include <GLES2/gl2.h>
#include <android/log.h>
#include <dirent.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <ctime>
#include <mutex>
#include <atomic>
#include <cmath>
#include <map>
#include <set>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "Breathless", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "Breathless", __VA_ARGS__)

static std::string gDataPath;
static GLuint gProgram = 0, gTexture = 0, gVbo = 0;
static int gTextureWidth = 0, gTextureHeight = 0;
static GLint gPosLoc = -1, gUvLoc = -1, gTexLoc = -1, gScaleLoc = -1, gUvScaleLoc = -1;
static int gViewW = 1, gViewH = 1;
static unsigned gFrame = 0;
static int gGldFiles = -1;
static std::string gFirstGldName;
static long gFirstGldSize = -1;
static unsigned int gFirstGldChecksum = 0;
static unsigned char gFirstGldHead[16];
static int gFirstGldHeadLen = 0;
static bool gFirstGldProbeOk = false;
static bool gGldDirProbeOk = false;
static unsigned int gGldDirOffset = 0;
static unsigned int gGldDirCount = 0;
static std::string gGldEntry0Name;
static unsigned int gGldEntry0Offset = 0;
static unsigned int gGldEntry0Length = 0;
static bool gGldEntry0TextureOk = false;
static unsigned int gTex0Width = 0;
static unsigned int gTex0Anim = 0;
static unsigned int gTex0Height = 0;
static unsigned int gTex0HShift = 0;
static unsigned int gTex0Frame = 0;
static unsigned int gTex0Zero = 0;
static std::vector<unsigned char> gTex0Raw;
static bool gTex0RawOk = false;
static unsigned int gTex0RawChecksum = 0;
static unsigned int gTex0RawMin = 0;
static unsigned int gTex0RawMax = 0;
static bool gPaletteOk = false;
static unsigned int gPaletteChecksum = 0;
static unsigned int gPaletteCount = 0;
static unsigned char gPalette[256 * 3];
static double gFps = 0.0;
static unsigned gFpsFrames = 0;
static double gFpsLastTime = 0.0;
// The original game renders a 320x200 playfield.  A 356x200 buffer is the
// closest integral-pixel 16:9 playfield; its 18-pixel side bands are genuine
// extra world view while the original 320-pixel HUD remains centred.
static const int FB_W = 356;
static const int FB_H = 200;
static const int VIEW_H = 160;
static const int VIEW_CENTER_Y = VIEW_H / 2;
static const int ORIGINAL_W = 320;
static const int PRESENTATION_H = 240;
static const int HUD_X = (FB_W - ORIGINAL_W) / 2;
static const float ORIGINAL_VERTICAL_REFERENCE = 110.0f;
static const int PICKUP_FLOAT_HEIGHT = 8;
static const int PICKUP_BOB_AMPLITUDE = 3;
static std::vector<unsigned int> gFramebuffer(FB_W * FB_H);
static std::vector<unsigned int> gPauseBackground(FB_W * FB_H);
static std::vector<unsigned int> gLastGameFramebuffer(FB_W * FB_H);
static std::mutex gGameSnapshotMutex;
static bool gHaveGameSnapshot = false;
static std::vector<unsigned int> gPresentationFramebuffer(ORIGINAL_W * PRESENTATION_H);

struct TextureEntry {
    std::string name;
    unsigned int offset;
    unsigned int length;
    unsigned int width;
    unsigned int height;
    unsigned int anim;
    unsigned int hshift;
    unsigned int frame;
    unsigned int zero;
    bool headerOk;
};

struct TextureBitmap {
    std::string name;
    std::vector<unsigned char> raw;
    unsigned int width = 0;
    unsigned int height = 0;
    unsigned int frames = 1;
    bool ok = false;
};

struct GfxBitmap {
    std::string name;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    std::vector<unsigned char> palette;
    std::vector<unsigned char> pixels;
    bool ok = false;
};

struct LgldBlockInfo {
    int floorHeight = 0;
    int ceilHeight = 0;
    int floorTex = 0;
    int ceilTex = 0;
    int illumination = 0;
    bool fog = false;
    int edge[4] = {0, 0, 0, 0};
    unsigned int effect = 0;
    unsigned int trigger2 = 0;
    unsigned int attributes = 0;
    unsigned int trigger = 0;
};

struct LgldEffectCommand {
    unsigned int trigger = 0;
    unsigned int type = 0;
    int param1 = 0;
    int param2 = 0;
    unsigned int key = 0;
};

struct LgldPlacedObject {
    unsigned int objectCode = 0;
    std::string name;
    unsigned int worldX = 0;
    unsigned int worldY = 0;
    unsigned int heading = 0;
    unsigned int flags = 0;
    unsigned int activationTrigger = 0;
};

struct ObjectSpriteFrame {
    int width = 0, height = 0, xOffset = 0, yOffset = 0;
    std::vector<unsigned char> pixels;
    std::vector<unsigned char> mask;
};

struct GlobalObjectInfo {
    int numFrames = 0;
    int radius = 16;
    int height = 64;
    int animationType = 0;
    unsigned int objectType = 0;
    int param[12] = {0};
    std::string sound[3];
    int spriteWidth = 0;
    int spriteHeight = 0;
    int spriteXOffset = 0;
    int spriteYOffset = 0;
    std::vector<unsigned char> spritePixels;
    std::vector<unsigned char> spriteMask;
    std::vector<ObjectSpriteFrame> frames;
    std::vector<unsigned int> frameOffsets;
};

struct RuntimeObject {
    size_t placedIndex = 0;
    float x = 0.0f, y = 0.0f;
    float heading = 0.0f;
    float bobPhase = 0.0f;
    float thinkClock = 0.0f;
    float attackClock = 0.0f;
    float stateClock = 0.0f;
    float contactClock = 0.0f;
    float alertClock = 0.0f;
    float lastSeenX = 0.0f, lastSeenY = 0.0f;
    int aiState = 0; // 0 seek, 1 short random walk, 2 collision avoidance, 4 prepare/fire
    int behaviorCounter = 4;
    int turnDirection = 1;
    int collisionAttempts = 0;
    int health = 1;
    int animationFrame = 0;
    float deathClock = 0.0f;
    int deathYOffset = 0;
    const GlobalObjectInfo* deathDefinition = nullptr;
    bool dying = false;
    bool exploding = false;
    bool corpse = false;
    bool collected = false;
    bool dead = false;
};

struct RuntimeProjectile {
    const GlobalObjectInfo* definition = nullptr;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float dirX = 0.0f, dirY = 0.0f, verticalSlope = 0.0f;
    float speedUnits = 0.0f, accelerationUnits = 0.0f, maxSpeedUnits = 0.0f;
    float speed = 0.0f; // current cells/second, also useful to diagnostics/tests
    float travelled = 0.0f, maxDistance = 20.0f;
    int damage = 1;
    bool enemy = false;
    bool dead = false;
};

struct RuntimeImpactSpark {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float previousX = 0.0f, previousY = 0.0f, previousZ = 0.0f;
    float velocityX = 0.0f, velocityY = 0.0f, velocityZ = 0.0f;
    float age = 0.0f, lifetime = 0.25f;
    unsigned int color = 0xffffffffu;
    bool dead = false;
};

struct LgldEdgeInfo {
    int normTex = 0;
    int upTex = 0;
    int lowTex = 0;
    unsigned int attribute = 0;
};

struct OrigVtHit {
    float distance = 0.0f;
    int mapX = 0;
    int mapY = 0;
    int blockIndex = 0;
    int side = 0;      // 0 = X crossing, 1 = Y/Z crossing, matching original ray-step split
    int stepX = 0;
    int stepY = 0;
    int edgeFace = -1; // 0 Edge1=east, 1 Edge2=south, 2 Edge3=west, 3 Edge4=north
    int edgeIndex = 0;
    int brushOffset = 0;
    bool stopWall = false;
};

struct OrigSpan {
    int x = 0;
    int y0 = 0;
    int y1 = 0;
    int blockIndex = 0;
    bool floorSpan = false;
    bool visibleSurface = true;
};


struct AssetInfo {
    std::string name;
    long size = 0;
    unsigned int checksum = 0;
    std::string id;
    unsigned int word0 = 0;
    unsigned int word1 = 0;
    unsigned int count = 0;
    unsigned int dirOffset = 0;
    unsigned int packedSize = 0;
    unsigned int unpackedSize = 0;
    unsigned int compressionType = 0;
    unsigned int unpackedHash = 0;
    std::string unpackedId;
    unsigned int lgldLength = 0;
    unsigned int lgldPayloadBytes = 0;
    unsigned int lgldLevelCount = 0;
    unsigned int lgldDirOffset = 0;
    unsigned int lgldMapLength = 0;
    unsigned int lgldBlocks = 0;
    unsigned int lgldEdges = 0;
    unsigned int lgldEffectLists = 0;
    unsigned int lgldEffectEntries = 0;
    unsigned int lgldEffectsBytes = 0;
    unsigned int lgldMapOffset = 0;
    unsigned int lgldUsedCells = 0;
    unsigned int lgldSolidCells = 0;
    unsigned int lgldMaxBlockAbs = 0;
    int lgldMinX = 0, lgldMinY = 0, lgldMaxX = 0, lgldMaxY = 0;
    unsigned int lgldTextureNames = 0;
    unsigned int lgldObjectNames = 0;
    unsigned int lgldObjects = 0;
    unsigned int lgldSoundNames = 0;
    std::string lgldFirstTexture;
    std::string lgldFirstObject;
    std::string lgldLoadPic;
    bool lgldParseOk = false;
    std::vector<short> lgldMapCells;
    std::vector<LgldBlockInfo> lgldBlockData;
    std::vector<LgldEdgeInfo> lgldEdgeData;
    std::vector<std::string> lgldTextureList;
    std::vector<std::string> lgldObjectList;
    std::vector<std::string> lgldSoundList;
    std::vector<std::vector<LgldEffectCommand> > lgldEffectData;
    std::vector<LgldPlacedObject> lgldPlacedObjects;
    std::string guess;
    std::string firstEntry;
    std::string headHex;
};

static std::vector<TextureEntry> gTextureEntries;
static int gTextureIndex = 0;
static std::mutex gTextureMutex;

static TextureBitmap gSkyTex;
static TextureBitmap gFloorTex[3];
static TextureBitmap gWallTex[8];
static int gWallTexCount = 0;
static int gFloorTexCount = 0;
static std::vector<TextureBitmap> gLevelTextureCache;
static std::vector<TextureBitmap> gLevelSwitchTextureCache;
static int gLevelTextureCacheAssetIndex = -99999;
static std::map<std::string, GlobalObjectInfo> gGlobalObjectInfo;
static std::map<std::string, GfxBitmap> gPresentationGraphics;
static std::vector<unsigned char> gHudPanelPixels;

struct SoundResource {
    std::string name;
    std::string linkedName;
    std::vector<signed char> pcm;
    int sampleRate = 11025;
    int volume = 64;
    int loop = 0;
    int type = 0;
    int code = 0;
};

struct SoundVoice {
    const SoundResource* resource = nullptr;
    double position = 0.0;
    float left = 0.5f, right = 0.5f;
    int exclusiveGroup = 0;
    bool allowLoop = false;
    bool protectedVoice = false;
};

static std::map<std::string, SoundResource> gSoundResources;
static std::vector<SoundVoice> gSoundVoices;
static std::mutex gAudioMutex;

enum FrontendState {
    FRONTEND_LOGO1,
    FRONTEND_LOGO2,
    FRONTEND_TITLE,
    FRONTEND_MENU,
    FRONTEND_SOUND,
    FRONTEND_CONTROLS,
    FRONTEND_GAME_OPTIONS,
    FRONTEND_CREDITS,
    FRONTEND_LOADING,
    FRONTEND_GAME,
    FRONTEND_PAUSE,
    FRONTEND_TERMINAL
};

static void setFrontendState(FrontendState state);
static void selectLevelRelative(int direction, const char* source);
static void applyGodModeLoadout();
static void syncPlayerHeightFromCurrentCell(bool forceLog);
static void beginTeleportTransition();
static bool saveGameProgress();
static void markGameProgressDirty();
static void resetSavedGame();

static FrontendState gFrontendState = FRONTEND_LOGO1;
static double gFrontendStateSince = 0.0;
static int gFrontendMenuSelection = 0;
static int gSoundMenuSelection = 0;
static int gControlsMenuSelection = 0;
static int gControlCapture = -1;
static int gPauseMenuSelection = 0;
static int gMusicVolume = 4;
static int gSoundVolume = 5;
static bool gMusicEnabled = true;
static std::atomic<bool> gSoundEnabled(true);
static bool gQuitRequested = false;
static int gFireKey = 105;       // R2
static int gActivateKey = 96;    // A
static int gWeaponKey = 99;      // X
static int gRunKey = 102;        // L1
static int gMenuKey = 108;       // START
static bool gFireHeld = false;
static bool gFireLatch = false;
static bool gRunHeld = false;
static double gNextAutoFireTime = 0.0;
static double gFireReleaseDeadline = 0.0;
static const double AUTO_FIRE_INTERVAL = 0.40; // previous version: 2.5 shots/second
static bool gGodMode = false;
static bool gCheatShoulders[4] = {false, false, false, false};
static bool gCheatChordLatch = false;
static double gGodModeMessageUntil = 0.0;
static double gGameResetMessageUntil = 0.0;
static bool gSaveDirty = false;
static double gLastProgressSaveTime = 0.0;
static std::mutex gProgressSaveMutex;

static std::vector<OrigVtHit> gOrigCenterVTable;
static int gOrigCenterBlock = 0;
static int gOrigCenterFloor = 0;
static int gOrigCenterCeil = 0;
static int gOrigCenterIllum = 0;
static int gOrigCenterAttr = 0;
static int gOrigProbeLastAsset = -99999;
static int gOrigProbeLastBlock = -99999;
static double gOrigProbeLastLog = 0.0;

// Original Breathless player/height constants from Sorgenti/TMap.i.
// v63 starts using them for movement/collision only. Rendering stays on the
// confirmed v54 wall-projection plane path; upper/lower wall rendering comes later.
static const int ORIG_PLAYER_HEIGHT = 56;
static const int ORIG_PLAYER_EYES_HEIGHT = 54;
// v63: allow one visible 32-unit step. v56 used 24 from the original notes,
// but the tested LGLD maps contain normal-looking 32-unit stair transitions.
static const int ORIG_PLAYER_MAX_RISE = 24;
static const int ORIG_PLAYER_MAX_DROP = 32767; // renderer-only legacy diagnostic; gameplay permits arbitrary drops

static int gHeightProbeCellX = 0;
static int gHeightProbeCellY = 0;
static int gHeightProbeRaw = 0;
static int gHeightProbeBlock = 0;
static int gHeightProbeFloor = 0;
static int gHeightProbeCeil = 0;
static int gHeightProbeFloorTex = 0;
static int gHeightProbeCeilTex = 0;
static int gHeightProbeIllum = 0;
static unsigned int gHeightProbeAttr = 0;
static unsigned int gHeightProbeEffect = 0;
static unsigned int gHeightProbeTrigger = 0;
static unsigned int gHeightProbeTrigger2 = 0;
static int gHeightProbeEdge[4] = {0, 0, 0, 0};
static int gHeightProbeNeighborBlock[4] = {0, 0, 0, 0};
static int gHeightProbeNeighborFloorDelta[4] = {0, 0, 0, 0};
static int gHeightProbeNeighborGap[4] = {0, 0, 0, 0};
static int gHeightProbeNeighborRaw[4] = {0, 0, 0, 0};
static double gHeightProbeLastLog = 0.0;
static int gHeightProbeLastAsset = -99999;
static int gHeightProbeLastCellX = -99999;
static int gHeightProbeLastCellY = -99999;
static int gHeightProbeLastBlock = -99999;

static int gOrigSpanLastAsset = -99999;
static int gOrigSpanLastBlock = -99999;
static double gOrigSpanLastBuild = 0.0;
static double gOrigSpanLastLog = 0.0;
static int gOrigSpanColumns = 0;
static int gOrigSpanCeilSegments = 0;
static int gOrigSpanFloorSegments = 0;
static int gOrigSpanUpperChanges = 0;
static int gOrigSpanLowerChanges = 0;
static int gOrigSpanMaxHits = 0;
static int gOrigSpanClosedColumns = 0;
static int gOrigSpanCeilPixels = 0;
static int gOrigSpanFloorPixels = 0;
static std::vector<OrigSpan> gOrigSpans;


static std::vector<AssetInfo> gAssetInfos;
static int gAssetIndex = 0;
static int gLastLevelAssetIndex = -9999;

static float gAnalogLX = 0.0f;
static float gAnalogLY = 0.0f;
static float gAnalogRX = 0.0f;
static float gAnalogRY = 0.0f;
static float gPlayerX = 1.5f;
static float gPlayerY = 1.5f;
static float gPlayerA = 0.0f;
static int gPlayerBaseZ = 0;
static int gPlayerTargetBaseZ = 0;
static int gPlayerCeilZ = 128;
static int gPlayerEyeZ = ORIG_PLAYER_EYES_HEIGHT;
static int gPlayerLastCellX = -9999;
static int gPlayerLastCellY = -9999;
static int gPlayerLastBlockIndex = -9999;
static double gPlayerHeightLastLog = 0.0;
static bool gPlayerStartChecked = false;
static double gMoveLastTime = 0.0;
static float gPlayerBaseZF = 0.0f;
static float gPlayerVerticalSpeed = 0.0f;
static float gPlayerBobPhase = 0.0f;
static float gPlayerBobOffset = 0.0f;
static bool gPlayerFalling = false;
static int gPlayerFallStartZ = 0;
static int gPlayerHealth = 100;
static int gPlayerShields = 100;
static int gPlayerEnergy = 1000;
static int gPlayerCredits = 0;
static int gPlayerScore = 0;
static const int PLAYER_WEAPON_COUNT = 6;
static int gPlayerWeapon = 0;
static unsigned char gPlayerWeapons[PLAYER_WEAPON_COUNT] = {1,0,0,0,0,0};
static bool gPlayerKeys[4] = {false,false,false,false};
static bool gPlayerProgressValid = false;
static bool gRestoreLevelCheckpoint = false;
static int gCheckpointHealth = 100, gCheckpointShields = 100, gCheckpointEnergy = 1000;
static int gCheckpointCredits = 0, gCheckpointScore = 0, gCheckpointWeapon = 0;
static unsigned char gCheckpointWeapons[PLAYER_WEAPON_COUNT] = {1,0,0,0,0,0};
static bool gCheckpointKeys[4] = {false,false,false,false};
static std::vector<RuntimeObject> gRuntimeObjects;
static std::vector<RuntimeProjectile> gRuntimeProjectiles;
static std::vector<RuntimeImpactSpark> gRuntimeImpactSparks;
static double gObjectLastTime = 0.0;
static double gPickupMessageUntil = 0.0;
static std::string gPickupMessage;
static double gHazardClock = 0.0;
static double gRedFlashUntil = 0.0;
static bool gPlayerDead = false;
static int gPlayerDeathEyeHeight = ORIG_PLAYER_EYES_HEIGHT;
static int gPlayerDeathWaitTicks = 60;
static float gPlayerDeathTickAccumulator = 0.0f;
static int gPlayerRetries = 3;
static bool gLevelExitActive = false;
static double gLevelExitStarted = 0.0;
static double gLevelExitCompleteAfter = 1.15;
static bool gTeleportActive = false;
static double gTeleportStarted = 0.0;
static double gTeleportCompleteAfter = 32.0 / 50.0;
static const int TELEPORT_SOUND_GROUP = 2;
// One-shot audio is produced by a separate Android thread. Keep transitions
// alive for a few output buffers after the last decoded sample, rather than
// tearing down the scene on the exact final-sample timestamp.
static const double TELEPORT_AUDIO_TAIL_SECONDS = 0.12;
static int gRuntimeTerminalNumber = 0;
static int gRuntimeTerminalPage = 0;
static int gRuntimeTerminalSelection = 0;
static std::vector<unsigned int> gRuntimeTerminalBackground;

struct ActiveLevelEffect {
    LgldEffectCommand command;
    unsigned int listIndex = 0;
    int phase = 0;
    float remaining = 0.0f;
    float fractional = 0.0f;
    int appliedLightDelta = 0;
    bool finished = false;
};

static int gRuntimeAssetIndex = -99999;
static std::vector<LgldBlockInfo> gRuntimeBlocks;
static std::vector<LgldBlockInfo> gInitialRuntimeBlocks;
static std::vector<ActiveLevelEffect> gActiveEffects;
static std::set<unsigned int> gPermanentEffectLists;
static std::set<unsigned int> gActiveEnemyTriggers;
static std::set<unsigned int> gActivatedSwitchParts;
// Nearest opaque world surface for sprite/object clipping. Plane distances are
// stored here too; using wall-only depth allowed objects from lower sectors to
// bleed through a nearer floor or door sill.
static std::vector<float> gWallDepth(FB_W * FB_H, 1.0e30f);

static const float SKY_SCROLL_SCALE_V33 = 168.0f;
static const float SKY_VERTICAL_SCALE_V33 = 0.82f;

static GLuint compileShader(GLenum type, const char* src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, 0);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof(log), 0, log);
        LOGE("shader compile failed: %s", log);
    }
    return shader;
}

static unsigned int be16(const unsigned char* p) {
    return ((unsigned int)p[0] << 8) | (unsigned int)p[1];
}

static unsigned int be32(const unsigned char* p) {
    return ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) | ((unsigned int)p[2] << 8) | (unsigned int)p[3];
}

static short beS16(const unsigned char* p) {
    return (short)be16(p);
}

static int beS32(const unsigned char* p) {
    return (int)be32(p);
}

static unsigned int fnv1aUpdate(unsigned int hash, const unsigned char* data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        hash ^= (unsigned int)data[i];
        hash *= 16777619u;
    }
    return hash;
}

static double nowSeconds() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

static float cameraPlaneScale() {
    // 3d.asm casts the 160 logical columns across X=-80..+80 with D=128.
    // The Amiga display doubles those logical columns; Android casts one ray
    // per square-pixel framebuffer column, so extend the original ray step to
    // FB_W. This reveals real side scenery rather than stretching 4:3 pixels.
    return (80.0f / 128.0f) * (float)FB_W / 160.0f;
}

static const char* abiName() {
#if defined(__aarch64__)
    return "ARM64";
#elif defined(__arm__)
    return "ARM32";
#elif defined(__i386__)
    return "X86";
#elif defined(__x86_64__)
    return "X64";
#else
    return "ABI?";
#endif
}

static void resetPaletteProbe() {
    gPaletteOk = false;
    gPaletteChecksum = 0;
    gPaletteCount = 0;
    memset(gPalette, 0, sizeof(gPalette));
}

static void resetGldProbe() {
    gFirstGldName.clear();
    gFirstGldSize = -1;
    gFirstGldChecksum = 0;
    memset(gFirstGldHead, 0, sizeof(gFirstGldHead));
    gFirstGldHeadLen = 0;
    gFirstGldProbeOk = false;
    gGldDirProbeOk = false;
    gGldDirOffset = 0;
    gGldDirCount = 0;
    gGldEntry0Name.clear();
    gGldEntry0Offset = 0;
    gGldEntry0Length = 0;
    gGldEntry0TextureOk = false;
    gTex0Width = gTex0Anim = gTex0Height = gTex0HShift = 0;
    gTex0Frame = gTex0Zero = 0;
    gTex0Raw.clear();
    gTex0RawOk = false;
    gTex0RawChecksum = 0;
    gTex0RawMin = 0;
    gTex0RawMax = 0;
    gTextureEntries.clear();
    gTextureIndex = 0;
    gSkyTex = TextureBitmap();
    for (int i = 0; i < 3; ++i) gFloorTex[i] = TextureBitmap();
    for (int i = 0; i < 8; ++i) gWallTex[i] = TextureBitmap();
    gWallTexCount = 0;
    gFloorTexCount = 0;
    gLevelTextureCache.clear();
    gLevelSwitchTextureCache.clear();
    gLevelTextureCacheAssetIndex = -99999;
    gPresentationGraphics.clear();
    gHudPanelPixels.clear();
}

static void probeGfxPalette() {
    resetPaletteProbe();
    const std::string fullPath = gDataPath + "/BLES0002.GLD";
    FILE* f = fopen(fullPath.c_str(), "rb");
    if (!f) { LOGE("GGLD palette open failed: %s", fullPath.c_str()); return; }

    unsigned char hdr[10];
    if (fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr)) { fclose(f); return; }
    if (memcmp(hdr, "GGLD", 4) != 0) { fclose(f); LOGE("GGLD palette bad id in BLES0002.GLD"); return; }
    const unsigned int numPalettes = be16(hdr + 8);
    if (numPalettes == 0) { fclose(f); return; }
    if (fread(gPalette, 1, sizeof(gPalette), f) != sizeof(gPalette)) { fclose(f); return; }
    fclose(f);

    unsigned int hash = 2166136261u;
    hash = fnv1aUpdate(hash, gPalette, sizeof(gPalette));
    gPaletteCount = numPalettes;
    gPaletteChecksum = hash;
    gPaletteOk = true;
    LOGI("GGLD palette ok file=BLES0002.GLD palettes=%u firstPaletteChk=%08x p0=%02x%02x%02x p16=%02x%02x%02x p255=%02x%02x%02x",
         gPaletteCount, gPaletteChecksum,
         gPalette[0], gPalette[1], gPalette[2],
         gPalette[16 * 3 + 0], gPalette[16 * 3 + 1], gPalette[16 * 3 + 2],
         gPalette[255 * 3 + 0], gPalette[255 * 3 + 1], gPalette[255 * 3 + 2]);
}

static bool unpackVirtualDreams(const unsigned char* source, size_t sourceBytes,
                                std::vector<unsigned char>& output, size_t outputBytes) {
    output.clear();
    if (!source || sourceBytes < 2u || outputBytes == 0u || outputBytes > 1024u * 1024u) return false;
    const unsigned int type = source[0];
    const unsigned int mask = type == 0u ? 15u : 31u;
    const unsigned int shift = type == 0u ? 4u : 3u;
    size_t sp = 1u;
    output.reserve(outputBytes);
    while (sp < sourceBytes && output.size() < outputBytes) {
        const unsigned int tag = source[sp++];
        if (tag == 0u) {
            if (sp + 8u > sourceBytes || output.size() + 8u > outputBytes) return false;
            output.insert(output.end(), source + sp, source + sp + 8u);
            sp += 8u;
            continue;
        }
        for (int bit = 7; bit >= 0 && output.size() < outputBytes; --bit) {
            if ((tag & (1u << bit)) == 0u) {
                if (sp >= sourceBytes) return false;
                output.push_back(source[sp++]);
                continue;
            }
            if (sp >= sourceBytes) return false;
            const unsigned int code = source[sp++];
            if (code == 0u) return output.size() == outputBytes;
            if (sp >= sourceBytes) return false;
            const unsigned int distance = ((code & ~mask) << shift) | source[sp++];
            const unsigned int count = (code & mask) + 2u;
            if (distance == 0u || distance > output.size() || output.size() + count > outputBytes) return false;
            for (unsigned int i = 0; i < count; ++i) output.push_back(output[output.size() - distance]);
        }
    }
    return output.size() == outputBytes;
}

static void loadPresentationGraphics() {
    gPresentationGraphics.clear();
    const std::string path = gDataPath + "/BLES0002.GLD";
    FILE* file = fopen(path.c_str(), "rb");
    if (!file) return;
    unsigned char fileHeader[10];
    if (fread(fileHeader, 1, sizeof(fileHeader), file) != sizeof(fileHeader) || memcmp(fileHeader, "GGLD", 4) != 0) {
        fclose(file);
        return;
    }
    const unsigned int directoryOffset = be32(fileHeader + 4);
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return; }
    const long fileSize = ftell(file);
    if (directoryOffset >= (unsigned int)fileSize || fseek(file, (long)directoryOffset, SEEK_SET) != 0) {
        fclose(file);
        return;
    }
    unsigned char countBytes[4];
    if (fread(countBytes, 1, 4, file) != 4) { fclose(file); return; }
    const unsigned int count = be32(countBytes);
    for (unsigned int i = 0; i < count && i < 256u; ++i) {
        unsigned char entry[12];
        if (fread(entry, 1, sizeof(entry), file) != sizeof(entry)) break;
        const long directoryPosition = ftell(file);
        char pictureName[5];
        memcpy(pictureName, entry, 4);
        pictureName[4] = 0;
        const std::string name = pictureName;
        const unsigned int offset = be32(entry + 4);
        const unsigned int length = be32(entry + 8);
        if (length >= 12u && offset + length <= (unsigned int)fileSize && fseek(file, (long)offset, SEEK_SET) == 0) {
            std::vector<unsigned char> bytes(length);
            if (fread(&bytes[0], 1, bytes.size(), file) == bytes.size()) {
                GfxBitmap picture;
                picture.name = name;
                picture.x = (int)be16(&bytes[4]);
                picture.y = (int)be16(&bytes[6]);
                picture.width = (int)be16(&bytes[8]);
                picture.height = (int)be16(&bytes[10]);
                const size_t expected = 768u + (size_t)picture.width * (size_t)picture.height;
                std::vector<unsigned char> unpacked;
                if (picture.width > 0 && picture.width <= 320 && picture.height > 0 && picture.height <= 256) {
                    if (bytes.size() >= 24u && memcmp(&bytes[12], "VDCO", 4) == 0) {
                        const unsigned int outputSize = be32(&bytes[16]);
                        const unsigned int packedSize = be32(&bytes[20]);
                        if (outputSize == expected && 24u + packedSize + 1u <= bytes.size())
                            unpackVirtualDreams(&bytes[24], packedSize + 1u, unpacked, outputSize);
                    } else if (bytes.size() >= 12u + expected) {
                        unpacked.assign(bytes.begin() + 12, bytes.begin() + 12 + expected);
                    }
                    if (unpacked.size() == expected) {
                        picture.palette.assign(unpacked.begin(), unpacked.begin() + 768);
                        picture.pixels.assign(unpacked.begin() + 768, unpacked.end());
                        picture.ok = true;
                        gPresentationGraphics[name] = picture;
                    }
                }
            }
        }
        fseek(file, directoryPosition, SEEK_SET);
    }
    fclose(file);
    LOGI("presentation gfx loaded count=%u", (unsigned int)gPresentationGraphics.size());
}

static void loadHudPanel() {
    gHudPanelPixels.clear();
    std::string path = gDataPath + "/Panel04.raw";
    FILE* file = fopen(path.c_str(), "rb");
    if (!file) {
        path = gDataPath + "/Sorgenti/Graphic/Panel04.raw";
        file = fopen(path.c_str(), "rb");
    }
    if (!file) return;
    std::vector<unsigned char> planar(12800u);
    const bool readOk = fread(&planar[0], 1, planar.size(), file) == planar.size();
    fclose(file);
    if (!readOk) return;
    const size_t planeBytes = 40u * 40u;
    gHudPanelPixels.assign(320u * 40u, 0);
    for (int y = 0; y < 40; ++y) for (int x = 0; x < 320; ++x) {
        unsigned int paletteIndex = 0;
        for (int plane = 0; plane < 8; ++plane) {
            const unsigned char value = planar[(size_t)plane * planeBytes + (size_t)y * 40u + (size_t)x / 8u];
            paletteIndex |= ((value >> (7 - (x & 7))) & 1u) << plane;
        }
        gHudPanelPixels[(size_t)y * 320u + (size_t)x] = (unsigned char)paletteIndex;
    }
    LOGI("original HUD panel loaded");
}

static unsigned int paletteColor(unsigned char idx) {
    if (gPaletteOk) {
        const unsigned int r = gPalette[(unsigned int)idx * 3u + 0u];
        const unsigned int g = gPalette[(unsigned int)idx * 3u + 1u];
        const unsigned int b = gPalette[(unsigned int)idx * 3u + 2u];
        return 0xff000000u | (b << 16) | (g << 8) | r;
    }
    const unsigned int v = idx;
    return 0xff000000u | (v << 16) | (v << 8) | v;
}

static void putPixelSafe(int x, int y, unsigned int argb) {
    if ((unsigned)x >= (unsigned)FB_W || (unsigned)y >= (unsigned)FB_H) return;
    gFramebuffer[y * FB_W + x] = argb;
}

static const unsigned char* glyph5x7(char c) {
    static const unsigned char blank[7] = {0,0,0,0,0,0,0};
    static const unsigned char glyphs[][7] = {
        {0x0e,0x11,0x13,0x15,0x19,0x11,0x0e},{0x04,0x0c,0x04,0x04,0x04,0x04,0x0e},
        {0x0e,0x11,0x01,0x02,0x04,0x08,0x1f},{0x1e,0x01,0x01,0x0e,0x01,0x01,0x1e},
        {0x02,0x06,0x0a,0x12,0x1f,0x02,0x02},{0x1f,0x10,0x10,0x1e,0x01,0x01,0x1e},
        {0x0e,0x10,0x10,0x1e,0x11,0x11,0x0e},{0x1f,0x01,0x02,0x04,0x08,0x08,0x08},
        {0x0e,0x11,0x11,0x0e,0x11,0x11,0x0e},{0x0e,0x11,0x11,0x0f,0x01,0x01,0x0e},
        {0x0e,0x11,0x11,0x1f,0x11,0x11,0x11},{0x1e,0x11,0x11,0x1e,0x11,0x11,0x1e},
        {0x0e,0x11,0x10,0x10,0x10,0x11,0x0e},{0x1e,0x11,0x11,0x11,0x11,0x11,0x1e},
        {0x1f,0x10,0x10,0x1e,0x10,0x10,0x1f},{0x1f,0x10,0x10,0x1e,0x10,0x10,0x10},
        {0x0e,0x11,0x10,0x17,0x11,0x11,0x0f},{0x11,0x11,0x11,0x1f,0x11,0x11,0x11},
        {0x0e,0x04,0x04,0x04,0x04,0x04,0x0e},{0x07,0x02,0x02,0x02,0x12,0x12,0x0c},
        {0x11,0x12,0x14,0x18,0x14,0x12,0x11},{0x10,0x10,0x10,0x10,0x10,0x10,0x1f},
        {0x11,0x1b,0x15,0x15,0x11,0x11,0x11},{0x11,0x19,0x15,0x13,0x11,0x11,0x11},
        {0x0e,0x11,0x11,0x11,0x11,0x11,0x0e},{0x1e,0x11,0x11,0x1e,0x10,0x10,0x10},
        {0x0e,0x11,0x11,0x11,0x15,0x12,0x0d},{0x1e,0x11,0x11,0x1e,0x14,0x12,0x11},
        {0x0f,0x10,0x10,0x0e,0x01,0x01,0x1e},{0x1f,0x04,0x04,0x04,0x04,0x04,0x04},
        {0x11,0x11,0x11,0x11,0x11,0x11,0x0e},{0x11,0x11,0x11,0x11,0x11,0x0a,0x04},
        {0x11,0x11,0x11,0x15,0x15,0x1b,0x11},{0x11,0x11,0x0a,0x04,0x0a,0x11,0x11},
        {0x11,0x11,0x0a,0x04,0x04,0x04,0x04},{0x1f,0x01,0x02,0x04,0x08,0x10,0x1f},
        {0x00,0x00,0x00,0x1f,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x0c,0x0c},
        {0x00,0x04,0x04,0x00,0x04,0x04,0x00},{0x00,0x00,0x04,0x00,0x04,0x00,0x00},
        {0x00,0x04,0x00,0x1f,0x00,0x04,0x00},{0x01,0x02,0x04,0x08,0x10,0x00,0x00},
        {0x00,0x0a,0x0a,0x00,0x0a,0x0a,0x00},{0x0e,0x11,0x01,0x02,0x04,0x00,0x04}
    };
    if (c >= '0' && c <= '9') return glyphs[c - '0'];
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    if (c >= 'A' && c <= 'Z') return glyphs[10 + (c - 'A')];
    switch (c) { case '-': return glyphs[36]; case '.': return glyphs[37]; case ':': return glyphs[38]; case ';': return glyphs[39]; case '+': return glyphs[40]; case '/': return glyphs[41]; case '=': return glyphs[42]; case '?': return glyphs[43]; default: return blank; }
}

static void drawChar5x7(int x, int y, char c, unsigned int fg, unsigned int bg) {
    const unsigned char* g = glyph5x7(c);
    for (int row = 0; row < 7; ++row) for (int col = 0; col < 5; ++col) {
        const bool on = (g[row] & (1 << (4 - col))) != 0;
        if (on) putPixelSafe(x + col, y + row, fg);
        else if (bg) putPixelSafe(x + col, y + row, bg);
    }
}

static void drawText(int x, int y, const char* text) {
    const unsigned int fg = 0xffffffffu;
    int px = x;
    while (*text) { if (*text == ' ') px += 4; else { drawChar5x7(px, y, *text, fg, 0); px += 6; } ++text; }
}

static int textWidth(const char* text) {
    int width = 0;
    while (*text) { width += (*text == ' ') ? 4 : 6; ++text; }
    return width > 0 ? width - 1 : 0;
}

static void drawTextCentered(int y, const char* text) {
    drawText((FB_W - textWidth(text)) / 2, y, text);
}

// Exact bitplane glyph data used by Breathless' CaratteriDigital/CaratteriMini.
static const unsigned char HUD_DIGITAL[10][4][9] = {
{{0x70,0x88,0x88,0x88,0x70,0x88,0x88,0x88,0x70},{0,0,0,0,0x70,0,0,0,0},{0xfc,0xfc,0xfc,0xfc,0x8c,0xfc,0xfc,0xfc,0xfc},{0x8c,0x74,0x74,0x74,0xfc,0x74,0x74,0x74,0x8c}},
{{0x70,0x88,0x88,0x88,0x70,0x88,0x88,0x88,0x70},{0x70,0x80,0x80,0x80,0x70,0x80,0x80,0x80,0x70},{0x8c,0x7c,0x7c,0x7c,0x8c,0x7c,0x7c,0x7c,0x8c},{0xfc,0xf4,0xf4,0xf4,0xfc,0xf4,0xf4,0xf4,0xfc}},
{{0x70,0x88,0x88,0x88,0x70,0x88,0x88,0x88,0x70},{0,0x80,0x80,0x80,0,0x08,0x08,0x08,0},{0xfc,0x7c,0x7c,0x7c,0xfc,0xf4,0xf4,0xf4,0xfc},{0x8c,0xf4,0xf4,0xf4,0x8c,0x7c,0x7c,0x7c,0x8c}},
{{0x70,0x88,0x88,0x88,0x70,0x88,0x88,0x88,0x70},{0,0x80,0x80,0x80,0,0x80,0x80,0x80,0},{0xfc,0x7c,0x7c,0x7c,0xfc,0x7c,0x7c,0x7c,0xfc},{0x8c,0xf4,0xf4,0xf4,0x8c,0xf4,0xf4,0xf4,0x8c}},
{{0x70,0x88,0x88,0x88,0x70,0x88,0x88,0x88,0x70},{0x70,0,0,0,0,0x80,0x80,0x80,0x70},{0x8c,0xfc,0xfc,0xfc,0xfc,0x7c,0x7c,0x7c,0x8c},{0xfc,0x74,0x74,0x74,0x8c,0xf4,0xf4,0xf4,0xfc}},
{{0x70,0x88,0x88,0x88,0x70,0x88,0x88,0x88,0x70},{0,0x08,0x08,0x08,0,0x80,0x80,0x80,0},{0xfc,0xf4,0xf4,0xf4,0xfc,0x7c,0x7c,0x7c,0xfc},{0x8c,0x7c,0x7c,0x7c,0x8c,0xf4,0xf4,0xf4,0x8c}},
{{0x70,0x88,0x88,0x88,0x70,0x88,0x88,0x88,0x70},{0,0x08,0x08,0x08,0,0,0,0,0},{0xfc,0xf4,0xf4,0xf4,0xfc,0xfc,0xfc,0xfc,0xfc},{0x8c,0x7c,0x7c,0x7c,0x8c,0x74,0x74,0x74,0x8c}},
{{0x70,0x88,0x88,0x88,0x70,0x88,0x88,0x88,0x70},{0,0x80,0x80,0x80,0x70,0x80,0x80,0x80,0x70},{0xfc,0x7c,0x7c,0x7c,0x8c,0x7c,0x7c,0x7c,0x8c},{0x8c,0xf4,0xf4,0xf4,0xfc,0xf4,0xf4,0xf4,0xfc}},
{{0x70,0x88,0x88,0x88,0x70,0x88,0x88,0x88,0x70},{0,0,0,0,0,0,0,0,0},{0xfc,0xfc,0xfc,0xfc,0xfc,0xfc,0xfc,0xfc,0xfc},{0x8c,0x74,0x74,0x74,0x8c,0x74,0x74,0x74,0x8c}},
{{0x70,0x88,0x88,0x88,0x70,0x88,0x88,0x88,0x70},{0,0,0,0,0,0x80,0x80,0x80,0},{0xfc,0xfc,0xfc,0xfc,0xfc,0x7c,0x7c,0x7c,0xfc},{0x8c,0x74,0x74,0x74,0x8c,0xf4,0xf4,0xf4,0x8c}}
};

static const unsigned char HUD_MINI[10][2][5] = {
{{0xe0,0xa0,0xa0,0xa0,0xe0},{0x10,0x50,0x50,0x50,0x10}},{{0x20,0x20,0x20,0x20,0x20},{0xd0,0xd0,0xd0,0xd0,0xd0}},
{{0xe0,0x20,0xe0,0x80,0xe0},{0x10,0xd0,0x10,0x70,0x10}},{{0xe0,0x20,0x60,0x20,0xe0},{0x10,0xd0,0x90,0xd0,0x10}},
{{0x80,0x80,0xa0,0xe0,0x20},{0x70,0x70,0x50,0x10,0xd0}},{{0xe0,0x80,0xe0,0x20,0xe0},{0x10,0x70,0x10,0xd0,0x10}},
{{0x80,0x80,0xe0,0xa0,0xe0},{0x70,0x70,0x10,0x50,0x10}},{{0xe0,0x20,0x20,0x20,0x20},{0x10,0xd0,0xd0,0xd0,0xd0}},
{{0xe0,0xa0,0xe0,0xa0,0xe0},{0x10,0x50,0x10,0x50,0x10}},{{0xe0,0xa0,0xe0,0x20,0x20},{0x10,0x50,0x10,0xd0,0xd0}}
};

static void drawOriginalHudNumber(int x, int panelY, int value, int digits, bool mini) {
    char number[16];
    snprintf(number, sizeof(number), "%0*d", digits, std::max(0, value));
    const int width = mini ? 4 : 6, height = mini ? 5 : 9;
    for (int digitIndex = 0; digitIndex < digits; ++digitIndex) {
        const int digit = number[digitIndex] >= '0' && number[digitIndex] <= '9' ? number[digitIndex] - '0' : 0;
        for (int row = 0; row < height; ++row) for (int col = 0; col < width; ++col) {
            const int px = x + digitIndex * width + col;
            if ((unsigned)px >= 320u || (unsigned)(panelY + row) >= 40u) continue;
            unsigned char paletteIndex = gHudPanelPixels[(size_t)(panelY + row) * 320u + (size_t)px];
            const unsigned char bit = (unsigned char)(0x80u >> col);
            if (mini) {
                if (HUD_MINI[digit][0][row] & bit) paletteIndex |= 1u; else paletteIndex &= (unsigned char)~1u;
                if (HUD_MINI[digit][1][row] & bit) paletteIndex |= 16u; else paletteIndex &= (unsigned char)~16u;
            } else {
                static const unsigned char planes[4] = {1u,4u,8u,16u};
                for (int p = 0; p < 4; ++p) {
                    if (HUD_DIGITAL[digit][p][row] & bit) paletteIndex |= planes[p];
                    else paletteIndex &= (unsigned char)~planes[p];
                }
            }
            gFramebuffer[(size_t)(160 + panelY + row) * FB_W + (size_t)(HUD_X + px)] = paletteColor(paletteIndex);
        }
    }
}

static unsigned char texturePixelColumnMajor(const std::vector<unsigned char>& raw, unsigned int width, unsigned int height, unsigned int x, unsigned int y) {
    if (width == 0 || height == 0 || raw.empty()) return 0;
    x %= width; y %= height;
    const size_t pos = (size_t)x * (size_t)height + (size_t)y;
    if (pos >= raw.size()) return 0;
    return raw[pos];
}

static unsigned char sampleColumnMajorTexture(const std::vector<unsigned char>& raw, unsigned int width, unsigned int height, int x, int y) {
    if (width == 0 || height == 0 || raw.empty()) return 0;
    int xx = x % (int)width, yy = y % (int)height;
    if (xx < 0) xx += (int)width;
    if (yy < 0) yy += (int)height;
    return texturePixelColumnMajor(raw, width, height, (unsigned int)xx, (unsigned int)yy);
}

static unsigned char sampleTextureBitmap(const TextureBitmap& tb, int x, int y) {
    if (!tb.ok || tb.width == 0 || tb.height == 0 || tb.raw.empty()) return 0;
    const unsigned int frameSize = tb.width * tb.height;
    unsigned int frame = 0;
    if (tb.frames > 1 && tb.raw.size() >= (size_t)frameSize * (size_t)tb.frames) {
        // Original TGLD textures can contain multiple animation brushes in one entry
        // (for example Blob/animated wall textures). Advance softly at ~8 fps.
        frame = (unsigned int)(nowSeconds() * 8.0) % tb.frames;
    }
    const size_t base = (size_t)frame * (size_t)frameSize;
    int xx = x % (int)tb.width;
    int yy = y % (int)tb.height;
    if (xx < 0) xx += (int)tb.width;
    if (yy < 0) yy += (int)tb.height;
    const size_t pos = base + (size_t)xx * (size_t)tb.height + (size_t)yy;
    if (pos >= tb.raw.size()) return 0;
    return tb.raw[pos];
}

static unsigned int shadeColor(unsigned int col, int light) {
    if (light < 24) light = 24;
    if (light > 256) light = 256;
    unsigned int r = col & 0xffu, g = (col >> 8) & 0xffu, b = (col >> 16) & 0xffu;
    r = (r * (unsigned int)light) >> 8; g = (g * (unsigned int)light) >> 8; b = (b * (unsigned int)light) >> 8;
    return 0xff000000u | (b << 16) | (g << 8) | r;
}

static void updateFps() {
    const double t = nowSeconds();
    if (gFpsLastTime == 0.0) gFpsLastTime = t;
    ++gFpsFrames;
    const double dt = t - gFpsLastTime;
    if (dt >= 0.50) { gFps = (double)gFpsFrames / dt; gFpsFrames = 0; gFpsLastTime = t; }
}

static std::string trimRightSpaces(std::string v) {
    while (!v.empty() && (v.back() == ' ' || v.back() == '\0')) v.pop_back();
    return v;
}

static bool namesEqualLoose(const std::string& a, const std::string& b) {
    return trimRightSpaces(a) == trimRightSpaces(b);
}

static bool readTexturePixelsByEntry(const std::string& fullPath, const TextureEntry& te, std::vector<unsigned char>& out, unsigned int& width, unsigned int& height) {
    out.clear(); width = height = 0;
    if (!te.headerOk || te.width == 0 || te.height == 0 || te.width > 512 || te.height > 512) return false;
    const unsigned int pixelBytes = te.width * te.height;
    const unsigned int rawStart = te.offset + 20u;
    if (pixelBytes == 0 || pixelBytes > te.length || rawStart + pixelBytes > (unsigned int)gFirstGldSize) return false;
    FILE* f = fopen(fullPath.c_str(), "rb");
    if (!f) return false;
    out.assign(pixelBytes, 0);
    const bool ok = (fseek(f, (long)rawStart, SEEK_SET) == 0 && fread(&out[0], 1, pixelBytes, f) == pixelBytes);
    fclose(f);
    if (!ok) { out.clear(); return false; }
    width = te.width; height = te.height;
    return true;
}


static bool readTextureBitmapByEntry(const std::string& fullPath, const TextureEntry& te, TextureBitmap& out) {
    out = TextureBitmap();
    out.name = te.name;
    if (!te.headerOk || te.width == 0 || te.height == 0 || te.width > 512 || te.height > 512) return false;
    const unsigned int frameSize = te.width * te.height;
    unsigned int frames = te.anim;
    if (frames < 1) frames = 1;
    if (frames > 16) frames = 1;
    unsigned int totalBytes = frameSize * frames;
    const unsigned int rawStart = te.offset + 20u;
    if (totalBytes == 0 || totalBytes > te.length || rawStart + totalBytes > (unsigned int)gFirstGldSize) {
        frames = 1;
        totalBytes = frameSize;
    }
    if (totalBytes == 0 || totalBytes > te.length || rawStart + totalBytes > (unsigned int)gFirstGldSize) return false;
    FILE* f = fopen(fullPath.c_str(), "rb");
    if (!f) return false;
    out.raw.assign(totalBytes, 0);
    const bool ok = (fseek(f, (long)rawStart, SEEK_SET) == 0 && fread(&out.raw[0], 1, totalBytes, f) == totalBytes);
    fclose(f);
    if (!ok) { out = TextureBitmap(); return false; }
    out.width = te.width;
    out.height = te.height;
    out.frames = frames;
    out.ok = true;
    return true;
}

static bool readSwitchOnTextureByEntry(const std::string& fullPath, const TextureEntry& te, TextureBitmap& out) {
    out = TextureBitmap();
    out.name = te.name + ":ON";
    // Non-animated switch textures store a relative link in tx_AnimCount.
    // It points from the first texture's Height field to the second Height
    // field, so the embedded ON header starts at entry offset + te.zero.
    if (!te.headerOk || te.anim > 1u || te.zero == 0u) return false;
    const unsigned int headerOffset = te.offset + te.zero;
    if (headerOffset + 20u > (unsigned int)gFirstGldSize ||
        headerOffset < te.offset || headerOffset >= te.offset + te.length) return false;
    FILE* f = fopen(fullPath.c_str(), "rb");
    if (!f) return false;
    unsigned char header[20];
    bool ok = fseek(f, (long)headerOffset, SEEK_SET) == 0 && fread(header, 1, sizeof(header), f) == sizeof(header);
    const unsigned int width = ok ? be16(header) : 0u;
    const unsigned int height = ok ? be16(header + 4) : 0u;
    const unsigned int pixelBytes = width * height;
    if (!ok || width == 0u || width > 512u || height == 0u || height > 512u ||
        pixelBytes == 0u || headerOffset + 20u + pixelBytes > te.offset + te.length) {
        fclose(f);
        return false;
    }
    out.raw.assign(pixelBytes, 0);
    ok = fread(&out.raw[0], 1, pixelBytes, f) == pixelBytes;
    fclose(f);
    if (!ok) { out = TextureBitmap(); return false; }
    out.width = width;
    out.height = height;
    out.frames = 1;
    out.ok = true;
    return true;
}

static bool readTextureByIndex(const std::string& fullPath, int index) {
    gTex0Raw.clear(); gTex0RawOk = false; gTex0RawChecksum = 0; gTex0RawMin = 0; gTex0RawMax = 0;
    gGldEntry0TextureOk = false; gGldEntry0Name.clear(); gGldEntry0Offset = 0; gGldEntry0Length = 0;
    gTex0Width = gTex0Anim = gTex0Height = gTex0HShift = 0; gTex0Frame = gTex0Zero = 0;
    if (index < 0 || index >= (int)gTextureEntries.size()) return false;
    const TextureEntry& te = gTextureEntries[(size_t)index];
    gGldEntry0Name = te.name; gGldEntry0Offset = te.offset; gGldEntry0Length = te.length;
    gTex0Width = te.width; gTex0Height = te.height; gTex0Anim = te.anim; gTex0HShift = te.hshift; gTex0Frame = te.frame; gTex0Zero = te.zero;
    gGldEntry0TextureOk = te.headerOk;
    if (!readTexturePixelsByEntry(fullPath, te, gTex0Raw, gTex0Width, gTex0Height)) return false;
    unsigned int hash = 2166136261u; hash = fnv1aUpdate(hash, &gTex0Raw[0], gTex0Raw.size());
    unsigned int mn = 255, mx = 0;
    for (size_t i = 0; i < gTex0Raw.size(); ++i) { if (gTex0Raw[i] < mn) mn = gTex0Raw[i]; if (gTex0Raw[i] > mx) mx = gTex0Raw[i]; }
    gTex0RawChecksum = hash; gTex0RawMin = mn; gTex0RawMax = mx; gTex0RawOk = true;
    return true;
}

static bool snapshotCurrentTexture(std::vector<unsigned char>& raw, unsigned int& width, unsigned int& height) {
    std::lock_guard<std::mutex> lock(gTextureMutex);
    if (!gTex0RawOk || gTex0Width == 0 || gTex0Height == 0 || gTex0Raw.empty()) return false;
    const size_t expected = (size_t)gTex0Width * (size_t)gTex0Height;
    if (expected == 0 || expected > gTex0Raw.size()) return false;
    width = gTex0Width; height = gTex0Height;
    raw.assign(gTex0Raw.begin(), gTex0Raw.begin() + expected);
    return true;
}

static bool nameStarts(const std::string& s, const char* prefix) {
    return prefix && strncasecmp(s.c_str(), prefix, strlen(prefix)) == 0;
}

static bool loadExactTextureResource(const std::string& fullPath, const std::string& texName, TextureBitmap& out) {
    out = TextureBitmap();
    const std::string wanted = trimRightSpaces(texName);
    if (wanted.empty()) return false;
    for (size_t i = 0; i < gTextureEntries.size(); ++i) {
        if (namesEqualLoose(gTextureEntries[i].name, wanted)) {
            return readTextureBitmapByEntry(fullPath, gTextureEntries[i], out);
        }
    }
    // Safety fallback: some level texture names are prefixes without padding quirks.
    for (size_t i = 0; i < gTextureEntries.size(); ++i) {
        const std::string n = trimRightSpaces(gTextureEntries[i].name);
        if (n == wanted || (n.size() >= wanted.size() && n.compare(0, wanted.size(), wanted) == 0)) {
            return readTextureBitmapByEntry(fullPath, gTextureEntries[i], out);
        }
    }
    return false;
}

static bool loadExactSwitchTextureResource(const std::string& fullPath, const std::string& texName, TextureBitmap& out) {
    out = TextureBitmap();
    const std::string wanted = trimRightSpaces(texName);
    if (wanted.empty()) return false;
    for (size_t i = 0; i < gTextureEntries.size(); ++i) {
        if (namesEqualLoose(gTextureEntries[i].name, wanted))
            return readSwitchOnTextureByEntry(fullPath, gTextureEntries[i], out);
    }
    return false;
}

static bool loadNamedTextureResource(const std::string& fullPath, const char* prefix, TextureBitmap& out) {
    out = TextureBitmap();
    for (size_t i = 0; i < gTextureEntries.size(); ++i) {
        if (nameStarts(gTextureEntries[i].name, prefix)) {
            return readTextureBitmapByEntry(fullPath, gTextureEntries[i], out);
        }
    }
    return false;
}

static void loadSceneTextures(const std::string& fullPath) {
    loadNamedTextureResource(fullPath, "Sky01", gSkyTex);
    const char* flatNames[3] = {"Flat01", "Flat04", "Flat31"};
    gFloorTexCount = 0;
    for (int i = 0; i < 3; ++i) if (loadNamedTextureResource(fullPath, flatNames[i], gFloorTex[gFloorTexCount])) ++gFloorTexCount;

    const char* wallNames[] = {"Wall01", "Wall20", "Wall31", "Wall37", "Wall42", "Wall45", "Door01", "Term01"};
    gWallTexCount = 0;
    for (size_t i = 0; i < sizeof(wallNames) / sizeof(wallNames[0]) && gWallTexCount < 8; ++i) {
        if (loadNamedTextureResource(fullPath, wallNames[i], gWallTex[gWallTexCount])) ++gWallTexCount;
    }
    // Fallback: use any available non-sky/non-flat texture if exact wall names were not enough.
    for (size_t i = 0; i < gTextureEntries.size() && gWallTexCount < 4; ++i) {
        const std::string& n = gTextureEntries[i].name;
        if (nameStarts(n, "Sky") || nameStarts(n, "Flat")) continue;
        TextureBitmap tb;
        if (readTextureBitmapByEntry(fullPath, gTextureEntries[i], tb)) {
            gWallTex[gWallTexCount++] = tb;
        }
    }
    LOGI("scene textures v64 column-height-anchor sky=%s %ux%u floors=%d walls=%d", gSkyTex.ok ? "OK" : "MISS", gSkyTex.width, gSkyTex.height, gFloorTexCount, gWallTexCount);
}

static void loadCurrentTexture() {
    std::lock_guard<std::mutex> lock(gTextureMutex);
    if (!gFirstGldProbeOk || gFirstGldName.empty() || gTextureEntries.empty()) return;
    if (gTextureIndex < 0) gTextureIndex = 0;
    if (gTextureIndex >= (int)gTextureEntries.size()) gTextureIndex = (int)gTextureEntries.size() - 1;
    const std::string fullPath = gDataPath + "/" + gFirstGldName;
    const bool ok = readTextureByIndex(fullPath, gTextureIndex);
    LOGI("texture select idx=%d/%d name=%s off=%u len=%u tex=%ux%u raw=%s rawchk=%08x min=%u max=%u",
         gTextureIndex, (int)gTextureEntries.size(), gGldEntry0Name.c_str(), gGldEntry0Offset, gGldEntry0Length, gTex0Width, gTex0Height,
         ok && gTex0RawOk ? "OK" : "MISS", gTex0RawChecksum, gTex0RawMin, gTex0RawMax);
}

static void parseTextureGldDirectory(const std::string& fullPath) {
    if (gFirstGldHeadLen < 8 || memcmp(gFirstGldHead, "TGLD", 4) != 0) return;
    const unsigned int dirOffset = be32(gFirstGldHead + 4);
    if (dirOffset == 0 || (long)dirOffset >= gFirstGldSize) return;
    FILE* f = fopen(fullPath.c_str(), "rb"); if (!f) return;
    if (fseek(f, (long)dirOffset, SEEK_SET) != 0) { fclose(f); return; }
    unsigned char hdr[4]; if (fread(hdr, 1, 4, f) != 4) { fclose(f); return; }
    const unsigned int count = be32(hdr); if (count == 0 || count > 4096) { fclose(f); return; }
    gGldDirOffset = dirOffset; gGldDirCount = count; gGldDirProbeOk = true;
    gTextureEntries.clear(); gTextureEntries.reserve(count);
    for (unsigned int i = 0; i < count; ++i) {
        unsigned char entry[16]; if (fread(entry, 1, 16, f) != 16) break;
        char name[9]; memcpy(name, entry, 8); name[8] = 0;
        TextureEntry te; te.name = name; te.offset = be32(entry + 8); te.length = be32(entry + 12);
        te.width = te.height = te.anim = te.hshift = te.frame = te.zero = 0; te.headerOk = false;
        if (te.offset + 16u <= (unsigned int)gFirstGldSize && te.length >= 16u) {
            const long savePos = ftell(f);
            if (fseek(f, (long)te.offset, SEEK_SET) == 0) {
                unsigned char th[16];
                if (fread(th, 1, 16, f) == 16) {
                    te.width = be16(th + 0); te.anim = be16(th + 2); te.height = be16(th + 4); te.hshift = be16(th + 6); te.frame = be32(th + 8); te.zero = be32(th + 12);
                    te.headerOk = (te.width > 0 && te.width <= 512 && te.height > 0 && te.height <= 512);
                }
            }
            fseek(f, savePos, SEEK_SET);
        }
        gTextureEntries.push_back(te);
    }
    fclose(f);
    gTextureIndex = 0; loadCurrentTexture(); loadSceneTextures(fullPath);
    LOGI("TGLD dir ok file=%s dir=%u count=%u loaded=%d firstEntry=%s off=%u len=%u tex=%ux%u anim=%u hshift=%u frame=%u zero=%u raw=%s rawchk=%08x min=%u max=%u",
         gFirstGldName.c_str(), gGldDirOffset, gGldDirCount, (int)gTextureEntries.size(), gGldEntry0Name.c_str(), gGldEntry0Offset, gGldEntry0Length,
         gTex0Width, gTex0Height, gTex0Anim, gTex0HShift, gTex0Frame, gTex0Zero, gTex0RawOk ? "OK" : "MISS", gTex0RawChecksum, gTex0RawMin, gTex0RawMax);
}

static void probeFirstGld(const std::vector<std::string>& files) {
    if (files.empty()) return;
    gFirstGldName = files[0];
    for (size_t i = 0; i < files.size(); ++i) if (strcasecmp(files[i].c_str(), "BLES0001.GLD") == 0) { gFirstGldName = files[i]; break; }
    const std::string fullPath = gDataPath + "/" + gFirstGldName;
    FILE* f = fopen(fullPath.c_str(), "rb"); if (!f) return;
    unsigned char buf[4096]; unsigned int hash = 2166136261u; long total = 0; gFirstGldHeadLen = 0;
    for (;;) {
        const size_t got = fread(buf, 1, sizeof(buf), f);
        if (got > 0) {
            if (gFirstGldHeadLen < 16) { int copy = (int)std::min(got, (size_t)(16 - gFirstGldHeadLen)); memcpy(gFirstGldHead + gFirstGldHeadLen, buf, (size_t)copy); gFirstGldHeadLen += copy; }
            hash = fnv1aUpdate(hash, buf, got); total += (long)got;
        }
        if (got < sizeof(buf)) { if (ferror(f)) { fclose(f); return; } break; }
    }
    fclose(f);
    gFirstGldSize = total; gFirstGldChecksum = hash; gFirstGldProbeOk = (total > 0);
    char headHex[64]; headHex[0] = 0;
    for (int i = 0; i < gFirstGldHeadLen; ++i) { char tmp[4]; snprintf(tmp, sizeof(tmp), "%02X", gFirstGldHead[i]); strncat(headHex, tmp, sizeof(headHex) - strlen(headHex) - 1); }
    LOGI("GLD probe ok name=%s size=%ld fnv1a=%08x head=%s", gFirstGldName.c_str(), gFirstGldSize, gFirstGldChecksum, headHex);
    parseTextureGldDirectory(fullPath);
}



static bool slzUnpack(const unsigned char* src, size_t srcLen, unsigned int type, size_t expectedSize, std::vector<unsigned char>& out) {
    out.clear();
    out.reserve(expectedSize);
    const unsigned int mask = (type == 0) ? 15u : 31u;
    const unsigned int lsl = (type == 0) ? 4u : 3u;
    size_t p = 0;
    int guard = 0;
    while (p < srcLen && out.size() <= expectedSize && guard++ < 2000000) {
        unsigned int tag = src[p++];
        if (tag == 0) {
            for (int i = 0; i < 8 && p < srcLen && out.size() < expectedSize; ++i) out.push_back(src[p++]);
            continue;
        }
        for (int bit = 0; bit < 8 && out.size() <= expectedSize; ++bit) {
            bool compressed = (tag & 0x80u) != 0;
            tag = (tag << 1u) & 0xffu;
            if (!compressed) {
                if (p >= srcLen) return false;
                out.push_back(src[p++]);
            } else {
                if (p >= srcLen) return false;
                unsigned int cs = src[p++];
                if (cs == 0) return out.size() == expectedSize;
                unsigned int num = (cs & mask) + 2u;
                unsigned int off = (cs << lsl) & 0xffffu;
                if (p >= srcLen) return false;
                off = (off & 0xff00u) | src[p++];
                if (off == 0 || off > out.size()) return false;
                size_t from = out.size() - off;
                for (unsigned int k = 0; k < num && out.size() < expectedSize; ++k) {
                    out.push_back(out[from + k]);
                }
            }
        }
    }
    return out.size() == expectedSize;
}

static std::string safeIdFromBytes(const unsigned char* p, size_t len) {
    if (!p || len < 4) return "????";
    char id[5];
    for (int i = 0; i < 4; ++i) {
        unsigned char c = p[i];
        id[i] = (c >= 32 && c < 127) ? (char)c : '?';
    }
    id[4] = 0;
    return std::string(id);
}

static std::string safeFixedName(const unsigned char* p, size_t len) {
    std::string out;
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = p[i];
        if (c >= 32 && c < 127) out.push_back((char)c);
        else out.push_back('?');
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

static bool parseLgldLevelInfo(AssetInfo& ai, const std::vector<unsigned char>& unpacked) {
    if (unpacked.size() < 15 || safeIdFromBytes(&unpacked[0], unpacked.size()) != "LGLD") return false;

    ai.lgldLevelCount = be16(&unpacked[4]);
    ai.lgldDirOffset = be32(&unpacked[6]);
    ai.compressionType = unpacked[10];
    ai.lgldLength = be32(&unpacked[11]);
    ai.lgldMapLength = ai.lgldLength;
    const unsigned int mapBytes = 128u * 128u * 2u;
    ai.lgldPayloadBytes = (ai.lgldLength > mapBytes) ? (ai.lgldLength - mapBytes) : 0;

    size_t p = 15;
    if (p + 4 > unpacked.size()) return false;
    ai.lgldBlocks = be32(&unpacked[p]);
    p += 4;
    if (ai.lgldBlocks > 8192 || p + (size_t)ai.lgldBlocks * 32u > unpacked.size()) return false;
    ai.lgldBlockData.clear();
    ai.lgldBlockData.reserve(ai.lgldBlocks);
    for (unsigned int i = 0; i < ai.lgldBlocks; ++i) {
        LgldBlockInfo bi;
        bi.floorHeight = beS16(&unpacked[p + 0]);
        bi.ceilHeight = beS16(&unpacked[p + 2]);
        bi.floorTex = beS16(&unpacked[p + 4]);
        bi.ceilTex = beS16(&unpacked[p + 6]);
        bi.illumination = (int)(int8_t)unpacked[p + 10];
        bi.fog = (unpacked[p + 11] & 0x80u) != 0u;
        bi.edge[0] = beS32(&unpacked[p + 12]);
        bi.edge[1] = beS32(&unpacked[p + 16]);
        bi.edge[2] = beS32(&unpacked[p + 20]);
        bi.edge[3] = beS32(&unpacked[p + 24]);
        bi.effect = unpacked[p + 28];
        bi.trigger2 = unpacked[p + 29];
        bi.attributes = unpacked[p + 30];
        bi.trigger = unpacked[p + 31];
        ai.lgldBlockData.push_back(bi);
        p += 32;
    }

    if (p + 4 > unpacked.size()) return false;
    ai.lgldEdges = be32(&unpacked[p]);
    p += 4;
    if (ai.lgldEdges > 16384 || p + (size_t)ai.lgldEdges * 16u > unpacked.size()) return false;
    ai.lgldEdgeData.clear();
    ai.lgldEdgeData.reserve(ai.lgldEdges);
    for (unsigned int i = 0; i < ai.lgldEdges; ++i) {
        LgldEdgeInfo ei;
        ei.normTex = beS32(&unpacked[p + 0]);
        ei.upTex = beS32(&unpacked[p + 4]);
        ei.lowTex = beS32(&unpacked[p + 8]);
        ei.attribute = be16(&unpacked[p + 12]);
        ai.lgldEdgeData.push_back(ei);
        p += 16;
    }

    const size_t effectsStart = p;
    if (p + 4 > unpacked.size()) return false;
    ai.lgldEffectData.clear();
    int el = beS32(&unpacked[p]);
    p += 4;
    int guard = 0;
    while (el >= 0 && guard++ < 10000) {
        ai.lgldEffectLists++;
        std::vector<LgldEffectCommand> list;
        while (el != 0 && guard++ < 10000) {
            ai.lgldEffectEntries++;
            if (p + 4 + 2 + 4 > unpacked.size()) return false;
            LgldEffectCommand command;
            command.trigger = ((unsigned int)el >> 16) & 0xffffu;
            command.type = (unsigned int)el & 0xffffu;
            command.param1 = beS16(&unpacked[p]);
            command.param2 = beS16(&unpacked[p + 2]);
            command.key = unpacked[p + 4];
            list.push_back(command);
            p += 4;
            p += 2;
            el = beS32(&unpacked[p]);
            p += 4;
        }
        ai.lgldEffectData.push_back(list);
        if (p + 4 > unpacked.size()) return false;
        el = beS32(&unpacked[p]);
        p += 4;
    }
    if (guard >= 10000) return false;
    ai.lgldEffectsBytes = (unsigned int)(p - effectsStart);

    if (p + mapBytes > unpacked.size()) return false;
    ai.lgldMapOffset = (unsigned int)p;
    ai.lgldMapCells.assign(128u * 128u, 0);
    bool haveCell = false;
    ai.lgldMinX = ai.lgldMinY = 9999;
    ai.lgldMaxX = ai.lgldMaxY = -1;
    for (int y = 0; y < 128; ++y) {
        for (int x = 0; x < 128; ++x) {
            const short v = beS16(&unpacked[p + ((size_t)y * 128u + (size_t)x) * 2u]);
            ai.lgldMapCells[(size_t)y * 128u + (size_t)x] = v;
            if (v != 0) {
                ai.lgldUsedCells++;
                if (v < 0) ai.lgldSolidCells++;
                unsigned int ab = (unsigned int)(v < 0 ? -v : v);
                if (ab > ai.lgldMaxBlockAbs) ai.lgldMaxBlockAbs = ab;
                if (x < ai.lgldMinX) ai.lgldMinX = x;
                if (y < ai.lgldMinY) ai.lgldMinY = y;
                if (x > ai.lgldMaxX) ai.lgldMaxX = x;
                if (y > ai.lgldMaxY) ai.lgldMaxY = y;
                haveCell = true;
            }
        }
    }
    if (!haveCell) ai.lgldMinX = ai.lgldMinY = ai.lgldMaxX = ai.lgldMaxY = 0;
    p += mapBytes;

    const unsigned int expectedMapEnd = 15u + ai.lgldLength;
    if (expectedMapEnd != (unsigned int)p) {
        // Do not fail: keep parsing names from the actual stream position, but log via fields.
    }

    if (p + 4 <= unpacked.size()) {
        ai.lgldTextureNames = be32(&unpacked[p]);
        p += 4;
        if (ai.lgldTextureNames <= 4096 && p + (size_t)ai.lgldTextureNames * 8u <= unpacked.size()) {
            ai.lgldTextureList.clear();
            ai.lgldTextureList.reserve(ai.lgldTextureNames);
            for (unsigned int i = 0; i < ai.lgldTextureNames; ++i) {
                std::string tn = safeFixedName(&unpacked[p + (size_t)i * 8u], 8);
                if (i == 0) ai.lgldFirstTexture = tn;
                ai.lgldTextureList.push_back(tn);
            }
            p += (size_t)ai.lgldTextureNames * 8u;
        }
    }
    if (p + 4 <= unpacked.size()) {
        ai.lgldObjectNames = be32(&unpacked[p]);
        p += 4;
        if (ai.lgldObjectNames <= 4096 && p + (size_t)ai.lgldObjectNames * 4u <= unpacked.size()) {
            ai.lgldObjectList.clear();
            ai.lgldObjectList.reserve(ai.lgldObjectNames);
            for (unsigned int i = 0; i < ai.lgldObjectNames; ++i) {
                std::string on = safeFixedName(&unpacked[p + (size_t)i * 4u], 4);
                if (i == 0) ai.lgldFirstObject = on;
                ai.lgldObjectList.push_back(on);
            }
            p += (size_t)ai.lgldObjectNames * 4u;
        }
    }
    if (p + 4 <= unpacked.size()) {
        ai.lgldObjects = be32(&unpacked[p]);
        p += 4;
        const size_t objSize = 10;
        if (ai.lgldObjects <= 4096 && p + (size_t)ai.lgldObjects * objSize <= unpacked.size()) {
            ai.lgldPlacedObjects.clear();
            ai.lgldPlacedObjects.reserve(ai.lgldObjects);
            for (unsigned int i = 0; i < ai.lgldObjects; ++i) {
                LgldPlacedObject object;
                object.objectCode = be16(&unpacked[p]);
                object.worldX = be16(&unpacked[p + 2]);
                object.worldY = be16(&unpacked[p + 4]);
                object.heading = be16(&unpacked[p + 6]);
                object.flags = unpacked[p + 8];
                object.activationTrigger = unpacked[p + 9];
                if (object.objectCode == 0) object.name = "PLAYER";
                else if (object.objectCode <= ai.lgldObjectList.size()) object.name = ai.lgldObjectList[object.objectCode - 1u];
                else object.name = "?";
                ai.lgldPlacedObjects.push_back(object);
                p += objSize;
            }
        }
    }
    if (p + 4 <= unpacked.size()) {
        ai.lgldSoundNames = be32(&unpacked[p]);
        p += 4;
        if (ai.lgldSoundNames <= 4096 && p + (size_t)ai.lgldSoundNames * 4u <= unpacked.size()) {
            ai.lgldSoundList.clear();
            ai.lgldSoundList.reserve(ai.lgldSoundNames);
            for (unsigned int i = 0; i < ai.lgldSoundNames; ++i)
                ai.lgldSoundList.push_back(safeFixedName(&unpacked[p + (size_t)i * 4u], 4));
            p += (size_t)ai.lgldSoundNames * 4u;
        }
    }
    if (p + 4 <= unpacked.size()) {
        ai.lgldLoadPic = safeFixedName(&unpacked[p], 4);
    }

    ai.lgldParseOk = true;
    return true;
}


static bool readGldDirectoryHeader(const std::string& fullPath, unsigned int dirOffset, long fileSize, unsigned int& count, std::string& firstEntry) {
    count = 0;
    firstEntry.clear();
    if (dirOffset == 0 || dirOffset + 4u >= (unsigned int)fileSize) return false;
    FILE* df = fopen(fullPath.c_str(), "rb");
    if (!df) return false;
    bool ok = false;
    if (fseek(df, (long)dirOffset, SEEK_SET) == 0) {
        unsigned char cbuf[16];
        size_t got = fread(cbuf, 1, sizeof(cbuf), df);
        if (got >= 4) {
            count = be32(cbuf);
            ok = (count > 0 && count < 4096);
            if (ok && got >= 8) {
                char n[5];
                memcpy(n, cbuf + 4, 4);
                n[4] = 0;
                firstEntry = n;
            }
        }
    }
    fclose(df);
    return ok;
}

static AssetInfo probeAssetFile(const std::string& name) {
    AssetInfo ai;
    ai.name = name;
    const std::string fullPath = gDataPath + "/" + name;
    FILE* f = fopen(fullPath.c_str(), "rb");
    if (!f) { ai.guess = "OPEN_FAIL"; return ai; }

    unsigned char buf[4096];
    unsigned char head[32];
    int headLen = 0;
    unsigned int hash = 2166136261u;
    long total = 0;
    for (;;) {
        size_t got = fread(buf, 1, sizeof(buf), f);
        if (got > 0) {
            if (headLen < (int)sizeof(head)) {
                int copy = (int)std::min(got, (size_t)(sizeof(head) - (size_t)headLen));
                memcpy(head + headLen, buf, (size_t)copy);
                headLen += copy;
            }
            hash = fnv1aUpdate(hash, buf, got);
            total += (long)got;
        }
        if (got < sizeof(buf)) break;
    }
    fclose(f);

    ai.size = total;
    ai.checksum = hash;
    char tmpHex[96]; tmpHex[0] = 0;
    for (int i = 0; i < headLen && i < 16; ++i) {
        char t[4]; snprintf(t, sizeof(t), "%02X", head[i]);
        strncat(tmpHex, t, sizeof(tmpHex) - strlen(tmpHex) - 1);
    }
    ai.headHex = tmpHex;
    if (headLen >= 4) ai.id.assign((const char*)head, (const char*)head + 4);
    else ai.id = "????";

    if (headLen >= 8) ai.word0 = be32(head + 4);
    if (headLen >= 12) ai.word1 = be32(head + 8);

    if (ai.id == "TGLD") {
        ai.dirOffset = ai.word0;
        ai.guess = "TEXTURES";
        if (ai.dirOffset > 0 && ai.dirOffset + 4u < (unsigned int)ai.size) {
            FILE* df = fopen(fullPath.c_str(), "rb");
            if (df && fseek(df, (long)ai.dirOffset, SEEK_SET) == 0) {
                unsigned char cbuf[4];
                if (fread(cbuf, 1, 4, df) == 4) ai.count = be32(cbuf);
            }
            if (df) fclose(df);
        }
    } else if (ai.id == "GGLD") {
        ai.dirOffset = ai.word0;
        ai.count = (headLen >= 10) ? be16(head + 8) : 0;
        ai.guess = "GFX/PAL";
    } else if (ai.id == "OGLD") {
        ai.dirOffset = ai.word0;
        ai.guess = "OBJECTS";
        readGldDirectoryHeader(fullPath, ai.dirOffset, ai.size, ai.count, ai.firstEntry);
    } else if (ai.id == "SGLD") {
        ai.dirOffset = ai.word0;
        ai.guess = "SOUNDS";
        readGldDirectoryHeader(fullPath, ai.dirOffset, ai.size, ai.count, ai.firstEntry);
    } else if (ai.id == "MGLD") {
        ai.guess = "MAIN INDEX";
    } else if (ai.id == "VDCO") {
        ai.unpackedSize = ai.word0;
        ai.packedSize = ai.word1;
        if (headLen >= 13) ai.compressionType = head[12];
        ai.guess = "LEVEL/VDCO";

        if (ai.packedSize > 0 && ai.packedSize + 13u <= (unsigned int)ai.size && ai.unpackedSize > 0 && ai.unpackedSize < 1024u * 1024u) {
            FILE* vf = fopen(fullPath.c_str(), "rb");
            if (vf && fseek(vf, 13, SEEK_SET) == 0) {
                std::vector<unsigned char> packed(ai.packedSize);
                if (fread(&packed[0], 1, packed.size(), vf) == packed.size()) {
                    std::vector<unsigned char> unpacked;
                    if (slzUnpack(&packed[0], packed.size(), ai.compressionType, ai.unpackedSize, unpacked)) {
                        ai.unpackedHash = fnv1aUpdate(2166136261u, &unpacked[0], unpacked.size());
                        ai.unpackedId = safeIdFromBytes(&unpacked[0], unpacked.size());
                        if (ai.unpackedId == "LGLD" && unpacked.size() >= 15) {
                            ai.guess = parseLgldLevelInfo(ai, unpacked) ? "LEVEL/LGLD" : "LEVEL/LGLD?";
                        } else {
                            ai.guess = "VDCO/UNPACK";
                        }
                    } else {
                        ai.guess = "VDCO/BADPACK";
                    }
                }
            }
            if (vf) fclose(vf);
        }
    } else if (ai.id == "DGLD") {
        ai.dirOffset = ai.word0;
        ai.guess = "DATA?";
    } else {
        ai.guess = "UNKNOWN";
        // Heuristic: if bytes 4..7 point near end and contain sane count, mark as container-like.
        if (ai.word0 > 0 && ai.word0 + 4u < (unsigned int)ai.size) {
            unsigned int c = 0;
            std::string first;
            if (readGldDirectoryHeader(fullPath, ai.word0, ai.size, c, first)) {
                ai.count = c;
                ai.dirOffset = ai.word0;
                ai.firstEntry = first;
                ai.guess = "CONTAINER";
            }
        }
    }
    return ai;
}


static std::string v63HistString(const std::map<int, int>& hist, int maxEntries) {
    std::string out;
    int shown = 0;
    for (std::map<int, int>::const_iterator it = hist.begin(); it != hist.end(); ++it) {
        if (shown >= maxEntries) {
            char more[32];
            snprintf(more, sizeof(more), ",...");
            out += more;
            break;
        }
        char tmp[64];
        snprintf(tmp, sizeof(tmp), "%s%d:%d", out.empty() ? "" : ",", it->first, it->second);
        out += tmp;
        ++shown;
    }
    if (out.empty()) out = "-";
    return out;
}

static std::string v63TexNameForAsset(const AssetInfo& ai, int tex) {
    int idx = tex < 0 ? -tex : tex;
    if (idx > 0 && idx <= (int)ai.lgldTextureList.size()) return ai.lgldTextureList[(size_t)idx - 1u];
    return std::string("-");
}

static const LgldBlockInfo* v63BlockByIndex(const AssetInfo& ai, int blockIndex) {
    if (blockIndex <= 0 || blockIndex >= (int)ai.lgldBlockData.size()) return nullptr;
    return &ai.lgldBlockData[(size_t)blockIndex];
}

static int v63AbsBlock(short raw) {
    int v = (int)raw;
    return v < 0 ? -v : v;
}

static const LgldBlockInfo* v63BlockForCell(const AssetInfo& ai, int x, int y, short* outRaw, int* outBlock) {
    if (outRaw) *outRaw = 0;
    if (outBlock) *outBlock = 0;
    if (x < 0 || y < 0 || x >= 128 || y >= 128 || ai.lgldMapCells.size() != 128u * 128u) return nullptr;
    short raw = ai.lgldMapCells[(size_t)y * 128u + (size_t)x];
    int bi = v63AbsBlock(raw);
    if (outRaw) *outRaw = raw;
    if (outBlock) *outBlock = bi;
    return v63BlockByIndex(ai, bi);
}

static const char* v63DirName(int dir) {
    switch (dir) {
        case 0: return "E";
        case 1: return "S";
        default: return "?";
    }
}

static void v63AppendExample(std::string& dst, const char* tag, int x, int y, int nx, int ny, int blockA, int blockB, int dFloor, unsigned int attrB) {
    if (dst.size() > 420) return;
    char tmp[128];
    snprintf(tmp, sizeof(tmp), "%s%s %d,%d>%d,%d b%d>%d dF=%d aB=%02x",
             dst.empty() ? "" : " | ", tag, x, y, nx, ny, blockA, blockB, dFloor, attrB);
    dst += tmp;
}

static void runV61MapScannerReport() {
    const std::string csvPath = gDataPath + "/Breathless_v64_map_height_report.csv";
    FILE* csv = fopen(csvPath.c_str(), "wb");
    if (csv) {
        fprintf(csv, "kind,asset,x,y,dir,nx,ny,rawA,blockA,fhA,chA,gapA,attrA,fxA,trA,tr2A,rawB,blockB,fhB,chB,gapB,attrB,fxB,trB,tr2B,dFloor,dCeil,edgeA,edgeB,note,texA,texB\n");
    }

    int levelCount = 0;
    int totalDamageCells = 0;
    int totalDeepDrops = 0;
    int totalHighRises = 0;
    int totalFxCells = 0;
    int totalTriggerCells = 0;

    LOGI("mapscan v64 start csv=%s", csvPath.c_str());

    for (size_t assetIndex = 0; assetIndex < gAssetInfos.size(); ++assetIndex) {
        const AssetInfo& ai = gAssetInfos[assetIndex];
        if (!ai.lgldParseOk || ai.lgldMapCells.size() != 128u * 128u) continue;
        ++levelCount;

        std::map<int, int> floorHist;
        std::map<int, int> ceilHist;
        std::map<int, int> gapHist;
        std::map<int, int> attrDamageHist;
        std::map<int, int> effectHist;
        std::map<int, int> triggerHist;
        std::map<int, int> riseHist;
        std::map<int, int> dropHist;
        std::map<int, int> ceilDeltaHist;

        int openCells = 0;
        int solidCells = 0;
        int damageCells = 0;
        int fxCells = 0;
        int triggerCells = 0;
        int transitionEdges = 0;
        int highRiseEdges = 0;
        int deepDropEdges = 0;
        int lavaDropEdges = 0;
        int walkableStepEdges = 0;
        int solidEdges = 0;
        int lowestFloor = 999999;
        int highestFloor = -999999;
        int lowestDamageFloor = 999999;
        int highestDamageFloor = -999999;
        std::string examples;
        std::string damageExamples;

        for (int y = 0; y < 128; ++y) {
            for (int x = 0; x < 128; ++x) {
                short raw = 0;
                int blockIndex = 0;
                const LgldBlockInfo* b = v63BlockForCell(ai, x, y, &raw, &blockIndex);
                if (raw == 0 || !b) continue;

                if (raw > 0) ++openCells;
                else ++solidCells;
                const int gap = b->ceilHeight - b->floorHeight;
                floorHist[b->floorHeight]++;
                ceilHist[b->ceilHeight]++;
                gapHist[gap]++;
                if (b->floorHeight < lowestFloor) lowestFloor = b->floorHeight;
                if (b->floorHeight > highestFloor) highestFloor = b->floorHeight;

                if ((b->attributes & 3u) != 0u) {
                    ++damageCells;
                    totalDamageCells++;
                    attrDamageHist[(int)(b->attributes & 3u)]++;
                    if (b->floorHeight < lowestDamageFloor) lowestDamageFloor = b->floorHeight;
                    if (b->floorHeight > highestDamageFloor) highestDamageFloor = b->floorHeight;
                    if (damageExamples.size() < 360) {
                        char tmp[160];
                        std::string tn = v63TexNameForAsset(ai, b->floorTex);
                        snprintf(tmp, sizeof(tmp), "%s%d,%d b%d fh=%d attr=%02x ft=%d/%s",
                                 damageExamples.empty() ? "" : " | ", x, y, blockIndex, b->floorHeight,
                                 b->attributes, b->floorTex, tn.c_str());
                        damageExamples += tmp;
                    }
                    if (csv) {
                        std::string texA = v63TexNameForAsset(ai, b->floorTex);
                        fprintf(csv, "damage,%s,%d,%d,,,%d,%d,%d,%d,%d,%02x,%u,%u,%u,,,,,,,,,,,,,,attr%u,%s,\n",
                                ai.name.c_str(), x, y, raw, blockIndex, b->floorHeight, b->ceilHeight, gap,
                                b->attributes, b->effect, b->trigger, b->trigger2, (unsigned int)(b->attributes & 3u), texA.c_str());
                    }
                }
                if (b->effect != 0u) {
                    ++fxCells;
                    ++totalFxCells;
                    effectHist[(int)b->effect]++;
                }
                if (b->trigger != 0u || b->trigger2 != 0u) {
                    ++triggerCells;
                    ++totalTriggerCells;
                    if (b->trigger != 0u) triggerHist[(int)b->trigger]++;
                    if (b->trigger2 != 0u) triggerHist[(int)b->trigger2]++;
                }
            }
        }

        static const int dx[2] = {1, 0};
        static const int dy[2] = {0, 1};
        for (int y = 0; y < 128; ++y) {
            for (int x = 0; x < 128; ++x) {
                short rawA = 0;
                int blockA = 0;
                const LgldBlockInfo* a = v63BlockForCell(ai, x, y, &rawA, &blockA);
                if (rawA == 0 || !a) continue;
                for (int dir = 0; dir < 2; ++dir) {
                    const int nx = x + dx[dir];
                    const int ny = y + dy[dir];
                    short rawB = 0;
                    int blockB = 0;
                    const LgldBlockInfo* b = v63BlockForCell(ai, nx, ny, &rawB, &blockB);
                    if (rawB == 0 || !b) continue;

                    const int dFloor = b->floorHeight - a->floorHeight;
                    const int dCeil = b->ceilHeight - a->ceilHeight;
                    const int gapA = a->ceilHeight - a->floorHeight;
                    const int gapB = b->ceilHeight - b->floorHeight;
                    const int edgeA = (dir == 0) ? a->edge[0] : a->edge[1];
                    const int edgeB = (dir == 0) ? b->edge[2] : b->edge[3];
                    const bool interesting = (dFloor != 0) || (dCeil != 0) || ((a->attributes & 3u) != 0u) || ((b->attributes & 3u) != 0u) || a->effect || b->effect;
                    if (!interesting) continue;

                    ++transitionEdges;
                    if (rawA < 0 || rawB < 0) ++solidEdges;
                    if (dFloor > 0) {
                        riseHist[dFloor]++;
                        if (dFloor > ORIG_PLAYER_MAX_RISE) {
                            ++highRiseEdges;
                            ++totalHighRises;
                            v63AppendExample(examples, "rise", x, y, nx, ny, blockA, blockB, dFloor, b->attributes);
                        } else {
                            ++walkableStepEdges;
                        }
                    } else if (dFloor < 0) {
                        dropHist[-dFloor]++;
                        if (-dFloor > ORIG_PLAYER_MAX_DROP) {
                            ++deepDropEdges;
                            ++totalDeepDrops;
                            v63AppendExample(examples, "drop", x, y, nx, ny, blockA, blockB, dFloor, b->attributes);
                        }
                    }
                    if (dCeil != 0) ceilDeltaHist[dCeil]++;
                    if ((b->attributes & 3u) != 0u && dFloor < 0) {
                        ++lavaDropEdges;
                        v63AppendExample(examples, "lavaDrop", x, y, nx, ny, blockA, blockB, dFloor, b->attributes);
                    }

                    if (csv) {
                        const char* note = "";
                        if ((b->attributes & 3u) != 0u && dFloor < 0) note = "drop_to_damage";
                        else if (dFloor < -ORIG_PLAYER_MAX_DROP) note = "deep_drop";
                        else if (dFloor > ORIG_PLAYER_MAX_RISE) note = "high_rise";
                        else if (dFloor != 0) note = "walkable_step_or_slope";
                        else if (dCeil != 0) note = "ceiling_change";
                        else if ((a->attributes & 3u) || (b->attributes & 3u)) note = "damage_edge";
                        else if (a->effect || b->effect) note = "effect_edge";
                        std::string texA = v63TexNameForAsset(ai, a->floorTex);
                        std::string texB = v63TexNameForAsset(ai, b->floorTex);
                        fprintf(csv, "transition,%s,%d,%d,%s,%d,%d,%d,%d,%d,%d,%d,%02x,%u,%u,%u,%d,%d,%d,%d,%d,%02x,%u,%u,%u,%d,%d,%d,%d,%s,%s,%s\n",
                                ai.name.c_str(), x, y, v63DirName(dir), nx, ny,
                                rawA, blockA, a->floorHeight, a->ceilHeight, gapA, a->attributes, a->effect, a->trigger, a->trigger2,
                                rawB, blockB, b->floorHeight, b->ceilHeight, gapB, b->attributes, b->effect, b->trigger, b->trigger2,
                                dFloor, dCeil, edgeA, edgeB, note, texA.c_str(), texB.c_str());
                    }
                }
            }
        }

        const std::string floors = v63HistString(floorHist, 12);
        const std::string ceils = v63HistString(ceilHist, 12);
        const std::string gaps = v63HistString(gapHist, 12);
        const std::string rises = v63HistString(riseHist, 10);
        const std::string drops = v63HistString(dropHist, 10);
        const std::string effects = v63HistString(effectHist, 10);
        const std::string triggers = v63HistString(triggerHist, 10);
        const int d1 = attrDamageHist.count(1) ? attrDamageHist[1] : 0;
        const int d2 = attrDamageHist.count(2) ? attrDamageHist[2] : 0;
        const int d3 = attrDamageHist.count(3) ? attrDamageHist[3] : 0;

        LOGI("mapscan v64 level=%s open=%d solid=%d floorRange=%d..%d floors=%s ceils=%s gaps=%s",
             ai.name.c_str(), openCells, solidCells, lowestFloor == 999999 ? 0 : lowestFloor,
             highestFloor == -999999 ? 0 : highestFloor, floors.c_str(), ceils.c_str(), gaps.c_str());
        LOGI("mapscan v64 level=%s damage=%d attr1=%d attr2=%d attr3=%d dmgFloor=%d..%d fxCells=%d fx=%s trigCells=%d trig=%s",
             ai.name.c_str(), damageCells, d1, d2, d3,
             lowestDamageFloor == 999999 ? 0 : lowestDamageFloor,
             highestDamageFloor == -999999 ? 0 : highestDamageFloor,
             fxCells, effects.c_str(), triggerCells, triggers.c_str());
        LOGI("mapscan v64 level=%s trans=%d solidEdges=%d steps<=%d=%d highRise>%d=%d deepDrop>%d=%d lavaDrops=%d rises=%s drops=%s examples=%s",
             ai.name.c_str(), transitionEdges, solidEdges, ORIG_PLAYER_MAX_RISE, walkableStepEdges,
             ORIG_PLAYER_MAX_RISE, highRiseEdges, ORIG_PLAYER_MAX_DROP, deepDropEdges, lavaDropEdges,
             rises.c_str(), drops.c_str(), examples.empty() ? "-" : examples.c_str());
        if (!damageExamples.empty()) {
            LOGI("mapscan v64 damage examples %s %s", ai.name.c_str(), damageExamples.c_str());
        }
    }

    if (csv) fclose(csv);
    LOGI("mapscan v64 done levels=%d damageCells=%d highRiseEdges=%d deepDropEdges=%d fxCells=%d triggerCells=%d csv=%s",
         levelCount, totalDamageCells, totalHighRises, totalDeepDrops, totalFxCells, totalTriggerCells, csvPath.c_str());
}


static void scanAssetFiles(const std::vector<std::string>& gldNames) {
    gAssetInfos.clear();
    gAssetInfos.reserve(gldNames.size());
    for (size_t i = 0; i < gldNames.size(); ++i) {
        AssetInfo ai = probeAssetFile(gldNames[i]);
        gAssetInfos.push_back(ai);
        LOGI("asset v64 column-height-anchor idx=%d name=%s size=%ld id=%s guess=%s dir=%u count=%u first=%s pack=%u unpack=%u ctype=%u uid=%s ulen=%u payload=%u blocks=%u edges=%u maps=%u solid=%u bounds=%d,%d-%d,%d tex=%u objn=%u objs=%u snd=%u load=%s uchk=%08x chk=%08x head=%s",
             (int)i, ai.name.c_str(), ai.size, ai.id.c_str(), ai.guess.c_str(),
             ai.dirOffset, ai.count, ai.firstEntry.c_str(), ai.packedSize, ai.unpackedSize, ai.compressionType, ai.unpackedId.c_str(),
             ai.lgldLength, ai.lgldPayloadBytes, ai.lgldBlocks, ai.lgldEdges, ai.lgldUsedCells, ai.lgldSolidCells,
             ai.lgldMinX, ai.lgldMinY, ai.lgldMaxX, ai.lgldMaxY, ai.lgldTextureNames, ai.lgldObjectNames, ai.lgldObjects, ai.lgldSoundNames, ai.lgldLoadPic.c_str(),
             ai.unpackedHash, ai.checksum, ai.headHex.c_str());
    }
    int firstLevel = -1;
    for (size_t i = 0; i < gAssetInfos.size(); ++i) {
        if (gAssetInfos[i].lgldParseOk && gAssetInfos[i].lgldMapCells.size() == 128u * 128u) { firstLevel = (int)i; break; }
    }
    if (firstLevel >= 0) gAssetIndex = firstLevel;
    if (gAssetIndex < 0) gAssetIndex = 0;
    if (gAssetIndex >= (int)gAssetInfos.size()) gAssetIndex = (int)gAssetInfos.size() - 1;
    if (gAssetIndex < 0) gAssetIndex = 0;
    runV61MapScannerReport();
}

static void loadGlobalObjectDefinitions() {
    gGlobalObjectInfo.clear();
    for (size_t aiIndex = 0; aiIndex < gAssetInfos.size(); ++aiIndex) {
        const AssetInfo& asset = gAssetInfos[aiIndex];
        if (asset.id != "OGLD" || asset.dirOffset == 0u) continue;
        const std::string path = gDataPath + "/" + asset.name;
        FILE* file = fopen(path.c_str(), "rb");
        if (!file || fseek(file, (long)asset.dirOffset, SEEK_SET) != 0) { if (file) fclose(file); continue; }
        unsigned char countBytes[4];
        if (fread(countBytes, 1, 4, file) != 4) { fclose(file); continue; }
        const unsigned int count = be32(countBytes);
        for (unsigned int i = 0; i < count && i < 4096u; ++i) {
            unsigned char entry[12];
            if (fread(entry, 1, sizeof(entry), file) != sizeof(entry)) break;
            const std::string name = safeFixedName(entry, 4);
            const unsigned int offset = be32(entry + 4);
            const unsigned int length = be32(entry + 8);
            const long directoryPosition = ftell(file);
            if (length >= 44u && offset + length <= (unsigned int)asset.size && fseek(file, (long)offset, SEEK_SET) == 0) {
                std::vector<unsigned char> objectBytes(length);
                if (fread(&objectBytes[0], 1, objectBytes.size(), file) == objectBytes.size()) {
                    const unsigned char* header = &objectBytes[0];
                    GlobalObjectInfo info;
                    info.numFrames = (int)be16(header);
                    info.radius = (int)be16(header + 2);
                    info.height = (int)be16(header + 4);
                    info.animationType = (int)(int8_t)header[6];
                    info.objectType = header[7];
                    for (int parameter = 0; parameter < 4; ++parameter)
                        info.param[parameter] = beS16(header + 8 + parameter * 2);
                    for (int parameter = 4; parameter < 12; ++parameter)
                        info.param[parameter] = (int)(int8_t)header[16 + parameter - 4];
                    info.sound[0] = safeFixedName(header + 24, 4);
                    info.sound[1] = safeFixedName(header + 28, 4);
                    info.sound[2] = safeFixedName(header + 32, 4);
                    const unsigned int frameOffset = be32(header + 36);
                    if (frameOffset + 8u <= objectBytes.size()) {
                        const unsigned char* frame = &objectBytes[frameOffset];
                        info.spriteWidth = (int)be16(frame);
                        info.spriteHeight = (int)be16(frame + 2);
                        info.spriteXOffset = beS16(frame + 4);
                        info.spriteYOffset = beS16(frame + 6);
                        if (info.spriteWidth > 0 && info.spriteWidth <= 512 && info.spriteHeight > 0 && info.spriteHeight <= 512 &&
                            frameOffset + 8u + (size_t)info.spriteWidth * 4u <= objectBytes.size()) {
                            info.spritePixels.assign((size_t)info.spriteWidth * (size_t)info.spriteHeight, 0);
                            info.spriteMask.assign(info.spritePixels.size(), 0);
                            for (int column = 0; column < info.spriteWidth; ++column) {
                                const unsigned int columnOffset = be32(frame + 8u + (size_t)column * 4u);
                                size_t cp = (size_t)frameOffset + (size_t)columnOffset;
                                while (cp + 2u <= objectBytes.size()) {
                                    const int startY = (int)(int8_t)objectBytes[cp++];
                                    if (startY < 0) break;
                                    const unsigned int run = objectBytes[cp++];
                                    if (cp + run > objectBytes.size()) break;
                                    for (unsigned int pixel = 0; pixel < run; ++pixel) {
                                        const int sy = startY + (int)pixel;
                                        if (sy < 0 || sy >= info.spriteHeight) continue;
                                        const size_t destination = (size_t)sy * (size_t)info.spriteWidth + (size_t)column;
                                        info.spritePixels[destination] = objectBytes[cp + pixel];
                                        info.spriteMask[destination] = 1;
                                    }
                                    cp += run;
                                }
                            }
                        }
                    }
                    const int frameCount = std::max(0, std::min(256, info.numFrames));
                    for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
                        const size_t listPosition = 44u + (size_t)frameIndex * 4u;
                        if (listPosition + 4u > objectBytes.size()) break;
                        const unsigned int offsetInObject = be32(header + listPosition);
                        if (offsetInObject == 0u || offsetInObject + 8u > objectBytes.size()) break;
                        info.frameOffsets.push_back(offsetInObject);
                        const unsigned char* frame = &objectBytes[offsetInObject];
                        ObjectSpriteFrame decoded;
                        decoded.width = (int)be16(frame);
                        decoded.height = (int)be16(frame + 2);
                        decoded.xOffset = beS16(frame + 4);
                        decoded.yOffset = beS16(frame + 6);
                        if (decoded.width <= 0 || decoded.width > 512 || decoded.height <= 0 || decoded.height > 512 ||
                            offsetInObject + 8u + (size_t)decoded.width * 4u > objectBytes.size()) continue;
                        decoded.pixels.assign((size_t)decoded.width * (size_t)decoded.height, 0);
                        decoded.mask.assign(decoded.pixels.size(), 0);
                        for (int column = 0; column < decoded.width; ++column) {
                            const unsigned int columnOffset = be32(frame + 8u + (size_t)column * 4u);
                            size_t cp = (size_t)offsetInObject + (size_t)columnOffset;
                            while (cp + 2u <= objectBytes.size()) {
                                const int startY = (int)(int8_t)objectBytes[cp++];
                                if (startY < 0) break;
                                const unsigned int run = objectBytes[cp++];
                                if (cp + run > objectBytes.size()) break;
                                for (unsigned int pixel = 0; pixel < run; ++pixel) {
                                    const int sy = startY + (int)pixel;
                                    if (sy < 0 || sy >= decoded.height) continue;
                                    const size_t destination = (size_t)sy * (size_t)decoded.width + (size_t)column;
                                    decoded.pixels[destination] = objectBytes[cp + pixel];
                                    decoded.mask[destination] = 1;
                                }
                                cp += run;
                            }
                        }
                        info.frames.push_back(decoded);
                    }
                    gGlobalObjectInfo[name] = info;
                }
            }
            fseek(file, directoryPosition, SEEK_SET);
        }
        fclose(file);
        LOGI("global object definitions loaded file=%s count=%u", asset.name.c_str(), (unsigned int)gGlobalObjectInfo.size());
        break;
    }
}

static void loadSoundResources() {
    std::lock_guard<std::mutex> lock(gAudioMutex);
    gSoundResources.clear();
    gSoundVoices.clear();
    for (size_t aiIndex = 0; aiIndex < gAssetInfos.size(); ++aiIndex) {
        const AssetInfo& asset = gAssetInfos[aiIndex];
        if (asset.id != "SGLD" || asset.dirOffset == 0u) continue;
        const std::string path = gDataPath + "/" + asset.name;
        FILE* file = fopen(path.c_str(), "rb");
        if (!file || fseek(file, (long)asset.dirOffset, SEEK_SET) != 0) { if (file) fclose(file); continue; }
        unsigned char countBytes[4];
        if (fread(countBytes, 1, 4, file) != 4) { fclose(file); continue; }
        const unsigned int count = be32(countBytes);
        for (unsigned int i = 0; i < count && i < 1024u; ++i) {
            unsigned char entry[12];
            if (fread(entry, 1, 12, file) != 12) break;
            const long directoryPosition = ftell(file);
            const std::string name = safeFixedName(entry, 4);
            const unsigned int offset = be32(entry + 4), length = be32(entry + 8);
            if (length >= 16u && offset + length <= (unsigned int)asset.size && fseek(file, (long)offset, SEEK_SET) == 0) {
                std::vector<unsigned char> bytes(length);
                if (fread(&bytes[0], 1, bytes.size(), file) == bytes.size()) {
                    SoundResource sound;
                    sound.name = name;
                    bool hasLink = false;
                    for (int linkByte = 0; linkByte < 4; ++linkByte) if (bytes[(size_t)linkByte] != 0u) hasLink = true;
                    if (hasLink) sound.linkedName = safeFixedName(&bytes[0], 4);
                    // TMap.i declares snd_length as "Lunghezza in word" and
                    // Paula's ac_len register also counts 16-bit words.  The
                    // previous decoder treated that value as bytes and cut
                    // every original effect sample in half.
                    const int sampleLengthWords = (int)be16(&bytes[4]);
                    const size_t sampleLengthBytes = (size_t)sampleLengthWords * 2u;
                    const int period = (int)be16(&bytes[6]);
                    sound.volume = std::min(64, (int)be16(&bytes[8]));
                    sound.loop = (int)be16(&bytes[10]);
                    sound.type = (int)(int8_t)bytes[14];
                    sound.code = bytes[15];
                    if (period > 0) {
                        // TSP1 (global code 3) intentionally uses Paula period
                        // 1712, about 2071 Hz. The former generic 4 kHz floor
                        // played it at nearly double speed and made its tail
                        // sound truncated. Preserve that original rate without
                        // changing the safety floor used by unrelated assets.
                        const int minimumRate = sound.type == 1 && sound.code == 3 ? 1000 : 4000;
                        sound.sampleRate = std::max(minimumRate, std::min(48000, 3546895 / period));
                    }
                    if (sound.linkedName.empty() && sound.type != 0 && sampleLengthWords > 0 &&
                        16u + sampleLengthBytes <= bytes.size())
                        sound.pcm.assign((const signed char*)&bytes[16],
                                         (const signed char*)&bytes[16 + sampleLengthBytes]);
                    gSoundResources[name] = sound;
                }
            }
            fseek(file, directoryPosition, SEEK_SET);
        }
        fclose(file);
        LOGI("original sound resources loaded file=%s count=%u", asset.name.c_str(), (unsigned int)gSoundResources.size());
        break;
    }
}

static const SoundResource* resolvedSoundResource(const SoundResource* settings) {
    const SoundResource* sample = settings;
    for (int links = 0; links < 4 && sample && sample->pcm.empty() && !sample->linkedName.empty(); ++links) {
        std::map<std::string, SoundResource>::const_iterator linked = gSoundResources.find(sample->linkedName);
        if (linked == gSoundResources.end()) break;
        sample = &linked->second;
    }
    return sample;
}

static double soundResourceDuration(const std::string& requestedName) {
    std::lock_guard<std::mutex> lock(gAudioMutex);
    std::map<std::string, SoundResource>::const_iterator found = gSoundResources.find(requestedName);
    if (found == gSoundResources.end()) return 0.0;
    const SoundResource* sample = resolvedSoundResource(&found->second);
    return sample && !sample->pcm.empty() && sample->sampleRate > 0
        ? (double)sample->pcm.size() / (double)sample->sampleRate : 0.0;
}

static void stopSoundGroup(int exclusiveGroup) {
    if (exclusiveGroup == 0) return;
    std::lock_guard<std::mutex> lock(gAudioMutex);
    for (size_t i = 0; i < gSoundVoices.size();) {
        if (gSoundVoices[i].exclusiveGroup == exclusiveGroup)
            gSoundVoices.erase(gSoundVoices.begin() + (long)i);
        else ++i;
    }
}

static void stopLoopingSoundGroup(int exclusiveGroup) {
    if (exclusiveGroup == 0) return;
    std::lock_guard<std::mutex> lock(gAudioMutex);
    for (size_t i = 0; i < gSoundVoices.size();) {
        if (gSoundVoices[i].exclusiveGroup == exclusiveGroup && gSoundVoices[i].allowLoop)
            gSoundVoices.erase(gSoundVoices.begin() + (long)i);
        else ++i;
    }
}

static void playSoundResource(const std::string& requestedName, float pan = 0.0f,
                              int exclusiveGroup = 0, bool allowLoop = false,
                              bool protectedVoice = false) {
    if (!gSoundEnabled.load() || requestedName.empty()) return;
    std::lock_guard<std::mutex> lock(gAudioMutex);
    std::map<std::string, SoundResource>::const_iterator found = gSoundResources.find(requestedName);
    if (found == gSoundResources.end()) return;
    const SoundResource* settings = &found->second;
    const SoundResource* sample = resolvedSoundResource(settings);
    if (!sample || sample->pcm.empty()) return;
    if (exclusiveGroup != 0) {
        for (size_t i = 0; i < gSoundVoices.size(); ++i) {
            const SoundVoice& active = gSoundVoices[i];
            if (active.exclusiveGroup == exclusiveGroup && active.resource &&
                active.position < active.resource->pcm.size()) return;
        }
    }
    SoundVoice voice;
    voice.resource = sample;
    const float volume = (float)settings->volume / 64.0f;
    pan = std::max(-1.0f, std::min(1.0f, pan));
    voice.left = volume * (pan > 0.0f ? 1.0f - pan : 1.0f);
    voice.right = volume * (pan < 0.0f ? 1.0f + pan : 1.0f);
    voice.exclusiveGroup = exclusiveGroup;
    voice.allowLoop = allowLoop;
    voice.protectedVoice = protectedVoice;
    if (gSoundVoices.size() >= 12u) {
        size_t victim = gSoundVoices.size();
        for (size_t i = 0; i < gSoundVoices.size(); ++i) {
            if (!gSoundVoices[i].protectedVoice) { victim = i; break; }
        }
        if (victim < gSoundVoices.size()) gSoundVoices.erase(gSoundVoices.begin() + (long)victim);
        else if (!protectedVoice) return;
        else gSoundVoices.erase(gSoundVoices.begin());
    }
    gSoundVoices.push_back(voice);
}

static void playGlobalSoundCode(int code, float pan = 0.0f) {
    for (std::map<std::string, SoundResource>::const_iterator it = gSoundResources.begin();
         it != gSoundResources.end(); ++it) {
        if (it->second.type == 1 && it->second.code == code) {
            playSoundResource(it->first, pan);
            return;
        }
    }
}

static bool isDoorEffectType(unsigned int type) {
    return type == 5u || type == 6u || type == 7u || type == 8u || type == 12u;
}

static void playDoorSoundCode(int code, unsigned int trigger) {
    const int group = 1000 + (int)(trigger & 0x7fffu);
    stopSoundGroup(group);
    for (std::map<std::string, SoundResource>::const_iterator it = gSoundResources.begin();
         it != gSoundResources.end(); ++it) {
        if (it->second.type == 1 && it->second.code == code) {
            playSoundResource(it->first, 0.0f, group);
            return;
        }
    }
}

static double globalSoundDuration(int code) {
    for (std::map<std::string, SoundResource>::const_iterator it = gSoundResources.begin();
         it != gSoundResources.end(); ++it)
        if (it->second.type == 1 && it->second.code == code) return soundResourceDuration(it->first);
    return 0.0;
}

static double playTeleportSound() {
    stopSoundGroup(TELEPORT_SOUND_GROUP);
    for (std::map<std::string, SoundResource>::const_iterator it = gSoundResources.begin();
         it != gSoundResources.end(); ++it) {
        if (it->second.type != 1 || it->second.code != 3) continue;
        // The teleport effect is transition-critical: normal shots and enemy
        // effects must neither evict it nor share/restart its mixer voice.
        playSoundResource(it->first, 0.0f, TELEPORT_SOUND_GROUP, false, true);
        return globalSoundDuration(3);
    }
    return 0.0;
}

static std::string progressSavePath() {
    return gDataPath.empty() ? std::string() : gDataPath + "/breathless_save_v1.dat";
}

static void appendSaveU32(std::vector<unsigned char>& bytes, unsigned int value) {
    bytes.push_back((unsigned char)(value & 0xffu));
    bytes.push_back((unsigned char)((value >> 8) & 0xffu));
    bytes.push_back((unsigned char)((value >> 16) & 0xffu));
    bytes.push_back((unsigned char)((value >> 24) & 0xffu));
}

static unsigned int readSaveU32(const std::vector<unsigned char>& bytes, size_t& offset, bool& ok) {
    if (offset + 4u > bytes.size()) { ok = false; return 0u; }
    const unsigned int value = (unsigned int)bytes[offset] |
        ((unsigned int)bytes[offset + 1u] << 8) |
        ((unsigned int)bytes[offset + 2u] << 16) |
        ((unsigned int)bytes[offset + 3u] << 24);
    offset += 4u;
    return value;
}

static void appendSaveF32(std::vector<unsigned char>& bytes, float value) {
    unsigned int bits = 0u;
    memcpy(&bits, &value, sizeof(bits));
    appendSaveU32(bytes, bits);
}

static float readSaveF32(const std::vector<unsigned char>& bytes, size_t& offset, bool& ok) {
    const unsigned int bits = readSaveU32(bytes, offset, ok);
    float value = 0.0f;
    memcpy(&value, &bits, sizeof(value));
    if (!std::isfinite(value)) ok = false;
    return value;
}

static void appendSaveName(std::vector<unsigned char>& bytes, const std::string& name, size_t width) {
    for (size_t i = 0; i < width; ++i) bytes.push_back(i < name.size() ? (unsigned char)name[i] : 0u);
}

static std::string readSaveName(const std::vector<unsigned char>& bytes, size_t& offset,
                                size_t width, bool& ok) {
    if (offset + width > bytes.size()) { ok = false; return std::string(); }
    size_t length = 0;
    while (length < width && bytes[offset + length] != 0u) ++length;
    const std::string value((const char*)&bytes[offset], length);
    offset += width;
    return value;
}

static std::string globalObjectName(const GlobalObjectInfo* definition) {
    if (!definition) return std::string();
    for (std::map<std::string, GlobalObjectInfo>::const_iterator it = gGlobalObjectInfo.begin();
         it != gGlobalObjectInfo.end(); ++it)
        if (&it->second == definition) return it->first;
    return std::string();
}

static int firstPlayableLevelIndex() {
    for (size_t i = 0; i < gAssetInfos.size(); ++i)
        if (gAssetInfos[i].lgldParseOk && gAssetInfos[i].lgldMapCells.size() == 128u * 128u)
            return (int)i;
    return 0;
}

static int playableLevelOrdinal(int assetIndex, int* totalOut = nullptr) {
    int ordinal = 0, total = 0;
    for (size_t i = 0; i < gAssetInfos.size(); ++i) {
        if (!gAssetInfos[i].lgldParseOk || gAssetInfos[i].lgldMapCells.size() != 128u * 128u) continue;
        ++total;
        if ((int)i == assetIndex) ordinal = total;
    }
    if (totalOut) *totalOut = total;
    return ordinal;
}

static bool saveGameProgress() {
    std::lock_guard<std::mutex> saveLock(gProgressSaveMutex);
    if (!gPlayerProgressValid || gPlayerDead || gDataPath.empty() ||
        gAssetIndex < 0 || gAssetIndex >= (int)gAssetInfos.size()) return false;
    const AssetInfo& level = gAssetInfos[(size_t)gAssetIndex];
    if (!level.lgldParseOk) return false;

    const bool snapshotValid = gRuntimeAssetIndex == gAssetIndex &&
        gRuntimeBlocks.size() == level.lgldBlockData.size() &&
        !gLevelExitActive && !gTeleportActive;

    std::vector<unsigned char> bytes;
    bytes.reserve(256u + (snapshotValid ? gRuntimeBlocks.size() * 12u + gRuntimeObjects.size() * 92u : 0u));
    static const unsigned char magic[8] = {'B','L','S','A','V','E','0','2'};
    bytes.insert(bytes.end(), magic, magic + sizeof(magic));
    appendSaveU32(bytes, 2u);
    appendSaveU32(bytes, snapshotValid ? 1u : 0u);
    appendSaveName(bytes, level.name, 16u);
    appendSaveU32(bytes, (unsigned int)std::max(1, std::min(100, gPlayerHealth)));
    appendSaveU32(bytes, (unsigned int)std::max(0, std::min(100, gPlayerShields)));
    appendSaveU32(bytes, (unsigned int)std::max(0, std::min(9999, gPlayerEnergy)));
    appendSaveU32(bytes, (unsigned int)std::max(0, std::min(99999, gPlayerCredits)));
    appendSaveU32(bytes, (unsigned int)std::max(0, gPlayerScore));
    appendSaveU32(bytes, (unsigned int)std::max(0, std::min(PLAYER_WEAPON_COUNT - 1, gPlayerWeapon)));
    appendSaveU32(bytes, (unsigned int)std::max(1, std::min(3, gPlayerRetries)));
    for (int i = 0; i < PLAYER_WEAPON_COUNT; ++i) bytes.push_back((unsigned char)std::min(2, (int)gPlayerWeapons[i]));
    for (int i = 0; i < 4; ++i) bytes.push_back(gPlayerKeys[i] ? 1u : 0u);

    appendSaveU32(bytes, (unsigned int)std::max(1, std::min(100, gCheckpointHealth)));
    appendSaveU32(bytes, (unsigned int)std::max(0, std::min(100, gCheckpointShields)));
    appendSaveU32(bytes, (unsigned int)std::max(0, std::min(9999, gCheckpointEnergy)));
    appendSaveU32(bytes, (unsigned int)std::max(0, std::min(99999, gCheckpointCredits)));
    appendSaveU32(bytes, (unsigned int)std::max(0, gCheckpointScore));
    appendSaveU32(bytes, (unsigned int)std::max(0, std::min(PLAYER_WEAPON_COUNT - 1, gCheckpointWeapon)));
    for (int i = 0; i < PLAYER_WEAPON_COUNT; ++i) bytes.push_back((unsigned char)std::min(2, (int)gCheckpointWeapons[i]));
    for (int i = 0; i < 4; ++i) bytes.push_back(gCheckpointKeys[i] ? 1u : 0u);

    if (snapshotValid) {
        appendSaveF32(bytes, gPlayerX);
        appendSaveF32(bytes, gPlayerY);
        appendSaveF32(bytes, gPlayerA);
        appendSaveF32(bytes, gPlayerBaseZF);
        appendSaveF32(bytes, gPlayerVerticalSpeed);
        appendSaveF32(bytes, gPlayerBobPhase);
        appendSaveF32(bytes, gPlayerBobOffset);
        appendSaveU32(bytes, (unsigned int)gPlayerBaseZ);
        appendSaveU32(bytes, (unsigned int)gPlayerTargetBaseZ);
        appendSaveU32(bytes, (unsigned int)gPlayerCeilZ);
        appendSaveU32(bytes, (unsigned int)gPlayerEyeZ);
        appendSaveU32(bytes, (unsigned int)gPlayerLastCellX);
        appendSaveU32(bytes, (unsigned int)gPlayerLastCellY);
        appendSaveU32(bytes, (unsigned int)gPlayerLastBlockIndex);
        appendSaveU32(bytes, (unsigned int)gPlayerFallStartZ);
        appendSaveU32(bytes, (gPlayerFalling ? 1u : 0u) | (gPlayerStartChecked ? 2u : 0u));

        appendSaveU32(bytes, (unsigned int)gRuntimeBlocks.size());
        for (size_t i = 0; i < gRuntimeBlocks.size(); ++i) {
            appendSaveU32(bytes, (unsigned int)gRuntimeBlocks[i].floorHeight);
            appendSaveU32(bytes, (unsigned int)gRuntimeBlocks[i].ceilHeight);
            appendSaveU32(bytes, (unsigned int)gRuntimeBlocks[i].illumination);
        }

        appendSaveU32(bytes, (unsigned int)gRuntimeObjects.size());
        for (size_t i = 0; i < gRuntimeObjects.size(); ++i) {
            const RuntimeObject& object = gRuntimeObjects[i];
            appendSaveU32(bytes, (unsigned int)object.placedIndex);
            appendSaveF32(bytes, object.x); appendSaveF32(bytes, object.y);
            appendSaveF32(bytes, object.heading); appendSaveF32(bytes, object.bobPhase);
            appendSaveF32(bytes, object.thinkClock); appendSaveF32(bytes, object.attackClock);
            appendSaveF32(bytes, object.stateClock); appendSaveF32(bytes, object.contactClock);
            appendSaveF32(bytes, object.alertClock);
            appendSaveF32(bytes, object.lastSeenX); appendSaveF32(bytes, object.lastSeenY);
            appendSaveU32(bytes, (unsigned int)object.aiState);
            appendSaveU32(bytes, (unsigned int)object.behaviorCounter);
            appendSaveU32(bytes, (unsigned int)object.turnDirection);
            appendSaveU32(bytes, (unsigned int)object.collisionAttempts);
            appendSaveU32(bytes, (unsigned int)object.health);
            appendSaveU32(bytes, (unsigned int)object.animationFrame);
            appendSaveF32(bytes, object.deathClock);
            appendSaveU32(bytes, (unsigned int)object.deathYOffset);
            appendSaveU32(bytes, (object.dying ? 1u : 0u) | (object.exploding ? 2u : 0u) |
                          (object.corpse ? 4u : 0u) | (object.collected ? 8u : 0u) |
                          (object.dead ? 16u : 0u));
            appendSaveName(bytes, globalObjectName(object.deathDefinition), 8u);
        }

        appendSaveU32(bytes, (unsigned int)gActiveEffects.size());
        for (size_t i = 0; i < gActiveEffects.size(); ++i) {
            const ActiveLevelEffect& effect = gActiveEffects[i];
            appendSaveU32(bytes, effect.command.trigger);
            appendSaveU32(bytes, effect.command.type);
            appendSaveU32(bytes, (unsigned int)effect.command.param1);
            appendSaveU32(bytes, (unsigned int)effect.command.param2);
            appendSaveU32(bytes, effect.command.key);
            appendSaveU32(bytes, effect.listIndex);
            appendSaveU32(bytes, (unsigned int)effect.phase);
            appendSaveF32(bytes, effect.remaining);
            appendSaveF32(bytes, effect.fractional);
            appendSaveU32(bytes, (unsigned int)effect.appliedLightDelta);
            appendSaveU32(bytes, effect.finished ? 1u : 0u);
        }

        const std::set<unsigned int>* sets[] = {
            &gPermanentEffectLists, &gActiveEnemyTriggers, &gActivatedSwitchParts
        };
        for (size_t setIndex = 0; setIndex < sizeof(sets) / sizeof(sets[0]); ++setIndex) {
            appendSaveU32(bytes, (unsigned int)sets[setIndex]->size());
            for (std::set<unsigned int>::const_iterator it = sets[setIndex]->begin();
                 it != sets[setIndex]->end(); ++it) appendSaveU32(bytes, *it);
        }
    }
    appendSaveU32(bytes, fnv1aUpdate(2166136261u, &bytes[0], bytes.size()));

    const std::string path = progressSavePath();
    const std::string temporaryPath = path + ".tmp";
    FILE* file = fopen(temporaryPath.c_str(), "wb");
    if (!file) { LOGE("could not create save file"); return false; }
    const bool written = fwrite(&bytes[0], 1, bytes.size(), file) == bytes.size() && fflush(file) == 0;
    const bool closed = fclose(file) == 0;
    if (!written || !closed || rename(temporaryPath.c_str(), path.c_str()) != 0) {
        remove(temporaryPath.c_str());
        LOGE("could not commit save file");
        return false;
    }
    gSaveDirty = false;
    gLastProgressSaveTime = nowSeconds();
    LOGI("progress saved level=%s health=%d armor=%d credits=%d snapshot=%s objects=%u",
         level.name.c_str(), gPlayerHealth, gPlayerShields, gPlayerCredits,
         snapshotValid ? "yes" : "no", snapshotValid ? (unsigned int)gRuntimeObjects.size() : 0u);
    return true;
}

static bool loadGameProgress() {
    std::lock_guard<std::mutex> saveLock(gProgressSaveMutex);
    const std::string path = progressSavePath();
    FILE* file = path.empty() ? nullptr : fopen(path.c_str(), "rb");
    if (!file) return false;
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return false; }
    const long length = ftell(file);
    if (length < 70 || length > 4 * 1024 * 1024 || fseek(file, 0, SEEK_SET) != 0) { fclose(file); return false; }
    std::vector<unsigned char> bytes((size_t)length);
    const bool read = fread(&bytes[0], 1, bytes.size(), file) == bytes.size();
    fclose(file);
    if (!read) return false;
    static const unsigned char magicV1[8] = {'B','L','S','A','V','E','0','1'};
    static const unsigned char magicV2[8] = {'B','L','S','A','V','E','0','2'};
    const bool legacyV1 = memcmp(&bytes[0], magicV1, sizeof(magicV1)) == 0;
    const bool currentV2 = memcmp(&bytes[0], magicV2, sizeof(magicV2)) == 0;
    if (!legacyV1 && !currentV2) return false;
    size_t checksumOffset = bytes.size() - 4u;
    bool ok = true;
    const unsigned int storedChecksum = readSaveU32(bytes, checksumOffset, ok);
    if (!ok || storedChecksum != fnv1aUpdate(2166136261u, &bytes[0], bytes.size() - 4u)) return false;

    size_t offset = 8u;
    const unsigned int version = readSaveU32(bytes, offset, ok);
    if (!ok || (legacyV1 ? version != 1u : version != 2u)) return false;
    const bool snapshotValid = currentV2 && (readSaveU32(bytes, offset, ok) & 1u) != 0u;
    const std::string levelName = readSaveName(bytes, offset, 16u, ok);
    const int health = (int)readSaveU32(bytes, offset, ok);
    const int shields = (int)readSaveU32(bytes, offset, ok);
    const int energy = (int)readSaveU32(bytes, offset, ok);
    const int credits = (int)readSaveU32(bytes, offset, ok);
    const int score = (int)readSaveU32(bytes, offset, ok);
    const int weapon = (int)readSaveU32(bytes, offset, ok);
    const int retries = (int)readSaveU32(bytes, offset, ok);
    if (!ok ||
        health < 1 || health > 100 || shields < 0 || shields > 100 ||
        energy < 0 || energy > 9999 || credits < 0 || credits > 99999 ||
        score < 0 || weapon < 0 || weapon >= PLAYER_WEAPON_COUNT || retries < 1 || retries > 3) return false;
    unsigned char weapons[PLAYER_WEAPON_COUNT];
    bool keys[4];
    if (offset + PLAYER_WEAPON_COUNT + 4u > bytes.size()) return false;
    for (int i = 0; i < PLAYER_WEAPON_COUNT; ++i) {
        weapons[i] = bytes[offset++];
        if (weapons[i] > 2u) return false;
    }
    for (int i = 0; i < 4; ++i) {
        if (bytes[offset] > 1u) return false;
        keys[i] = bytes[offset++] != 0u;
    }
    if (weapons[0] == 0u || weapons[weapon] == 0u) return false;

    int checkpointHealth = health, checkpointShields = shields, checkpointEnergy = energy;
    int checkpointCredits = credits, checkpointScore = score, checkpointWeapon = weapon;
    unsigned char checkpointWeapons[PLAYER_WEAPON_COUNT];
    bool checkpointKeys[4];
    memcpy(checkpointWeapons, weapons, sizeof(checkpointWeapons));
    memcpy(checkpointKeys, keys, sizeof(checkpointKeys));
    if (currentV2) {
        checkpointHealth = (int)readSaveU32(bytes, offset, ok);
        checkpointShields = (int)readSaveU32(bytes, offset, ok);
        checkpointEnergy = (int)readSaveU32(bytes, offset, ok);
        checkpointCredits = (int)readSaveU32(bytes, offset, ok);
        checkpointScore = (int)readSaveU32(bytes, offset, ok);
        checkpointWeapon = (int)readSaveU32(bytes, offset, ok);
        if (!ok || checkpointHealth < 1 || checkpointHealth > 100 || checkpointShields < 0 || checkpointShields > 100 ||
            checkpointEnergy < 0 || checkpointEnergy > 9999 || checkpointCredits < 0 || checkpointCredits > 99999 ||
            checkpointScore < 0 || checkpointWeapon < 0 || checkpointWeapon >= PLAYER_WEAPON_COUNT ||
            offset + PLAYER_WEAPON_COUNT + 4u > bytes.size()) return false;
        for (int i = 0; i < PLAYER_WEAPON_COUNT; ++i) {
            checkpointWeapons[i] = bytes[offset++];
            if (checkpointWeapons[i] > 2u) return false;
        }
        for (int i = 0; i < 4; ++i) {
            if (bytes[offset] > 1u) return false;
            checkpointKeys[i] = bytes[offset++] != 0u;
        }
        if (checkpointWeapons[0] == 0u || checkpointWeapons[checkpointWeapon] == 0u) return false;
    }

    int levelIndex = -1;
    for (size_t i = 0; i < gAssetInfos.size(); ++i)
        if (gAssetInfos[i].name == levelName && gAssetInfos[i].lgldParseOk) { levelIndex = (int)i; break; }
    if (levelIndex < 0) return false;

    float playerX = 1.5f, playerY = 1.5f, playerA = 0.0f, playerBaseZF = 0.0f;
    float playerVerticalSpeed = 0.0f, playerBobPhase = 0.0f, playerBobOffset = 0.0f;
    int playerBaseZ = 0, playerTargetBaseZ = 0, playerCeilZ = 128, playerEyeZ = ORIG_PLAYER_EYES_HEIGHT;
    int playerLastCellX = -9999, playerLastCellY = -9999, playerLastBlockIndex = -9999, playerFallStartZ = 0;
    unsigned int playerFlags = 0u;
    std::vector<LgldBlockInfo> savedBlocks;
    std::vector<RuntimeObject> savedObjects;
    std::vector<ActiveLevelEffect> savedEffects;
    std::set<unsigned int> savedPermanentEffects, savedEnemyTriggers, savedSwitchParts;

    if (snapshotValid) {
        playerX = readSaveF32(bytes, offset, ok); playerY = readSaveF32(bytes, offset, ok);
        playerA = readSaveF32(bytes, offset, ok); playerBaseZF = readSaveF32(bytes, offset, ok);
        playerVerticalSpeed = readSaveF32(bytes, offset, ok); playerBobPhase = readSaveF32(bytes, offset, ok);
        playerBobOffset = readSaveF32(bytes, offset, ok);
        playerBaseZ = (int)readSaveU32(bytes, offset, ok);
        playerTargetBaseZ = (int)readSaveU32(bytes, offset, ok);
        playerCeilZ = (int)readSaveU32(bytes, offset, ok);
        playerEyeZ = (int)readSaveU32(bytes, offset, ok);
        playerLastCellX = (int)readSaveU32(bytes, offset, ok);
        playerLastCellY = (int)readSaveU32(bytes, offset, ok);
        playerLastBlockIndex = (int)readSaveU32(bytes, offset, ok);
        playerFallStartZ = (int)readSaveU32(bytes, offset, ok);
        playerFlags = readSaveU32(bytes, offset, ok);
        const AssetInfo& savedLevel = gAssetInfos[(size_t)levelIndex];
        const int savedCellX = (int)floorf(playerX), savedCellY = (int)floorf(playerY);
        if (!ok || savedCellX < 0 || savedCellX >= 128 || savedCellY < 0 || savedCellY >= 128 ||
            savedLevel.lgldMapCells[(size_t)savedCellY * 128u + (size_t)savedCellX] <= 0) return false;
        const unsigned int blockCount = readSaveU32(bytes, offset, ok);
        if (!ok || blockCount != savedLevel.lgldBlockData.size() || blockCount > 65536u) return false;
        savedBlocks = savedLevel.lgldBlockData;
        for (size_t i = 0; i < savedBlocks.size(); ++i) {
            savedBlocks[i].floorHeight = (int)readSaveU32(bytes, offset, ok);
            savedBlocks[i].ceilHeight = (int)readSaveU32(bytes, offset, ok);
            savedBlocks[i].illumination = (int)readSaveU32(bytes, offset, ok);
        }
        const unsigned int objectCount = readSaveU32(bytes, offset, ok);
        if (!ok || objectCount > savedLevel.lgldPlacedObjects.size() || objectCount > 65536u) return false;
        savedObjects.reserve(objectCount);
        std::set<unsigned int> placedIndices;
        for (unsigned int i = 0; i < objectCount; ++i) {
            RuntimeObject object;
            object.placedIndex = (size_t)readSaveU32(bytes, offset, ok);
            object.x = readSaveF32(bytes, offset, ok); object.y = readSaveF32(bytes, offset, ok);
            object.heading = readSaveF32(bytes, offset, ok); object.bobPhase = readSaveF32(bytes, offset, ok);
            object.thinkClock = readSaveF32(bytes, offset, ok); object.attackClock = readSaveF32(bytes, offset, ok);
            object.stateClock = readSaveF32(bytes, offset, ok); object.contactClock = readSaveF32(bytes, offset, ok);
            object.alertClock = readSaveF32(bytes, offset, ok);
            object.lastSeenX = readSaveF32(bytes, offset, ok); object.lastSeenY = readSaveF32(bytes, offset, ok);
            object.aiState = (int)readSaveU32(bytes, offset, ok);
            object.behaviorCounter = (int)readSaveU32(bytes, offset, ok);
            object.turnDirection = (int)readSaveU32(bytes, offset, ok);
            object.collisionAttempts = (int)readSaveU32(bytes, offset, ok);
            object.health = (int)readSaveU32(bytes, offset, ok);
            object.animationFrame = (int)readSaveU32(bytes, offset, ok);
            object.deathClock = readSaveF32(bytes, offset, ok);
            object.deathYOffset = (int)readSaveU32(bytes, offset, ok);
            const unsigned int flags = readSaveU32(bytes, offset, ok);
            const std::string deathName = readSaveName(bytes, offset, 8u, ok);
            object.dying = (flags & 1u) != 0u; object.exploding = (flags & 2u) != 0u;
            object.corpse = (flags & 4u) != 0u; object.collected = (flags & 8u) != 0u;
            object.dead = (flags & 16u) != 0u;
            if (!deathName.empty()) {
                std::map<std::string, GlobalObjectInfo>::const_iterator definition = gGlobalObjectInfo.find(deathName);
                if (definition == gGlobalObjectInfo.end()) return false;
                object.deathDefinition = &definition->second;
            }
            if (!ok || object.placedIndex >= savedLevel.lgldPlacedObjects.size() ||
                savedLevel.lgldPlacedObjects[object.placedIndex].objectCode == 0u ||
                !placedIndices.insert((unsigned int)object.placedIndex).second) return false;
            savedObjects.push_back(object);
        }

        const unsigned int effectCount = readSaveU32(bytes, offset, ok);
        if (!ok || effectCount > 65536u) return false;
        savedEffects.reserve(effectCount);
        for (unsigned int i = 0; i < effectCount; ++i) {
            ActiveLevelEffect effect;
            effect.command.trigger = readSaveU32(bytes, offset, ok);
            effect.command.type = readSaveU32(bytes, offset, ok);
            effect.command.param1 = (int)readSaveU32(bytes, offset, ok);
            effect.command.param2 = (int)readSaveU32(bytes, offset, ok);
            effect.command.key = readSaveU32(bytes, offset, ok);
            effect.listIndex = readSaveU32(bytes, offset, ok);
            effect.phase = (int)readSaveU32(bytes, offset, ok);
            effect.remaining = readSaveF32(bytes, offset, ok);
            effect.fractional = readSaveF32(bytes, offset, ok);
            effect.appliedLightDelta = (int)readSaveU32(bytes, offset, ok);
            effect.finished = readSaveU32(bytes, offset, ok) != 0u;
            if (!ok || effect.listIndex == 0u || effect.listIndex > savedLevel.lgldEffectData.size()) return false;
            savedEffects.push_back(effect);
        }

        std::set<unsigned int>* sets[] = {&savedPermanentEffects, &savedEnemyTriggers, &savedSwitchParts};
        for (size_t setIndex = 0; setIndex < sizeof(sets) / sizeof(sets[0]); ++setIndex) {
            const unsigned int count = readSaveU32(bytes, offset, ok);
            if (!ok || count > 65536u) return false;
            for (unsigned int i = 0; i < count; ++i) sets[setIndex]->insert(readSaveU32(bytes, offset, ok));
        }
    }
    if (!ok || offset + 4u != bytes.size()) return false;

    gAssetIndex = levelIndex;
    gPlayerHealth = health;
    gPlayerShields = shields;
    gPlayerEnergy = energy;
    gPlayerCredits = credits;
    gPlayerScore = score;
    gPlayerWeapon = weapon;
    gPlayerRetries = retries;
    memcpy(gPlayerWeapons, weapons, sizeof(gPlayerWeapons));
    memcpy(gPlayerKeys, keys, sizeof(gPlayerKeys));
    gCheckpointHealth = checkpointHealth;
    gCheckpointShields = checkpointShields;
    gCheckpointEnergy = checkpointEnergy;
    gCheckpointCredits = checkpointCredits;
    gCheckpointScore = checkpointScore;
    gCheckpointWeapon = checkpointWeapon;
    memcpy(gCheckpointWeapons, checkpointWeapons, sizeof(gCheckpointWeapons));
    memcpy(gCheckpointKeys, checkpointKeys, sizeof(gCheckpointKeys));
    gPlayerProgressValid = true;
    gRestoreLevelCheckpoint = false;
    if (snapshotValid) {
        gPlayerX = playerX; gPlayerY = playerY; gPlayerA = playerA;
        gPlayerBaseZF = playerBaseZF; gPlayerVerticalSpeed = playerVerticalSpeed;
        gPlayerBobPhase = playerBobPhase; gPlayerBobOffset = playerBobOffset;
        gPlayerBaseZ = playerBaseZ; gPlayerTargetBaseZ = playerTargetBaseZ;
        gPlayerCeilZ = playerCeilZ; gPlayerEyeZ = playerEyeZ;
        gPlayerLastCellX = playerLastCellX; gPlayerLastCellY = playerLastCellY;
        gPlayerLastBlockIndex = playerLastBlockIndex; gPlayerFallStartZ = playerFallStartZ;
        gPlayerFalling = (playerFlags & 1u) != 0u;
        gPlayerStartChecked = true;
        gRuntimeAssetIndex = gAssetIndex;
        gInitialRuntimeBlocks = gAssetInfos[(size_t)gAssetIndex].lgldBlockData;
        gRuntimeBlocks.swap(savedBlocks);
        gRuntimeObjects.swap(savedObjects);
        gActiveEffects.swap(savedEffects);
        gPermanentEffectLists.swap(savedPermanentEffects);
        gActiveEnemyTriggers.swap(savedEnemyTriggers);
        gActivatedSwitchParts.swap(savedSwitchParts);
        gRuntimeProjectiles.clear();
        gRuntimeImpactSparks.clear();
        gObjectLastTime = nowSeconds();
        gMoveLastTime = nowSeconds();
    } else {
        gRuntimeAssetIndex = -99999;
        gPlayerStartChecked = false;
    }
    gPlayerDead = false;
    gLevelExitActive = false;
    gTeleportActive = false;
    gRuntimeTerminalNumber = 0;
    gRuntimeTerminalBackground.clear();
    gSaveDirty = false;
    gLastProgressSaveTime = nowSeconds();
    LOGI("progress loaded level=%s health=%d armor=%d credits=%d snapshot=%s objects=%u",
         levelName.c_str(), gPlayerHealth, gPlayerShields, gPlayerCredits,
         snapshotValid ? "yes" : "no", snapshotValid ? (unsigned int)gRuntimeObjects.size() : 0u);
    return true;
}

static void markGameProgressDirty() {
    if (gPlayerProgressValid && !gPlayerDead) gSaveDirty = true;
}

static void maybeAutosaveGameProgress() {
    if (gSaveDirty && !gPlayerDead && nowSeconds() - gLastProgressSaveTime >= 1.0)
        saveGameProgress();
}

static void resetSavedGame() {
    gGodMode = false;
    gAssetIndex = firstPlayableLevelIndex();
    gPlayerHealth = 100;
    gPlayerShields = 100;
    gPlayerEnergy = 1000;
    gPlayerCredits = 0;
    gPlayerScore = 0;
    gPlayerWeapon = 0;
    gPlayerRetries = 3;
    memset(gPlayerWeapons, 0, sizeof(gPlayerWeapons));
    gPlayerWeapons[0] = 1;
    memset(gPlayerKeys, 0, sizeof(gPlayerKeys));
    gCheckpointHealth = gPlayerHealth;
    gCheckpointShields = gPlayerShields;
    gCheckpointEnergy = gPlayerEnergy;
    gCheckpointCredits = gPlayerCredits;
    gCheckpointScore = gPlayerScore;
    gCheckpointWeapon = gPlayerWeapon;
    memcpy(gCheckpointWeapons, gPlayerWeapons, sizeof(gCheckpointWeapons));
    memcpy(gCheckpointKeys, gPlayerKeys, sizeof(gCheckpointKeys));
    gPlayerProgressValid = true;
    gRestoreLevelCheckpoint = false;
    gRuntimeAssetIndex = -99999;
    gRuntimeBlocks.clear();
    gInitialRuntimeBlocks.clear();
    gRuntimeObjects.clear();
    gRuntimeProjectiles.clear();
    gRuntimeImpactSparks.clear();
    gActiveEffects.clear();
    gPermanentEffectLists.clear();
    gActiveEnemyTriggers.clear();
    gActivatedSwitchParts.clear();
    gPlayerStartChecked = false;
    gPlayerDead = false;
    const std::string path = progressSavePath();
    if (!path.empty()) {
        std::lock_guard<std::mutex> saveLock(gProgressSaveMutex);
        remove(path.c_str());
        remove((path + ".tmp").c_str());
    }
    markGameProgressDirty();
    saveGameProgress();
    gGameResetMessageUntil = nowSeconds() + 2.0;
}

static void startOrContinueGame() {
    if (!gPlayerProgressValid) {
        resetSavedGame();
    }
    gRestoreLevelCheckpoint = false;
    gPlayerStartChecked = gRuntimeAssetIndex == gAssetIndex &&
        gRuntimeBlocks.size() == gAssetInfos[(size_t)gAssetIndex].lgldBlockData.size();
    gMoveLastTime = nowSeconds();
    setFrontendState(FRONTEND_LOADING);
}

static void scanGameData() {
    resetGldProbe();
    DIR* dir = opendir(gDataPath.c_str());
    if (!dir) { gGldFiles = -1; LOGE("data dir missing: %s", gDataPath.c_str()); return; }
    std::vector<std::string> gldNames;
    dirent* e;
    while ((e = readdir(dir)) != 0) {
        const char* n = e->d_name; size_t len = strlen(n);
        if (len > 4 && (strcasecmp(n + len - 4, ".gld") == 0)) gldNames.push_back(n);
    }
    closedir(dir);
    std::sort(gldNames.begin(), gldNames.end());
    gGldFiles = (int)gldNames.size();
    scanAssetFiles(gldNames);
    loadGlobalObjectDefinitions();
    loadSoundResources();
    probeGfxPalette();
    loadPresentationGraphics();
    loadHudPanel();
    probeFirstGld(gldNames);
    gFrontendState = gPresentationGraphics.count("LOG1") != 0u ? FRONTEND_LOGO1 : FRONTEND_TITLE;
    gFrontendStateSince = nowSeconds();
    gFrontendMenuSelection = 0;
    gPlayerProgressValid = false;
    gRestoreLevelCheckpoint = false;
    loadGameProgress();
    LOGI("Breathless Android 0.6.11 dataPath=%s gldFiles=%d first=%s probe=%s save=%s", gDataPath.c_str(), gGldFiles, gFirstGldName.c_str(), gFirstGldProbeOk ? "OK" : "MISS", gPlayerProgressValid ? "loaded" : "new");
}

static void ensureGl() {
    if (gProgram) return;
    const char* vs = "attribute vec2 aPos; attribute vec2 aUv; uniform vec2 uScale; varying vec2 vUv; void main(){ vUv=aUv; gl_Position=vec4(aPos*uScale,0.0,1.0); }";
    const char* fs = "precision mediump float; varying vec2 vUv; uniform sampler2D uTex; uniform vec2 uUvScale; void main(){ vec2 uv=clamp((vUv-vec2(0.5))/uUvScale+vec2(0.5),vec2(0.0),vec2(1.0)); gl_FragColor=texture2D(uTex,uv); }";
    GLuint v = compileShader(GL_VERTEX_SHADER, vs);
    GLuint f = compileShader(GL_FRAGMENT_SHADER, fs);
    gProgram = glCreateProgram(); glAttachShader(gProgram, v); glAttachShader(gProgram, f); glLinkProgram(gProgram); glDeleteShader(v); glDeleteShader(f);
    gPosLoc = glGetAttribLocation(gProgram, "aPos"); gUvLoc = glGetAttribLocation(gProgram, "aUv"); gTexLoc = glGetUniformLocation(gProgram, "uTex"); gScaleLoc = glGetUniformLocation(gProgram, "uScale"); gUvScaleLoc = glGetUniformLocation(gProgram, "uUvScale");
    const GLfloat verts[] = { -1.f,-1.f,0.f,1.f, 1.f,-1.f,1.f,1.f, -1.f,1.f,0.f,0.f, 1.f,1.f,1.f,0.f };
    glGenBuffers(1, &gVbo); glBindBuffer(GL_ARRAY_BUFFER, gVbo); glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glGenTextures(1, &gTexture); glBindTexture(GL_TEXTURE_2D, gTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, FB_W, FB_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, &gFramebuffer[0]);
    gTextureWidth = FB_W;
    gTextureHeight = FB_H;
}


static const AssetInfo* currentLgldAsset() {
    if (gAssetIndex < 0 || gAssetIndex >= (int)gAssetInfos.size()) return nullptr;
    const AssetInfo& ai = gAssetInfos[(size_t)gAssetIndex];
    if (!ai.lgldParseOk || ai.lgldMapCells.size() != 128u * 128u) return nullptr;
    return &ai;
}

static short currentLgldCellRaw(int mx, int my) {
    const AssetInfo* ai = currentLgldAsset();
    if (!ai || mx < 0 || my < 0 || mx >= 128 || my >= 128) return 0;
    return ai->lgldMapCells[(size_t)my * 128u + (size_t)mx];
}

static bool currentLgldOpenCell(int mx, int my) {
    return currentLgldCellRaw(mx, my) > 0;
}

static void ensureRuntimeLevelState() {
    const AssetInfo* ai = currentLgldAsset();
    if (!ai) {
        gRuntimeAssetIndex = -99999;
        gRuntimeBlocks.clear();
        gInitialRuntimeBlocks.clear();
        gActiveEffects.clear();
        gRuntimeProjectiles.clear();
        gRuntimeImpactSparks.clear();
        return;
    }
    if (gRuntimeAssetIndex == gAssetIndex && gRuntimeBlocks.size() == ai->lgldBlockData.size()) return;
    gRuntimeAssetIndex = gAssetIndex;
    gRuntimeBlocks = ai->lgldBlockData;
    gInitialRuntimeBlocks = ai->lgldBlockData;
    gActiveEffects.clear();
    gPermanentEffectLists.clear();
    gActiveEnemyTriggers.clear();
    gActivatedSwitchParts.clear();
    const bool restoredCheckpoint = gRestoreLevelCheckpoint;
    if (!gPlayerProgressValid) {
        gPlayerHealth = 100;
        gPlayerShields = 100;
        gPlayerEnergy = 1000;
        gPlayerCredits = 0;
        gPlayerScore = 0;
        gPlayerWeapon = 0;
        memset(gPlayerWeapons, 0, sizeof(gPlayerWeapons));
        gPlayerWeapons[0] = 1;
        memset(gPlayerKeys, 0, sizeof(gPlayerKeys));
        gPlayerProgressValid = true;
    } else if (gRestoreLevelCheckpoint) {
        gPlayerHealth = gCheckpointHealth;
        gPlayerShields = gCheckpointShields;
        gPlayerEnergy = gCheckpointEnergy;
        gPlayerCredits = gCheckpointCredits;
        gPlayerScore = gCheckpointScore;
        gPlayerWeapon = gCheckpointWeapon;
        memcpy(gPlayerWeapons, gCheckpointWeapons, sizeof(gPlayerWeapons));
        memcpy(gPlayerKeys, gCheckpointKeys, sizeof(gPlayerKeys));
    }
    gRestoreLevelCheckpoint = false;
    gPlayerDead = false;
    gPlayerDeathEyeHeight = ORIG_PLAYER_EYES_HEIGHT;
    gPlayerDeathWaitTicks = 60;
    gPlayerDeathTickAccumulator = 0.0f;
    gRedFlashUntil = 0.0;
    gLevelExitActive = false;
    gLevelExitStarted = 0.0;
    gLevelExitCompleteAfter = 1.15;
    gTeleportActive = false;
    gTeleportStarted = 0.0;
    gTeleportCompleteAfter = 32.0 / 50.0;
    gRuntimeObjects.clear();
    gRuntimeProjectiles.clear();
    gRuntimeImpactSparks.clear();
    gRuntimeObjects.reserve(ai->lgldPlacedObjects.size());
    for (size_t i = 0; i < ai->lgldPlacedObjects.size(); ++i) {
        const LgldPlacedObject& placed = ai->lgldPlacedObjects[i];
        if (placed.objectCode == 0u) continue;
        RuntimeObject runtime;
        runtime.placedIndex = i;
        runtime.x = (float)placed.worldX / 64.0f;
        runtime.y = (float)placed.worldY / 64.0f;
        runtime.heading = (float)placed.heading * (6.28318530718f / 2048.0f);
        runtime.bobPhase = (float)(i % 17u) * 0.37f;
        runtime.stateClock = 4.0f / 25.0f;
        runtime.attackClock = (float)((i * 17u + 11u) & 63u) / 25.0f;
        runtime.behaviorCounter = 4;
        runtime.lastSeenX = runtime.x;
        runtime.lastSeenY = runtime.y;
        runtime.turnDirection = (i & 1u) ? 1 : -1;
        std::map<std::string, GlobalObjectInfo>::const_iterator found = gGlobalObjectInfo.find(placed.name);
        if (found != gGlobalObjectInfo.end()) runtime.health = std::max(1, found->second.param[2]);
        gRuntimeObjects.push_back(runtime);
    }
    gObjectLastTime = nowSeconds();
    gPickupMessage.clear();
    gPickupMessageUntil = 0.0;
    gPlayerFalling = false;
    gPlayerVerticalSpeed = 0.0f;
    gHazardClock = 0.0;
    if (gGodMode) applyGodModeLoadout();
    gCheckpointHealth = gPlayerHealth;
    gCheckpointShields = gPlayerShields;
    gCheckpointEnergy = gPlayerEnergy;
    gCheckpointCredits = gPlayerCredits;
    gCheckpointScore = gPlayerScore;
    gCheckpointWeapon = gPlayerWeapon;
    memcpy(gCheckpointWeapons, gPlayerWeapons, sizeof(gCheckpointWeapons));
    memcpy(gCheckpointKeys, gPlayerKeys, sizeof(gCheckpointKeys));
    if (restoredCheckpoint) {
        markGameProgressDirty();
        saveGameProgress();
    }
    LOGI("runtime level initialized asset=%s blocks=%u effects=%u objects=%u", ai->name.c_str(),
         (unsigned int)gRuntimeBlocks.size(), (unsigned int)ai->lgldEffectData.size(),
         (unsigned int)ai->lgldPlacedObjects.size());
}

static const LgldBlockInfo* currentLgldBlockByIndex(int blockIndex) {
    const AssetInfo* ai = currentLgldAsset();
    if (!ai || blockIndex <= 0 || blockIndex >= (int)ai->lgldBlockData.size()) return nullptr;
    if (gRuntimeAssetIndex == gAssetIndex && gRuntimeBlocks.size() == ai->lgldBlockData.size()) return &gRuntimeBlocks[(size_t)blockIndex];
    return &ai->lgldBlockData[(size_t)blockIndex];
}

static __attribute__((unused)) LgldBlockInfo* runtimeLgldBlockByIndex(int blockIndex) {
    ensureRuntimeLevelState();
    if (blockIndex <= 0 || blockIndex >= (int)gRuntimeBlocks.size()) return nullptr;
    return &gRuntimeBlocks[(size_t)blockIndex];
}

static const LgldBlockInfo* currentLgldBlockForCell(int mx, int my) {
    const AssetInfo* ai = currentLgldAsset();
    if (!ai) return nullptr;
    int v = currentLgldCellRaw(mx, my);
    if (v < 0) v = -v;
    if (v <= 0 || v >= (int)ai->lgldBlockData.size()) return nullptr;
    return currentLgldBlockByIndex(v);
}

static const GlobalObjectInfo* objectDefinition(const std::string& name) {
    std::map<std::string, GlobalObjectInfo>::const_iterator it = gGlobalObjectInfo.find(name);
    return it == gGlobalObjectInfo.end() ? nullptr : &it->second;
}

static const LgldEdgeInfo* currentLgldEdgeByIndex(int edgeIndex);
static int originalEdgeFaceForStep(int side, int stepX, int stepY);
static int edgeIndexFromBlockFace(int blockIndex, int edgeFace);

static bool transitionHasSolidEdge(int fromX, int fromY, int toX, int toY) {
    if (fromX == toX && fromY == toY) return false;
    if (currentLgldCellRaw(toX, toY) <= 0) return true;
    const int side = fromX != toX ? 0 : 1;
    const int stepX = toX > fromX ? 1 : (toX < fromX ? -1 : 0);
    const int stepY = toY > fromY ? 1 : (toY < fromY ? -1 : 0);
    const int blockIndex = (int)currentLgldCellRaw(toX, toY);
    if (blockIndex <= 0) return true;
    const int edgeFace = originalEdgeFaceForStep(side, stepX, stepY);
    const LgldEdgeInfo* edge = currentLgldEdgeByIndex(edgeIndexFromBlockFace(blockIndex, edgeFace));
    return edge && edge->normTex != 0;
}

static bool clearObjectLine(float x0, float y0, float x1, float y1) {
    const float dx = x1 - x0, dy = y1 - y0;
    const int steps = std::max(1, (int)ceilf(sqrtf(dx * dx + dy * dy) * 16.0f));
    int previousX = (int)floorf(x0), previousY = (int)floorf(y0);
    for (int i = 1; i <= steps; ++i) {
        const float t = (float)i / (float)steps;
        const int mx = (int)floorf(x0 + dx * t), my = (int)floorf(y0 + dy * t);
        if (!currentLgldOpenCell(mx, my)) return false;
        const LgldBlockInfo* block = currentLgldBlockForCell(mx, my);
        if (!block || block->ceilHeight - block->floorHeight < ORIG_PLAYER_HEIGHT) return false;
        if (mx != previousX && transitionHasSolidEdge(previousX, previousY, mx, previousY)) return false;
        if (my != previousY && transitionHasSolidEdge(mx, previousY, mx, my)) return false;
        previousX = mx;
        previousY = my;
    }
    return true;
}

static bool enemyCanOccupy(float x, float y, int height, int previousFloor, float radius) {
    static const float hull[9][2] = {
        {0.0f, 0.0f}, {-1.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, -1.0f}, {0.0f, 1.0f},
        {-0.7071f, -0.7071f}, {0.7071f, -0.7071f}, {-0.7071f, 0.7071f}, {0.7071f, 0.7071f}
    };
    const int centerX = (int)floorf(x);
    const int centerY = (int)floorf(y);
    for (int sample = 0; sample < 9; ++sample) {
        const int mx = (int)floorf(x + hull[sample][0] * radius);
        const int my = (int)floorf(y + hull[sample][1] * radius);
        if (mx != centerX && transitionHasSolidEdge(centerX, centerY, mx, centerY)) return false;
        if (my != centerY && transitionHasSolidEdge(mx, centerY, mx, my)) return false;
        if (!currentLgldOpenCell(mx, my)) return false;
        const LgldBlockInfo* block = currentLgldBlockForCell(mx, my);
        if (!block || block->ceilHeight - block->floorHeight < height) return false;
        if ((block->attributes & 8u) != 0u) return false;
        if (block->floorHeight - previousFloor > ORIG_PLAYER_MAX_RISE) return false;
    }
    return true;
}

static void damagePlayer(int damage) {
    if (damage <= 0 || gPlayerHealth <= 0 || gGodMode) return;
    // Scores.asm PlayerHit sends 75% of the hit to shields (rounded up),
    // with the rest reducing health, and keeps the red palette for 13/50 s.
    const int shieldShare = (damage * 3 + 3) / 4;
    const int absorbed = std::min(gPlayerShields, shieldShare);
    gPlayerShields -= absorbed;
    gPlayerHealth = std::max(0, gPlayerHealth - (damage - absorbed));
    if (gPlayerHealth > 0) markGameProgressDirty();
    gRedFlashUntil = nowSeconds() + 13.0 / 50.0;
    playGlobalSoundCode(4);
    if (gPlayerHealth == 0) {
        stopSoundGroup(1);
        gPlayerDead = true;
        gPlayerDeathEyeHeight = ORIG_PLAYER_EYES_HEIGHT;
        gPlayerDeathWaitTicks = 60;
        gPlayerDeathTickAccumulator = 0.0f;
        gPlayerBobPhase = 0.0f;
        gPlayerBobOffset = 0.0f;
        gAnalogLX = gAnalogLY = gAnalogRX = gAnalogRY = 0.0f;
        gFireHeld = false;
        gFireLatch = false;
        gRunHeld = false;
        gNextAutoFireTime = 0.0;
        gFireReleaseDeadline = 0.0;
    }
}

static void applyGodModeLoadout() {
    gGodMode = true;
    gPlayerHealth = 100;
    gPlayerShields = 100;
    gPlayerEnergy = 9999;
    for (size_t i = 0; i < sizeof(gPlayerWeapons); ++i) gPlayerWeapons[i] = 2;
    gPlayerDead = false;
    markGameProgressDirty();
}

static void collectRuntimePickup(RuntimeObject& runtime, const GlobalObjectInfo& definition) {
    const int subtype = definition.param[0] & 0xff;
    const int value = std::max(1, definition.param[1]);
    bool accepted = true;
    if (subtype == 0) { if (gPlayerHealth >= 100) accepted = false; else { gPlayerHealth = std::min(100, gPlayerHealth + value); gPickupMessage = "HEALTH"; } }
    else if (subtype == 1) { if (gPlayerShields >= 100) accepted = false; else { gPlayerShields = std::min(100, gPlayerShields + value); gPickupMessage = "SHIELDS"; } }
    else if (subtype == 2) { if (gPlayerEnergy >= 9999) accepted = false; else { gPlayerEnergy = std::min(9999, gPlayerEnergy + value); gPickupMessage = "ENERGY"; } }
    else if (subtype == 3) { if (gPlayerCredits >= 99999) accepted = false; else { gPlayerCredits = std::min(99999, gPlayerCredits + value); gPickupMessage = "CREDITS"; } }
    else if (subtype >= 4 && subtype <= 7) {
        const int key = subtype - 4;
        if (gPlayerKeys[key]) accepted = false;
        else { gPlayerKeys[key] = true; static const char* names[4] = {"GREEN KEY","YELLOW KEY","RED KEY","BLUE KEY"}; gPickupMessage = names[key]; }
    }
    else if (subtype >= 8 && subtype < 8 + PLAYER_WEAPON_COUNT) {
        const int weapon = subtype - 8;
        if (gPlayerWeapons[weapon] >= 2) accepted = false;
        else { ++gPlayerWeapons[weapon]; gPlayerWeapon = weapon; gPickupMessage = gPlayerWeapons[weapon] == 1 ? "NEW WEAPON" : "WEAPON BOOST"; }
    } else { gPickupMessage = "ITEM"; }
    if (!accepted) return;
    runtime.collected = true;
    gPickupMessageUntil = nowSeconds() + 2.0;
    markGameProgressDirty();
    playSoundResource(definition.sound[0].empty() ? "ITM1" : definition.sound[0]);
}

static const GlobalObjectInfo* shotDefinitionForCode(int code) {
    for (std::map<std::string, GlobalObjectInfo>::const_iterator it = gGlobalObjectInfo.begin(); it != gGlobalObjectInfo.end(); ++it)
        if (it->second.objectType == 4u && (it->second.param[6] & 0xff) == (code & 0xff)) return &it->second;
    return nullptr;
}

static void spawnProjectile(const GlobalObjectInfo* definition, float x, float y, float z,
                            float targetX, float targetY, float targetZ, bool enemy, int damageMultiplier) {
    if (!definition) return;
    const float dx = targetX - x, dy = targetY - y;
    const float planarDistance = std::max(0.001f, sqrtf(dx * dx + dy * dy));
    RuntimeProjectile projectile;
    projectile.definition = definition;
    projectile.dirX = dx / planarDistance;
    projectile.dirY = dy / planarDistance;
    // This port has no vertical-look control. Keep all shots on their muzzle
    // plane so player and enemy projectiles remain visibly horizontal.
    (void)targetZ;
    projectile.verticalSlope = 0.0f;
    projectile.x = x + projectile.dirX * 0.25f;
    projectile.y = y + projectile.dirY * 0.25f;
    projectile.z = z;
    // Objects.asm PlayerShot copies param2, param5 and param6 to speed,
    // acceleration and maximum speed.  Movement is updated at 50 Hz.
    projectile.speedUnits = (float)std::max(1, abs(definition->param[1]));
    projectile.accelerationUnits = (float)std::max(0, abs(definition->param[4]));
    projectile.maxSpeedUnits = (float)std::max((int)projectile.speedUnits, abs(definition->param[5]));
    projectile.speed = projectile.speedUnits * 50.0f / 64.0f;
    projectile.maxDistance = (float)std::max(64, abs(definition->param[3])) / 64.0f;
    projectile.damage = std::max(1, abs(definition->param[0])) * std::max(1, damageMultiplier);
    projectile.enemy = enemy;
    gRuntimeProjectiles.push_back(projectile);
}

static const GlobalObjectInfo* explosionDefinitionForCode(int code) {
    for (std::map<std::string, GlobalObjectInfo>::const_iterator it = gGlobalObjectInfo.begin();
         it != gGlobalObjectInfo.end(); ++it) {
        if (it->second.objectType == 5u && it->second.param[0] == code) return &it->second;
    }
    return nullptr;
}

static bool hasEnemyFallingAnimation(const GlobalObjectInfo& definition) {
    return definition.frames.size() > 42u && definition.frameOffsets.size() > 42u &&
           definition.frameOffsets[41] != definition.frameOffsets[42];
}

static void beginEnemyExplosion(RuntimeObject& target, const GlobalObjectInfo& enemyDefinition,
                                int explosionCode) {
    const GlobalObjectInfo* explosion = explosionDefinitionForCode(explosionCode);
    if (!explosion || explosion->frames.empty()) {
        target.dying = false;
        target.exploding = false;
        target.corpse = true;
        target.animationFrame = enemyDefinition.frames.size() > 128u ? 128 :
            std::max(0, (int)enemyDefinition.frames.size() - 1);
        return;
    }
    target.dying = true;
    target.exploding = true;
    target.corpse = false;
    target.deathClock = 0.0f;
    target.deathDefinition = explosion;
    target.deathYOffset = (enemyDefinition.height - explosion->height) / 2;
    target.animationFrame = 0;
    playSoundResource(explosion->sound[0], 0.0f, 0, false, true);
}

static void damageRuntimeEnemy(RuntimeObject& target, const GlobalObjectInfo& definition, int damage,
                               const GlobalObjectInfo* projectileDefinition, float projectileHeading) {
    target.health -= std::max(1, damage);
    target.alertClock = 8.0f;
    target.aiState = 0;
    if (target.health <= 0) {
        target.health = 0;
        target.heading = projectileHeading;
        target.corpse = false;
        target.deathDefinition = nullptr;
        target.deathYOffset = 0;
        target.deathClock = 0.0f;
        gPlayerScore += std::max(0, definition.param[1]);
        const bool projectileForcesExplosion = projectileDefinition && projectileDefinition->param[8] >= 0;
        if (!projectileForcesExplosion && hasEnemyFallingAnimation(definition)) {
            target.dying = true;
            target.exploding = false;
            target.animationFrame = 42;
            // Keep the complete original death cry alive even if shots or
            // ambient effects request additional mixer voices meanwhile.
            playSoundResource(definition.sound[2], 0.0f, 0, false, true);
        } else {
            const int explosionCode = projectileForcesExplosion ? projectileDefinition->param[8] : definition.param[3];
            beginEnemyExplosion(target, definition, explosionCode);
        }
    } else playSoundResource(definition.sound[1]);
}

static unsigned int projectileImpactColor(const GlobalObjectInfo& definition) {
    const ObjectSpriteFrame* frame = definition.frames.empty() ? nullptr : &definition.frames[0];
    const std::vector<unsigned char>* pixels = frame ? &frame->pixels : &definition.spritePixels;
    const std::vector<unsigned char>* mask = frame ? &frame->mask : &definition.spriteMask;
    if (!pixels || pixels->empty()) return 0xffffffffu;

    uint64_t red = 0u, green = 0u, blue = 0u, samples = 0u;
    for (size_t i = 0; i < pixels->size(); ++i) {
        if (mask && i < mask->size() && (*mask)[i] == 0u) continue;
        const unsigned int color = paletteColor((*pixels)[i]);
        const unsigned int r = color & 0xffu;
        const unsigned int g = (color >> 8) & 0xffu;
        const unsigned int b = (color >> 16) & 0xffu;
        if (std::max(r, std::max(g, b)) < 32u) continue;
        red += r; green += g; blue += b; ++samples;
    }
    if (samples == 0u) return 0xffffffffu;
    unsigned int r = (unsigned int)(red / samples);
    unsigned int g = (unsigned int)(green / samples);
    unsigned int b = (unsigned int)(blue / samples);
    const unsigned int strongest = std::max(r, std::max(g, b));
    if (strongest > 0u && strongest < 224u) {
        r = std::min(255u, r * 224u / strongest);
        g = std::min(255u, g * 224u / strongest);
        b = std::min(255u, b * 224u / strongest);
    }
    return 0xff000000u | (b << 16) | (g << 8) | r;
}

static float impactSparkRandom() {
    static unsigned int state = 0x6d2b79f5u;
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return (float)(state & 0xffffu) / 65535.0f;
}

static void spawnProjectileImpactSparks(const RuntimeProjectile& projectile,
                                        float impactX, float impactY, float impactZ,
                                        float normalX, float normalY, float normalZ) {
    if (!projectile.definition) return;
    const float horizontalLength = sqrtf(normalX * normalX + normalY * normalY);
    if (horizontalLength > 0.0001f) {
        normalX /= horizontalLength;
        normalY /= horizontalLength;
    } else if (fabsf(normalZ) < 0.0001f) {
        normalX = -projectile.dirX;
        normalY = -projectile.dirY;
    }
    const unsigned int color = projectileImpactColor(*projectile.definition);
    const int sparkCount = 5;
    for (int i = 0; i < sparkCount; ++i) {
        const float tangent = (impactSparkRandom() - 0.5f) * 1.9f;
        const float outward = 0.65f + impactSparkRandom() * 0.85f;
        RuntimeImpactSpark spark;
        spark.x = impactX + normalX * 0.018f;
        spark.y = impactY + normalY * 0.018f;
        spark.z = impactZ + normalZ * 0.5f;
        spark.previousX = spark.x;
        spark.previousY = spark.y;
        spark.previousZ = spark.z;
        spark.velocityX = normalX * outward - normalY * tangent;
        spark.velocityY = normalY * outward + normalX * tangent;
        spark.velocityZ = normalZ * (28.0f + impactSparkRandom() * 28.0f) +
            (horizontalLength > 0.0001f ? 20.0f + impactSparkRandom() * 52.0f
                                        : (impactSparkRandom() - 0.5f) * 32.0f);
        spark.lifetime = 0.22f + impactSparkRandom() * 0.18f;
        spark.color = color;
        gRuntimeImpactSparks.push_back(spark);
    }
}

static void updateRuntimeImpactSparks(float dt) {
    for (size_t i = 0; i < gRuntimeImpactSparks.size(); ++i) {
        RuntimeImpactSpark& spark = gRuntimeImpactSparks[i];
        if (spark.dead) continue;
        spark.age += dt;
        if (spark.age >= spark.lifetime) { spark.dead = true; continue; }
        spark.previousX = spark.x;
        spark.previousY = spark.y;
        spark.previousZ = spark.z;
        const float nextX = spark.x + spark.velocityX * dt;
        const float nextY = spark.y + spark.velocityY * dt;
        float nextZ = spark.z + spark.velocityZ * dt;
        const int fromX = (int)floorf(spark.x), fromY = (int)floorf(spark.y);
        const int toX = (int)floorf(nextX), toY = (int)floorf(nextY);
        if ((toX != fromX && transitionHasSolidEdge(fromX, fromY, toX, fromY)) ||
            (toY != fromY && transitionHasSolidEdge(toX, fromY, toX, toY))) {
            spark.dead = true;
            continue;
        }
        const LgldBlockInfo* block = currentLgldBlockForCell(toX, toY);
        if (!block || nextZ >= (float)block->ceilHeight) { spark.dead = true; continue; }
        if (nextZ <= (float)block->floorHeight) {
            nextZ = (float)block->floorHeight + 0.5f;
            if (spark.velocityZ < 0.0f) spark.velocityZ = -spark.velocityZ * 0.30f;
            spark.velocityX *= 0.68f;
            spark.velocityY *= 0.68f;
        }
        spark.x = nextX;
        spark.y = nextY;
        spark.z = nextZ;
        spark.velocityZ -= 150.0f * dt;
    }
    gRuntimeImpactSparks.erase(std::remove_if(gRuntimeImpactSparks.begin(), gRuntimeImpactSparks.end(),
        [](const RuntimeImpactSpark& spark) { return spark.dead; }), gRuntimeImpactSparks.end());
}

static void updateRuntimeProjectiles(float dt) {
    const AssetInfo* ai = currentLgldAsset();
    if (!ai) return;
    for (size_t projectileIndex = 0; projectileIndex < gRuntimeProjectiles.size(); ++projectileIndex) {
        RuntimeProjectile& projectile = gRuntimeProjectiles[projectileIndex];
        if (projectile.dead || !projectile.definition) continue;
        projectile.speedUnits = std::min(projectile.maxSpeedUnits,
            projectile.speedUnits + projectile.accelerationUnits * dt * 50.0f);
        projectile.speed = projectile.speedUnits * 50.0f / 64.0f;
        const float distance = projectile.speed * dt;
        const int substeps = std::max(1, std::min(256, (int)ceilf(distance * 20.0f)));
        const float stepDistance = distance / (float)substeps;
        int previousCellX = (int)floorf(projectile.x);
        int previousCellY = (int)floorf(projectile.y);
        for (int substep = 0; substep < substeps && !projectile.dead; ++substep) {
            const float nextX = projectile.x + projectile.dirX * stepDistance;
            const float nextY = projectile.y + projectile.dirY * stepDistance;
            const float nextZ = projectile.z + projectile.verticalSlope * stepDistance;
            const int nextCellX = (int)floorf(nextX);
            const int nextCellY = (int)floorf(nextY);

            if (nextCellX != previousCellX &&
                transitionHasSolidEdge(previousCellX, previousCellY, nextCellX, previousCellY)) {
                const float normalX = nextCellX > previousCellX ? -1.0f : 1.0f;
                const float boundaryX = nextCellX > previousCellX
                    ? (float)nextCellX : (float)previousCellX;
                const float fraction = fabsf(nextX - projectile.x) > 0.00001f
                    ? (boundaryX - projectile.x) / (nextX - projectile.x) : 0.0f;
                const float impactY = projectile.y + (nextY - projectile.y) * fraction;
                const float impactZ = projectile.z + (nextZ - projectile.z) * fraction;
                spawnProjectileImpactSparks(projectile, boundaryX, impactY,
                    impactZ + (float)std::max(1, projectile.definition->height) * 0.5f,
                    normalX, 0.0f, 0.0f);
                projectile.dead = true;
                break;
            }
            if (nextCellY != previousCellY &&
                transitionHasSolidEdge(nextCellX, previousCellY, nextCellX, nextCellY)) {
                const float normalY = nextCellY > previousCellY ? -1.0f : 1.0f;
                const float boundaryY = nextCellY > previousCellY
                    ? (float)nextCellY : (float)previousCellY;
                const float fraction = fabsf(nextY - projectile.y) > 0.00001f
                    ? (boundaryY - projectile.y) / (nextY - projectile.y) : 0.0f;
                const float impactX = projectile.x + (nextX - projectile.x) * fraction;
                const float impactZ = projectile.z + (nextZ - projectile.z) * fraction;
                spawnProjectileImpactSparks(projectile, impactX, boundaryY,
                    impactZ + (float)std::max(1, projectile.definition->height) * 0.5f,
                    0.0f, normalY, 0.0f);
                projectile.dead = true;
                break;
            }

            const LgldBlockInfo* block = currentLgldBlockForCell(nextCellX, nextCellY);
            const int projectileHeight = std::max(1, projectile.definition->height);
            // A sprite may touch a floor/ceiling plane exactly. Reject only a
            // true penetration; otherwise floor-level effects vanish instantly.
            if (!block || nextZ < (float)block->floorHeight ||
                nextZ + projectileHeight > (float)block->ceilHeight) {
                spawnProjectileImpactSparks(projectile, projectile.x, projectile.y,
                    projectile.z + (float)projectileHeight * 0.5f,
                    -projectile.dirX, -projectile.dirY, 0.0f);
                projectile.dead = true;
                break;
            }

            projectile.x = nextX;
            projectile.y = nextY;
            projectile.z = nextZ;
            projectile.travelled += stepDistance;
            previousCellX = nextCellX;
            previousCellY = nextCellY;
            if (projectile.travelled >= projectile.maxDistance) {
                projectile.dead = true;
                break;
            }

            if (projectile.enemy) {
                const float dx = projectile.x - gPlayerX, dy = projectile.y - gPlayerY;
                if (!gPlayerDead && dx * dx + dy * dy < 0.16f &&
                    projectile.z >= gPlayerBaseZF - 4.0f &&
                    projectile.z <= gPlayerBaseZF + ORIG_PLAYER_HEIGHT + 4.0f) {
                    damagePlayer(projectile.damage);
                    projectile.dead = true;
                }
                continue;
            }

            for (size_t objectIndex = 0; objectIndex < gRuntimeObjects.size(); ++objectIndex) {
                RuntimeObject& target = gRuntimeObjects[objectIndex];
                if (target.dead || target.dying || target.corpse || target.collected ||
                    target.placedIndex >= ai->lgldPlacedObjects.size()) continue;
                const LgldPlacedObject& placed = ai->lgldPlacedObjects[target.placedIndex];
                const GlobalObjectInfo* definition = objectDefinition(placed.name);
                if (!definition || definition->objectType != 2u) continue;
                const float dx = projectile.x - target.x, dy = projectile.y - target.y;
                const float radius = std::max(10, definition->radius) / 64.0f;
                const LgldBlockInfo* enemyBlock = currentLgldBlockForCell((int)floorf(target.x), (int)floorf(target.y));
                const float enemyFloor = enemyBlock ? (float)enemyBlock->floorHeight : 0.0f;
                if (dx * dx + dy * dy <= radius * radius && projectile.z >= enemyFloor - 2.0f &&
                    projectile.z <= enemyFloor + definition->height + 2.0f) {
                    damageRuntimeEnemy(target, *definition, projectile.damage, projectile.definition,
                                       atan2f(projectile.dirY, projectile.dirX));
                    projectile.dead = true;
                    break;
                }
            }
        }
    }
    gRuntimeProjectiles.erase(std::remove_if(gRuntimeProjectiles.begin(), gRuntimeProjectiles.end(),
        [](const RuntimeProjectile& projectile) { return projectile.dead; }), gRuntimeProjectiles.end());
}

static bool enemyCanMove(size_t movingIndex, float x, float y, int height, int previousFloor, float radius) {
    const int fromX = (int)floorf(gRuntimeObjects[movingIndex].x);
    const int fromY = (int)floorf(gRuntimeObjects[movingIndex].y);
    const int toX = (int)floorf(x);
    const int toY = (int)floorf(y);
    if (toX != fromX && transitionHasSolidEdge(fromX, fromY, toX, fromY)) return false;
    if (toY != fromY && transitionHasSolidEdge(toX, fromY, toX, toY)) return false;
    if (!enemyCanOccupy(x, y, height, previousFloor, radius)) return false;
    for (size_t i = 0; i < gRuntimeObjects.size(); ++i) {
        if (i == movingIndex || gRuntimeObjects[i].dead || gRuntimeObjects[i].collected) continue;
        const float dx = x - gRuntimeObjects[i].x, dy = y - gRuntimeObjects[i].y;
        if (dx * dx + dy * dy < 0.12f) return false;
    }
    return true;
}

static float normalizedAngleDifference(float target, float current) {
    return atan2f(sinf(target - current), cosf(target - current));
}

static float turnEnemyToward(float current, float target, float maximumTurn) {
    const float difference = normalizedAngleDifference(target, current);
    if (difference > maximumTurn) return current + maximumTurn;
    if (difference < -maximumTurn) return current - maximumTurn;
    return target;
}

static bool chooseEnemyCollisionHeading(size_t movingIndex, RuntimeObject& runtime,
                                        float desiredHeading, int height, int oldFloor,
                                        float radius, float movementStep) {
    // Objects.asm statuses 2/3 keep turning in 45-degree steps after a
    // collision. Probe all eight original headings here so an enemy selects a
    // passable direction along the obstacle instead of repeatedly pushing into
    // the same wall.
    static const int positiveOffsets[8] = {0, 1, -1, 2, -2, 3, -3, 4};
    static const int negativeOffsets[8] = {0, -1, 1, -2, 2, -3, 3, 4};
    const int* offsets = runtime.turnDirection < 0 ? negativeOffsets : positiveOffsets;
    const float angleStep = 0.78539816339f;
    const float baseHeading = floorf(desiredHeading / angleStep + 0.5f) * angleStep;
    const float probe = std::max(0.14f, movementStep * 3.0f);
    const float currentDistance = hypotf(gPlayerX - runtime.x, gPlayerY - runtime.y);
    float bestHeading = runtime.heading;
    float bestDistance = currentDistance;
    float bestScore = 1.0e30f;
    bool found = false;
    for (int candidateIndex = 0; candidateIndex < 8; ++candidateIndex) {
        const float candidate = baseHeading + (float)offsets[candidateIndex] * angleStep;
        const float nx = runtime.x + cosf(candidate) * probe;
        const float ny = runtime.y + sinf(candidate) * probe;
        if (!enemyCanMove(movingIndex, nx, ny, height, oldFloor, radius)) continue;
        const float newDistance = hypotf(gPlayerX - nx, gPlayerY - ny);
        const float turnCost = fabsf(normalizedAngleDifference(candidate, runtime.heading));
        const float score = newDistance + 0.035f * turnCost;
        if (score < bestScore) {
            bestScore = score;
            bestHeading = candidate;
            bestDistance = newDistance;
            found = true;
        }
    }
    if (!found) {
        ++runtime.collisionAttempts;
        if ((runtime.collisionAttempts & 1) == 0) runtime.turnDirection = -runtime.turnDirection;
        runtime.heading += (float)runtime.turnDirection * angleStep;
        return false;
    }
    const float turn = normalizedAngleDifference(bestHeading, runtime.heading);
    if (fabsf(turn) > 0.01f) runtime.turnDirection = turn < 0.0f ? -1 : 1;
    runtime.heading = bestHeading;
    runtime.collisionAttempts = bestDistance < currentDistance ? 0 : runtime.collisionAttempts + 1;
    return true;
}

static unsigned int enemyRandomValue(size_t index, unsigned int salt) {
    unsigned int value = (unsigned int)(index + 1u) * 1103515245u + gFrame * 12345u + salt * 2654435761u;
    value ^= value >> 13;
    value *= 1274126177u;
    return value ^ (value >> 16);
}

static bool updateRuntimeDeathAnimation(RuntimeObject& runtime,
                                        const GlobalObjectInfo& enemyDefinition, float dt) {
    if (!runtime.dying) return false;
    runtime.deathClock += dt;
    if (runtime.exploding && runtime.deathDefinition) {
        const GlobalObjectInfo& explosion = *runtime.deathDefinition;
        const int frame = (int)(runtime.deathClock / 0.08f);
        if (frame < (int)explosion.frames.size()) {
            runtime.animationFrame = frame;
            return true;
        }
        runtime.dying = false;
        runtime.exploding = false;
        if (explosion.param[1] == 0) {
            runtime.dead = true;
            runtime.deathDefinition = nullptr;
        } else {
            runtime.corpse = true;
            if (explosion.param[1] < 0) {
                runtime.deathDefinition = nullptr;
                runtime.deathYOffset = 0;
                runtime.animationFrame = enemyDefinition.frames.size() > 128u ? 128 :
                    std::max(0, (int)enemyDefinition.frames.size() - 1);
            } else runtime.animationFrame = std::max(0, (int)explosion.frames.size() - 1);
        }
        return true;
    }
    while (runtime.dying && runtime.deathClock >= 0.16f) {
        runtime.deathClock -= 0.16f;
        const int frame = runtime.animationFrame;
        const bool repeatedFrame = frame + 1 < (int)enemyDefinition.frameOffsets.size() &&
            enemyDefinition.frameOffsets[(size_t)frame] == enemyDefinition.frameOffsets[(size_t)frame + 1u];
        if (frame >= 44 || repeatedFrame) {
            runtime.dying = false;
            runtime.corpse = true;
            runtime.animationFrame = enemyDefinition.frames.size() > 129u ? 129 :
                (enemyDefinition.frames.size() > 128u ? 128 :
                 std::max(0, (int)enemyDefinition.frames.size() - 1));
        } else ++runtime.animationFrame;
    }
    return true;
}

static void updateRuntimeObjects() {
    const AssetInfo* ai = currentLgldAsset();
    if (!ai || gRuntimeAssetIndex != gAssetIndex) return;
    const double now = nowSeconds();
    float dt = gObjectLastTime > 0.0 ? (float)(now - gObjectLastTime) : 0.0f;
    gObjectLastTime = now;
    dt = std::max(0.0f, std::min(0.08f, dt));
    updateRuntimeProjectiles(dt);
    updateRuntimeImpactSparks(dt);
    for (size_t i = 0; i < gRuntimeObjects.size(); ++i) {
        RuntimeObject& runtime = gRuntimeObjects[i];
        if (runtime.collected || runtime.dead || runtime.placedIndex >= ai->lgldPlacedObjects.size()) continue;
        const LgldPlacedObject& placed = ai->lgldPlacedObjects[runtime.placedIndex];
        const GlobalObjectInfo* definition = objectDefinition(placed.name);
        if (!definition) continue;
        if (updateRuntimeDeathAnimation(runtime, *definition, dt)) continue;
        if (runtime.corpse) continue;
        runtime.bobPhase += dt * 4.0f;
        if (definition->numFrames > 1 && definition->animationType > 0)
            runtime.animationFrame = (int)(now * 10.0) % definition->numFrames;
        const float dx = gPlayerX - runtime.x, dy = gPlayerY - runtime.y;
        const float distance = sqrtf(dx * dx + dy * dy);
        if (definition->objectType == 3u) {
            if (!gPlayerDead && distance <= ((float)definition->radius + 18.0f) / 64.0f)
                collectRuntimePickup(runtime, *definition);
            continue;
        }
        if (definition->objectType != 2u) continue;
        if (placed.activationTrigger != 0u && gActiveEnemyTriggers.count(placed.activationTrigger) == 0u) continue;
        runtime.contactClock = std::max(0.0f, runtime.contactClock - dt);
        runtime.stateClock -= dt;
        const float attackDistance = std::max(24, definition->param[0]) / 64.0f;
        const float desiredHeading = atan2f(dy, dx);

        // Objects.asm only suspends enemies beyond MAX_ENEMY_DIST (20 blocks).
        // Seeking and random movement do not require line-of-sight.
        if (distance > 20.0f) continue;

        const float contactDistance = sqrtf((float)(definition->radius * definition->radius + 2 * 16 * 16)) / 64.0f;
        if (!gPlayerDead && distance <= contactDistance && runtime.contactClock <= 0.0f) {
            damagePlayer(std::max(1, abs(definition->param[6])));
            runtime.contactClock = 10.0f / 25.0f;
        }

        // Status 4/-1: turn toward the player for four preparation steps,
        // then fire and return to seek/random movement.
        if (runtime.aiState == 4) {
            if (runtime.stateClock <= 0.0f) {
                runtime.heading = turnEnemyToward(runtime.heading, desiredHeading, 0.78539816339f);
                --runtime.behaviorCounter;
                runtime.stateClock = 3.0f / 25.0f;
            }
            if (runtime.behaviorCounter > 0) continue;
            const int gunCode = definition->param[4] & 0xff;
            const GlobalObjectInfo* shot = shotDefinitionForCode(gunCode);
            const LgldBlockInfo* enemyBlock = currentLgldBlockForCell((int)floorf(runtime.x), (int)floorf(runtime.y));
            const float floor = enemyBlock ? (float)enemyBlock->floorHeight : 0.0f;
            const float pan = std::max(-1.0f, std::min(1.0f, (runtime.x - gPlayerX) / 6.0f));
            if (!gPlayerDead && shot && clearObjectLine(runtime.x, runtime.y, gPlayerX, gPlayerY)) {
                spawnProjectile(shot, runtime.x, runtime.y, floor + definition->param[11],
                                gPlayerX, gPlayerY, gPlayerBaseZF + ORIG_PLAYER_EYES_HEIGHT, true, 1);
                playSoundResource(shot->sound[0], pan);
            } else if (!gPlayerDead && !shot && distance < 0.65f &&
                       clearObjectLine(runtime.x, runtime.y, gPlayerX, gPlayerY)) {
                damagePlayer(std::max(1, abs(definition->param[6])));
                playSoundResource(definition->sound[0], pan);
            }
            const int probability = std::max(1, abs(definition->param[8]));
            const unsigned int randomDelay = enemyRandomValue(i, 1u) % (unsigned int)probability;
            runtime.attackClock = (float)(8 + probability + randomDelay * 4u) / 25.0f;
            runtime.aiState = (enemyRandomValue(i, 2u) & 1u) ? 1 : 0;
            runtime.behaviorCounter = runtime.aiState == 0 ? 4 : 2;
            runtime.stateClock = runtime.aiState == 0 ? 4.0f / 25.0f : 2.0f / 25.0f;
            continue;
        }

        if (!gPlayerDead && distance <= attackDistance && clearObjectLine(runtime.x, runtime.y, gPlayerX, gPlayerY)) {
            runtime.attackClock = std::max(0.0f, runtime.attackClock - dt);
            if (runtime.attackClock <= 0.0f) {
                runtime.aiState = 4;
                runtime.behaviorCounter = 4;
                runtime.stateClock = 1.0f / 25.0f;
                continue;
            }
        }

        // ChooseEnemyDir: seek for four cycles, random-walk for two cycles;
        // headings change in the original 45-degree increments.
        if (runtime.stateClock <= 0.0f) {
            if (runtime.aiState == 0) {
                runtime.heading = turnEnemyToward(runtime.heading, desiredHeading, 0.78539816339f);
                if (--runtime.behaviorCounter <= 0) { runtime.aiState = 1; runtime.behaviorCounter = 2; }
            } else if (runtime.aiState == 1) {
                const unsigned int random = enemyRandomValue(i, (unsigned int)runtime.behaviorCounter + 7u) & 7u;
                if (random == 6u) runtime.heading -= 0.78539816339f;
                else if (random == 7u) runtime.heading += 0.78539816339f;
                if (--runtime.behaviorCounter <= 0) { runtime.aiState = 0; runtime.behaviorCounter = 5; }
            } else if (runtime.aiState == 2) {
                runtime.heading += (float)runtime.turnDirection * 0.78539816339f;
                runtime.aiState = 0;
                runtime.behaviorCounter = 5;
            }
            runtime.stateClock = 15.0f / 25.0f;
        }

        const float speed = std::max(1, abs(definition->param[7])) * 25.0f / 64.0f;
        const LgldBlockInfo* oldBlock = currentLgldBlockForCell((int)runtime.x, (int)runtime.y);
        const int oldFloor = oldBlock ? oldBlock->floorHeight : 0;
        const float step = std::min(speed * dt, std::max(0.0f, distance - contactDistance));
        const float nx = runtime.x + cosf(runtime.heading) * step;
        const float ny = runtime.y + sinf(runtime.heading) * step;
        const float collisionRadius = std::max(0.18f, std::min(0.42f, (float)definition->radius / 64.0f));
        if (step > 0.0f && enemyCanMove(i, nx, ny, std::max(16, definition->height), oldFloor, collisionRadius)) {
            runtime.x = nx;
            runtime.y = ny;
            runtime.collisionAttempts = 0;
        } else if (step > 0.0f) {
            const float cross = sinf(desiredHeading - runtime.heading);
            runtime.turnDirection = cross < 0.0f ? -1 : (cross > 0.0f ? 1 : (runtime.turnDirection == 0 ? 1 : -runtime.turnDirection));
            chooseEnemyCollisionHeading(i, runtime, desiredHeading, std::max(16, definition->height),
                                        oldFloor, collisionRadius, step);
            runtime.aiState = 0;
            runtime.behaviorCounter = 5;
            runtime.stateClock = 6.0f / 25.0f;
        }
    }
}

static const GlobalObjectInfo* currentWeaponDefinition() {
    const GlobalObjectInfo* fallback = nullptr;
    for (std::map<std::string, GlobalObjectInfo>::const_iterator it = gGlobalObjectInfo.begin(); it != gGlobalObjectInfo.end(); ++it) {
        if (it->second.objectType != 4u) continue;
        if (!fallback) fallback = &it->second;
        if ((it->second.param[6] & 0xff) == gPlayerWeapon) return &it->second;
    }
    return fallback;
}

static void playerFireWeapon() {
    if (gPlayerWeapon < 0 || gPlayerWeapon >= PLAYER_WEAPON_COUNT ||
        gPlayerHealth <= 0 || !gPlayerWeapons[gPlayerWeapon]) return;
    const GlobalObjectInfo* weapon = currentWeaponDefinition();
    const int energyCost = weapon ? std::max(0, weapon->param[2]) : 1;
    if (!gGodMode && gPlayerEnergy < energyCost) {
        if (weapon && weapon->param[7] != 0) stopSoundGroup(1);
        gPickupMessage = "NO ENERGY";
        gPickupMessageUntil = nowSeconds() + 1.0;
        return;
    }
    if (!gGodMode) gPlayerEnergy -= energyCost;
    if (!gGodMode) markGameProgressDirty();
    // BufferedPlaySoundFX may replace the currently assigned Paula channel
    // when a new ordinary shot arrives.  Reproduce that restart instead of
    // suppressing the request until the old sample ends: the longer SHT1,
    // SHT4 and SHT5 samples otherwise miss autofire shots.  The flamethrower
    // is the sole looped weapon and deliberately keeps its existing voice.
    if (weapon) {
        const bool loopingWeapon = weapon->param[7] != 0;
        if (!loopingWeapon) stopSoundGroup(1);
        playSoundResource(weapon->sound[0], 0.0f, 1, loopingWeapon);
    }
    if (!weapon) return;
    const int shots = gPlayerWeapons[gPlayerWeapon] >= 2 ? 2 : 1;
    for (int shotIndex = 0; shotIndex < shots; ++shotIndex) {
        const float angle = gPlayerA + (shots == 2 ? (shotIndex == 0 ? -0.025f : 0.025f) : 0.0f);
        spawnProjectile(weapon, gPlayerX, gPlayerY, gPlayerBaseZF + weapon->param[11],
                        gPlayerX + cosf(angle) * 20.0f, gPlayerY + sinf(angle) * 20.0f,
                        gPlayerBaseZF + weapon->param[11], false, 1);
    }
}

static const LgldEdgeInfo* currentLgldEdgeByIndex(int edgeIndex) {
    const AssetInfo* ai = currentLgldAsset();
    if (!ai) return nullptr;
    if (edgeIndex < 0) edgeIndex = -edgeIndex;
    if (edgeIndex < 0 || edgeIndex >= (int)ai->lgldEdgeData.size()) return nullptr;
    return &ai->lgldEdgeData[(size_t)edgeIndex];
}

static int currentLgldBlockIndexForCell(int mx, int my) {
    short v = currentLgldCellRaw(mx, my);
    if (v < 0) v = (short)-v;
    return (int)v;
}

static int originalEdgeFaceForStep(int side, int stepX, int stepY);

static bool activateEffectList(unsigned int listIndex) {
    const AssetInfo* ai = currentLgldAsset();
    if (!ai || listIndex == 0 || listIndex > ai->lgldEffectData.size()) return false;
    if (gPermanentEffectLists.count(listIndex) != 0u) return false;
    for (size_t i = 0; i < gActiveEffects.size(); ++i) {
        if (!gActiveEffects[i].finished && gActiveEffects[i].listIndex == listIndex) {
            LOGI("effect list %u ignored because it is already active", listIndex);
            return false;
        }
    }
    const std::vector<LgldEffectCommand>& list = ai->lgldEffectData[listIndex - 1u];
    bool oneShot = false;
    std::set<unsigned int> startedDoorTriggers;
    for (size_t i = 0; i < list.size(); ++i) {
        const LgldEffectCommand& c = list[i];
        if (c.key != 0u && (c.key > 4u || !gPlayerKeys[c.key - 1u])) {
            gPickupMessage = "KEY REQUIRED";
            gPickupMessageUntil = nowSeconds() + 1.5;
            playSoundResource("FAUL");
            LOGI("effect list %u blocked: missing key %u", listIndex, c.key);
            continue;
        }
        ActiveLevelEffect active;
        active.command = c;
        active.listIndex = listIndex;
        active.remaining = (float)std::max(0, abs(c.param1));
        gActiveEffects.push_back(active);
        if (isDoorEffectType(c.type) && startedDoorTriggers.insert(c.trigger).second)
            playDoorSoundCode(0, c.trigger);
        if ((c.type >= 1u && c.type <= 4u) || c.type == 9u || c.type == 10u ||
            ((c.type == 5u || c.type == 6u || c.type == 7u || c.type == 8u || c.type == 12u) && c.param2 == 0)) {
            oneShot = true;
        }
    }
    if (oneShot) gPermanentEffectLists.insert(listIndex);
    LOGI("effect list activated list=%u commands=%u", listIndex, (unsigned int)list.size());
    return !list.empty();
}

static int runtimePlaneDelta(int blockIndex, bool ceilingPlane) {
    if (blockIndex <= 0 || blockIndex >= (int)gRuntimeBlocks.size() ||
        blockIndex >= (int)gInitialRuntimeBlocks.size()) return 0;
    const LgldBlockInfo& current = gRuntimeBlocks[(size_t)blockIndex];
    const LgldBlockInfo& initial = gInitialRuntimeBlocks[(size_t)blockIndex];
    return ceilingPlane ? current.ceilHeight - initial.ceilHeight
                        : current.floorHeight - initial.floorHeight;
}

static void mutateTriggeredBlocks(unsigned int trigger, int floorDelta, int ceilDelta, int lightDelta) {
    if (trigger == 0u) return;
    for (size_t i = 1; i < gRuntimeBlocks.size(); ++i) {
        LgldBlockInfo& b = gRuntimeBlocks[i];
        if (b.trigger != trigger && b.trigger2 != trigger) continue;
        b.floorHeight += floorDelta;
        b.ceilHeight += ceilDelta;
        b.illumination = std::max(-128, std::min(127, b.illumination + lightDelta));
    }
}

static bool playerOccupiesTrigger(unsigned int trigger) {
    const LgldBlockInfo* b = currentLgldBlockForCell((int)floorf(gPlayerX), (int)floorf(gPlayerY));
    return b && (b->trigger == trigger || b->trigger2 == trigger);
}

static void beginTeleportTransition() {
    stopSoundGroup(1);
    const double soundDuration = playTeleportSound();
    gTeleportActive = true;
    gTeleportStarted = nowSeconds();
    // Animations.asm waits 32 ticks. Also honor a longer decoded sample so
    // asynchronous Android audio can always finish before the transition.
    gTeleportCompleteAfter = std::max(32.0 / 50.0,
        soundDuration + TELEPORT_AUDIO_TAIL_SECONDS);
    gAnalogLX = gAnalogLY = gAnalogRX = gAnalogRY = 0.0f;
    gFireHeld = gFireLatch = false;
    gRunHeld = false;
    gNextAutoFireTime = 0.0;
    gFireReleaseDeadline = 0.0;
}

static void openRuntimeTerminal(int terminalNumber) {
    gRuntimeTerminalNumber = std::max(1, terminalNumber);
    gRuntimeTerminalPage = 0;
    gRuntimeTerminalSelection = 0;
    gRuntimeTerminalBackground = gFramebuffer;
    gAnalogLX = gAnalogLY = gAnalogRX = gAnalogRY = 0.0f;
    setFrontendState(FRONTEND_TERMINAL);
}

static void updateRuntimeEffects(float dt) {
    ensureRuntimeLevelState();
    const float ticks = dt * 50.0f;
    for (size_t i = 0; i < gActiveEffects.size(); ++i) {
        ActiveLevelEffect& e = gActiveEffects[i];
        if (e.finished) continue;
        const unsigned int type = e.command.type;
        if (type == 11u) {
            openRuntimeTerminal(e.command.param1);
            e.finished = true;
            continue;
        }
        if (type == 14u) {
            // Animations.asm EndLevel sets Escape. TMapMain then plays global
            // sound 3, fades the picture/music to black and advances a level.
            if (!gLevelExitActive) {
                gLevelExitActive = true;
                gLevelExitStarted = nowSeconds();
                gAnalogLX = gAnalogLY = gAnalogRX = gAnalogRY = 0.0f;
                gFireHeld = gFireLatch = false;
                gNextAutoFireTime = 0.0;
                gFireReleaseDeadline = 0.0;
                const double soundDuration = playTeleportSound();
                gLevelExitCompleteAfter = std::max(1.15,
                    soundDuration + TELEPORT_AUDIO_TAIL_SECONDS);
            }
            e.finished = true;
            continue;
        }
        if (type == 15u) {
            if (e.phase == 0) {
                beginTeleportTransition();
                e.phase = 1;
            }
            if (nowSeconds() - gTeleportStarted >= gTeleportCompleteAfter) {
                gPlayerX = (float)e.command.param1 + 0.5f;
                gPlayerY = (float)e.command.param2 + 0.5f;
                gPlayerStartChecked = false;
                syncPlayerHeightFromCurrentCell(true);
                gTeleportActive = false;
                gTeleportStarted = 0.0;
                e.finished = true;
            }
            continue;
        }
        if (type == 17u) {
            gActiveEnemyTriggers.insert(e.command.trigger);
            e.finished = true;
            continue;
        }
        if (type == 13u) {
            // Original LinkedLight: derive one absolute light value from the
            // linked door's travelled distance. Never accumulate per frame.
            const ActiveLevelEffect* controller = nullptr;
            for (size_t j = 0; j < gActiveEffects.size(); ++j) {
                if (gActiveEffects[j].command.trigger == (unsigned int)e.command.param2 &&
                    !gActiveEffects[j].finished) {
                    controller = &gActiveEffects[j];
                    break;
                }
            }
            if (controller) {
                const int total = std::max(1, abs(controller->command.param1));
                float travelled = 0.0f;
                if (controller->phase == 0) travelled = (float)total - controller->remaining;
                else if (controller->phase == 1) travelled = (float)total;
                else travelled = controller->remaining;
                travelled = std::max(0.0f, std::min((float)total, travelled));
                const int desiredDelta = -(int)floorf(travelled * (float)e.command.param1 / (float)total + 0.5f);
                mutateTriggeredBlocks(e.command.trigger, 0, 0, desiredDelta - e.appliedLightDelta);
                e.appliedLightDelta = desiredDelta;
            } else if (gPermanentEffectLists.count(e.listIndex) == 0u) {
                mutateTriggeredBlocks(e.command.trigger, 0, 0, -e.appliedLightDelta);
                e.appliedLightDelta = 0;
                e.finished = true;
            } else {
                e.finished = true; // keep the final light value for a permanent-open door
            }
            continue;
        }
        if (type == 16u) {
            e.fractional += ticks;
            const int period = e.command.param2 > 0 ? e.command.param2 : 25;
            if ((int)e.fractional >= period) {
                e.fractional -= (float)period;
                e.phase ^= 1;
                mutateTriggeredBlocks(e.command.trigger, 0, 0, e.phase ? e.command.param1 : -e.command.param1);
            }
            continue;
        }

        e.fractional += ticks;
        int pixels = (int)e.fractional;
        if (pixels <= 0) continue;
        e.fractional -= (float)pixels;
        if (e.phase == 1) {
            e.remaining -= (float)pixels;
            if (e.remaining <= 0.0f) {
                e.phase = 2;
                e.remaining = (float)abs(e.command.param1);
                if (isDoorEffectType(type)) playDoorSoundCode(0, e.command.trigger);
            }
            continue;
        }
        const int step = std::min(pixels, std::max(0, (int)ceilf(e.remaining)));
        if (step <= 0) { e.finished = true; continue; }
        int floorDelta = 0, ceilDelta = 0, lightDelta = 0;
        switch (type) {
            case 1: ceilDelta = step; break;
            case 2: floorDelta = step; break;
            case 3: ceilDelta = -step; break;
            case 4: floorDelta = -step; break;
            case 5: ceilDelta = (e.phase == 0) ? step : -step; break;
            case 6: ceilDelta = (e.phase == 0) ? step : -step; floorDelta = -ceilDelta; break;
            case 7: floorDelta = (e.phase == 0) ? step : -step; break;
            case 8: floorDelta = (e.phase == 0) ? -step : step; break;
            // The original illumination byte is a darkness-table index:
            // LightUp subtracts it, LightDown adds it.
            case 9: lightDelta = -step; break;
            case 10: lightDelta = step; break;
            case 12: floorDelta = (e.phase == 0) ? -step : step; break;
            default: e.finished = true; continue;
        }
        if ((type == 5u || type == 6u || type == 7u || type == 8u || type == 12u) && e.phase == 2 && playerOccupiesTrigger(e.command.trigger)) {
            // Never crush the player: closing doors/lifts wait until the target blocks are clear.
            continue;
        }
        mutateTriggeredBlocks(e.command.trigger, floorDelta, ceilDelta, lightDelta);
        e.remaining -= (float)step;
        if (e.remaining > 0.0f) continue;
        if (type >= 1u && type <= 4u) e.finished = true;
        else if (type == 9u || type == 10u) e.finished = true;
        else if (e.phase == 0) {
            if (isDoorEffectType(type)) playDoorSoundCode(1, e.command.trigger);
            if (e.command.param2 == 0) e.finished = true;
            else { e.phase = 1; e.remaining = (float)e.command.param2; }
        } else {
            if (isDoorEffectType(type)) playDoorSoundCode(1, e.command.trigger);
            e.finished = true;
        }
    }
    gActiveEffects.erase(std::remove_if(gActiveEffects.begin(), gActiveEffects.end(),
        [](const ActiveLevelEffect& e) { return e.finished; }), gActiveEffects.end());
}

static void activateSwitchInFront() {
    const int fromX = (int)floorf(gPlayerX);
    const int fromY = (int)floorf(gPlayerY);
    const int stepX = (fabsf(cosf(gPlayerA)) >= fabsf(sinf(gPlayerA))) ? (cosf(gPlayerA) >= 0.0f ? 1 : -1) : 0;
    const int stepY = stepX == 0 ? (sinf(gPlayerA) >= 0.0f ? 1 : -1) : 0;
    const int tx = fromX + stepX, ty = fromY + stepY;
    const LgldBlockInfo* target = currentLgldBlockForCell(tx, ty);
    if (!target) return;
    const int face = originalEdgeFaceForStep(stepX != 0 ? 0 : 1, stepX, stepY);
    if ((target->attributes & (0x10u << face)) == 0u) return;
    int edgeIndex = target->edge[face];
    if (edgeIndex < 0) edgeIndex = -edgeIndex;
    if (edgeIndex > 0) {
        // movement.asm replaces all three edge brushes with their linked ON
        // texture before it activates the associated effect list.
        for (unsigned int part = 0; part < 3u; ++part)
            gActivatedSwitchParts.insert((unsigned int)edgeIndex * 3u + part);
    }
    if (activateEffectList(target->effect)) playSoundResource("SWT1");
}

static std::string lgldTextureDebugName(int idx) {
    const AssetInfo* ai = currentLgldAsset();
    if (!ai) return std::string("-");
    int aidx = idx < 0 ? -idx : idx;
    if (aidx > 0 && aidx <= (int)ai->lgldTextureList.size()) return ai->lgldTextureList[(size_t)aidx - 1u];
    return std::string("-");
}

static void updateHeightRayProbe() {
    const AssetInfo* ai = currentLgldAsset();
    if (!ai) {
        gHeightProbeBlock = 0;
        gHeightProbeRaw = 0;
        return;
    }

    const int cx = (int)floorf(gPlayerX);
    const int cy = (int)floorf(gPlayerY);
    const short raw = currentLgldCellRaw(cx, cy);
    const int blockIndex = currentLgldBlockIndexForCell(cx, cy);
    const LgldBlockInfo* block = currentLgldBlockByIndex(blockIndex);

    gHeightProbeCellX = cx;
    gHeightProbeCellY = cy;
    gHeightProbeRaw = (int)raw;
    gHeightProbeBlock = blockIndex;
    if (block) {
        gHeightProbeFloor = block->floorHeight;
        gHeightProbeCeil = block->ceilHeight;
        gHeightProbeFloorTex = block->floorTex;
        gHeightProbeCeilTex = block->ceilTex;
        gHeightProbeIllum = block->illumination;
        gHeightProbeAttr = block->attributes;
        gHeightProbeEffect = block->effect;
        gHeightProbeTrigger = block->trigger;
        gHeightProbeTrigger2 = block->trigger2;
        for (int i = 0; i < 4; ++i) gHeightProbeEdge[i] = block->edge[i];
    } else {
        gHeightProbeFloor = 0;
        gHeightProbeCeil = 0;
        gHeightProbeFloorTex = 0;
        gHeightProbeCeilTex = 0;
        gHeightProbeIllum = 0;
        gHeightProbeAttr = 0;
        gHeightProbeEffect = 0;
        gHeightProbeTrigger = 0;
        gHeightProbeTrigger2 = 0;
        for (int i = 0; i < 4; ++i) gHeightProbeEdge[i] = 0;
    }

    static const int dx[4] = {0, 1, 0, -1};
    static const int dy[4] = {-1, 0, 1, 0};
    for (int i = 0; i < 4; ++i) {
        const int nx = cx + dx[i];
        const int ny = cy + dy[i];
        gHeightProbeNeighborRaw[i] = (int)currentLgldCellRaw(nx, ny);
        gHeightProbeNeighborBlock[i] = currentLgldBlockIndexForCell(nx, ny);
        const LgldBlockInfo* nb = currentLgldBlockByIndex(gHeightProbeNeighborBlock[i]);
        if (block && nb) {
            gHeightProbeNeighborFloorDelta[i] = nb->floorHeight - block->floorHeight;
            const int lowCeil = std::min(block->ceilHeight, nb->ceilHeight);
            gHeightProbeNeighborGap[i] = lowCeil - nb->floorHeight;
        } else {
            gHeightProbeNeighborFloorDelta[i] = 9999;
            gHeightProbeNeighborGap[i] = 0;
        }
    }

    const double t = nowSeconds();
    const bool changed = (gAssetIndex != gHeightProbeLastAsset) ||
                         (cx != gHeightProbeLastCellX) ||
                         (cy != gHeightProbeLastCellY) ||
                         (blockIndex != gHeightProbeLastBlock);
    if (changed || t - gHeightProbeLastLog > 1.5) {
        gHeightProbeLastAsset = gAssetIndex;
        gHeightProbeLastCellX = cx;
        gHeightProbeLastCellY = cy;
        gHeightProbeLastBlock = blockIndex;
        gHeightProbeLastLog = t;

        const std::string ftn = lgldTextureDebugName(gHeightProbeFloorTex);
        const std::string ctn = lgldTextureDebugName(gHeightProbeCeilTex);
        LOGI("height probe v64 column-height-anchor asset=%s cell=%d,%d raw=%d block=%d fh=%d ch=%d gap=%d ft=%d/%s ct=%d/%s illum=%d attr=%02x fx=%u tr=%u tr2=%u eye=%d maxRise=%d",
             ai->name.c_str(), cx, cy, (int)raw, blockIndex, gHeightProbeFloor, gHeightProbeCeil,
             gHeightProbeCeil - gHeightProbeFloor, gHeightProbeFloorTex, ftn.c_str(),
             gHeightProbeCeilTex, ctn.c_str(), gHeightProbeIllum, gHeightProbeAttr,
             gHeightProbeEffect, gHeightProbeTrigger, gHeightProbeTrigger2,
             ORIG_PLAYER_EYES_HEIGHT, ORIG_PLAYER_MAX_RISE);
        LOGI("height probe v64 neigh N raw=%d b=%d dF=%d gap=%d E raw=%d b=%d dF=%d gap=%d S raw=%d b=%d dF=%d gap=%d W raw=%d b=%d dF=%d gap=%d edges=%d,%d,%d,%d",
             gHeightProbeNeighborRaw[0], gHeightProbeNeighborBlock[0], gHeightProbeNeighborFloorDelta[0], gHeightProbeNeighborGap[0],
             gHeightProbeNeighborRaw[1], gHeightProbeNeighborBlock[1], gHeightProbeNeighborFloorDelta[1], gHeightProbeNeighborGap[1],
             gHeightProbeNeighborRaw[2], gHeightProbeNeighborBlock[2], gHeightProbeNeighborFloorDelta[2], gHeightProbeNeighborGap[2],
             gHeightProbeNeighborRaw[3], gHeightProbeNeighborBlock[3], gHeightProbeNeighborFloorDelta[3], gHeightProbeNeighborGap[3],
             gHeightProbeEdge[0], gHeightProbeEdge[1], gHeightProbeEdge[2], gHeightProbeEdge[3]);
    }
}


static bool isWallCell(int mx, int my);

static int currentPlayerFloorHeight() {
    const LgldBlockInfo* b = currentLgldBlockForCell((int)floorf(gPlayerX), (int)floorf(gPlayerY));
    return b ? b->floorHeight : gPlayerTargetBaseZ;
}

static int currentPlayerCeilHeight() {
    const LgldBlockInfo* b = currentLgldBlockForCell((int)floorf(gPlayerX), (int)floorf(gPlayerY));
    return b ? b->ceilHeight : gPlayerCeilZ;
}

static void syncPlayerHeightFromCurrentCell(bool forceLog) {
    const AssetInfo* ai = currentLgldAsset();
    if (!ai) {
        gPlayerTargetBaseZ = 0;
        gPlayerCeilZ = 128;
        gPlayerBaseZ = 0;
        gPlayerBaseZF = 0.0f;
        gPlayerEyeZ = ORIG_PLAYER_EYES_HEIGHT;
        return;
    }
    const int cx = (int)floorf(gPlayerX);
    const int cy = (int)floorf(gPlayerY);
    const int bi = currentLgldBlockIndexForCell(cx, cy);
    const LgldBlockInfo* b = currentLgldBlockByIndex(bi);
    if (!b) return;

    gPlayerTargetBaseZ = b->floorHeight;
    gPlayerCeilZ = b->ceilHeight;
    if (forceLog) {
        gPlayerBaseZF = (float)gPlayerTargetBaseZ;
        gPlayerVerticalSpeed = 0.0f;
        gPlayerFalling = false;
    }
    gPlayerBaseZ = (int)floorf(gPlayerBaseZF + 0.5f);
    gPlayerEyeZ = gPlayerBaseZ + ORIG_PLAYER_EYES_HEIGHT;

    const double t = nowSeconds();
    if (forceLog || cx != gPlayerLastCellX || cy != gPlayerLastCellY || bi != gPlayerLastBlockIndex || t - gPlayerHeightLastLog > 1.5) {
        gPlayerLastCellX = cx;
        gPlayerLastCellY = cy;
        gPlayerLastBlockIndex = bi;
        gPlayerHeightLastLog = t;
        LOGI("player height v64 asset=%s cell=%d,%d block=%d base=%d eye=%d ceil=%d gap=%d attr=%02x",
             ai->name.c_str(), cx, cy, bi, gPlayerBaseZ, gPlayerEyeZ, gPlayerCeilZ,
             gPlayerCeilZ - gPlayerBaseZ, b->attributes);
    }
}

static bool currentLgldCellAllowsPlayerFrom(int mx, int my, int fromFloor, int fromCeil, const char* axisLabel) {
    const short raw = currentLgldCellRaw(mx, my);
    if (raw <= 0) return false;
    const int bi = currentLgldBlockIndexForCell(mx, my);
    const LgldBlockInfo* nb = currentLgldBlockByIndex(bi);
    if (!nb) return false;

    const int delta = nb->floorHeight - fromFloor;
    if (delta > ORIG_PLAYER_MAX_RISE) {
        static double lastRiseLog = 0.0;
        const double t = nowSeconds();
        if (t - lastRiseLog > 0.5) {
            lastRiseLog = t;
            LOGI("move block v64 reason=rise axis=%s cell=%d,%d block=%d rise=%d max=%d fromFloor=%d toFloor=%d gap=%d attr=%02x fx=%u tr=%u tr2=%u",
                 axisLabel ? axisLabel : "?", mx, my, bi, delta, ORIG_PLAYER_MAX_RISE, fromFloor, nb->floorHeight,
                 nb->ceilHeight - nb->floorHeight, nb->attributes, nb->effect, nb->trigger, nb->trigger2);
        }
        return false;
    }

    int clearance = 0;
    if (delta >= 0) {
        const int lowCeil = std::min(fromCeil, nb->ceilHeight);
        clearance = lowCeil - nb->floorHeight;
    } else {
        clearance = nb->ceilHeight - fromFloor;
    }
    if (clearance <= ORIG_PLAYER_HEIGHT + 8) {
        static double lastFitLog = 0.0;
        const double t = nowSeconds();
        if (t - lastFitLog > 0.5) {
            lastFitLog = t;
            LOGI("move block v64 reason=fit axis=%s cell=%d,%d block=%d clearance=%d need>%d fromFloor=%d fromCeil=%d toFloor=%d toCeil=%d attr=%02x fx=%u tr=%u tr2=%u",
                 axisLabel ? axisLabel : "?", mx, my, bi, clearance, ORIG_PLAYER_HEIGHT + 8,
                 fromFloor, fromCeil, nb->floorHeight, nb->ceilHeight, nb->attributes, nb->effect, nb->trigger, nb->trigger2);
        }
        return false;
    }
    return true;
}

static bool canOccupyPositionV57(float x, float y, const char* axisLabel) {
    const AssetInfo* ai = currentLgldAsset();
    if (!ai) {
        const float radius = 0.18f;
        return !isWallCell((int)floorf(x + radius), (int)floorf(y)) &&
               !isWallCell((int)floorf(x - radius), (int)floorf(y)) &&
               !isWallCell((int)floorf(x), (int)floorf(y + radius)) &&
               !isWallCell((int)floorf(x), (int)floorf(y - radius));
    }

    const int fromFloor = currentPlayerFloorHeight();
    const int fromCeil = currentPlayerCeilHeight();
    const float radius = 16.0f / 64.0f;
    const int targetCenterX = (int)floorf(x);
    const int targetCenterY = (int)floorf(y);
    if (!currentLgldCellAllowsPlayerFrom(targetCenterX, targetCenterY, fromFloor, fromCeil, axisLabel)) return false;

    // A drop changes the center sector before the circular hull has completely
    // cleared the old ledge. Grandfather only that already-overlapping corner,
    // and only while it moves away from the blocking cell. This prevents the
    // post-landing lock without allowing the player to push through a wall/door.
    const float signs[2] = {-1.0f, 1.0f};
    for (int iy = 0; iy < 2; ++iy) {
        for (int ix = 0; ix < 2; ++ix) {
            const float targetCornerX = x + signs[ix] * radius;
            const float targetCornerY = y + signs[iy] * radius;
            const int targetCellX = (int)floorf(targetCornerX);
            const int targetCellY = (int)floorf(targetCornerY);
            if (currentLgldCellAllowsPlayerFrom(targetCellX, targetCellY, fromFloor, fromCeil, axisLabel)) continue;

            const float currentCornerX = gPlayerX + signs[ix] * radius;
            const float currentCornerY = gPlayerY + signs[iy] * radius;
            const int currentCellX = (int)floorf(currentCornerX);
            const int currentCellY = (int)floorf(currentCornerY);
            if (currentCellX != targetCellX || currentCellY != targetCellY) return false;

            const float centerX = (float)targetCellX + 0.5f;
            const float centerY = (float)targetCellY + 0.5f;
            const float oldDx = currentCornerX - centerX;
            const float oldDy = currentCornerY - centerY;
            const float newDx = targetCornerX - centerX;
            const float newDy = targetCornerY - centerY;
            if (newDx * newDx + newDy * newDy + 0.00001f < oldDx * oldDx + oldDy * oldDy) return false;
        }
    }
    return true;
}

static void ensureLevelTextureCache() {
    const AssetInfo* ai = currentLgldAsset();
    if (!ai || !ai->lgldParseOk) {
        gLevelTextureCache.clear();
        gLevelSwitchTextureCache.clear();
        gLevelTextureCacheAssetIndex = -99999;
        return;
    }
    if (gLevelTextureCacheAssetIndex == gAssetIndex &&
        gLevelTextureCache.size() == ai->lgldTextureList.size() + 1u &&
        gLevelSwitchTextureCache.size() == ai->lgldTextureList.size() + 1u) return;

    gLevelTextureCacheAssetIndex = gAssetIndex;
    gLevelTextureCache.assign(ai->lgldTextureList.size() + 1u, TextureBitmap());
    gLevelSwitchTextureCache.assign(ai->lgldTextureList.size() + 1u, TextureBitmap());
    const std::string fullPath = gDataPath + "/" + gFirstGldName;
    unsigned int okCount = 0;
    unsigned int animCount = 0;
    unsigned int switchCount = 0;
    for (size_t i = 0; i < ai->lgldTextureList.size(); ++i) {
        if (loadExactTextureResource(fullPath, ai->lgldTextureList[i], gLevelTextureCache[i + 1u])) {
            ++okCount;
            if (gLevelTextureCache[i + 1u].frames > 1) ++animCount;
        }
        if (loadExactSwitchTextureResource(fullPath, ai->lgldTextureList[i], gLevelSwitchTextureCache[i + 1u]))
            ++switchCount;
    }
    LOGI("level textures asset=%s names=%u loaded=%u animated=%u switches=%u first=%s",
         ai->name.c_str(), (unsigned int)ai->lgldTextureList.size(), okCount, animCount,
         switchCount, ai->lgldFirstTexture.c_str());
}

static const TextureBitmap* levelTextureByIndex(int idx) {
    if (idx < 0) idx = -idx;
    ensureLevelTextureCache();
    if (idx > 0 && idx < (int)gLevelTextureCache.size() && gLevelTextureCache[(size_t)idx].ok) return &gLevelTextureCache[(size_t)idx];
    return nullptr;
}

static const TextureBitmap* levelSwitchTextureByIndex(int idx) {
    if (idx < 0) idx = -idx;
    ensureLevelTextureCache();
    if (idx > 0 && idx < (int)gLevelSwitchTextureCache.size() &&
        gLevelSwitchTextureCache[(size_t)idx].ok) return &gLevelSwitchTextureCache[(size_t)idx];
    return nullptr;
}


static const int MAP_W = 14;
static const int MAP_H = 12;
static const char* gMap[MAP_H] = {
    "11111111111111",
    "10000010000001",
    "10111010011101",
    "10001000010001",
    "11101011110101",
    "10000010000101",
    "10111110110101",
    "10000000000101",
    "10110111110101",
    "10010000000001",
    "10000011100001",
    "11111111111111"
};

static int mapCell(int mx, int my) {
    const AssetInfo* ai = currentLgldAsset();
    if (ai) {
        const short v = currentLgldCellRaw(mx, my);
        if (v > 0) return 0;       // original LGLD positive cells are walkable map cells
        if (v < 0) return 1 + ((-v) % 8);
        return 1;                  // outside/unused map space blocks ray and collision
    }
    if (mx < 0 || my < 0 || mx >= MAP_W || my >= MAP_H) return 1;
    char c = gMap[my][mx];
    if (c < '0' || c > '9') return 0;
    return c - '0';
}

static bool isWallCell(int mx, int my) { return mapCell(mx, my) != 0; }

static void ensurePlayerInOpenCell() {
    const AssetInfo* ai = currentLgldAsset();
    if (ai && gLastLevelAssetIndex != gAssetIndex) {
        gLastLevelAssetIndex = gAssetIndex;
        gPlayerStartChecked = false;
        gLevelTextureCacheAssetIndex = -99999;
    }

    if (ai) {
        if (gPlayerStartChecked && currentLgldOpenCell((int)floorf(gPlayerX), (int)floorf(gPlayerY))) return;
        gPlayerStartChecked = true;

        ensureRuntimeLevelState();
        for (size_t i = 0; i < ai->lgldPlacedObjects.size(); ++i) {
            const LgldPlacedObject& object = ai->lgldPlacedObjects[i];
            if (object.objectCode != 0u) continue;
            const float sx = (float)object.worldX / 64.0f;
            const float sy = (float)object.worldY / 64.0f;
            if (!currentLgldOpenCell((int)floorf(sx), (int)floorf(sy))) continue;
            gPlayerX = sx;
            gPlayerY = sy;
            gPlayerA = (float)object.heading * (6.28318530718f / 2048.0f);
            syncPlayerHeightFromCurrentCell(true);
            LOGI("original player spawn asset=%s world=%u,%u cell=%d,%d heading=%u", ai->name.c_str(),
                 object.worldX, object.worldY, (int)floorf(sx), (int)floorf(sy), object.heading);
            return;
        }

        // v32: playable original-level texture preview. Use the first good walkable
        // LGLD map cell near the parsed bounds as player spawn. This is still a
        // geometry probe, not final game spawn logic.
        for (int y = std::max(0, ai->lgldMinY); y <= std::min(127, ai->lgldMaxY); ++y) {
            for (int x = std::max(0, ai->lgldMinX); x <= std::min(127, ai->lgldMaxX); ++x) {
                if (!currentLgldOpenCell(x, y)) continue;
                // Prefer a cell that has some breathing room so the first view is not inside a wall.
                if (!currentLgldOpenCell(x + 1, y) && !currentLgldOpenCell(x, y + 1)) continue;
                gPlayerX = (float)x + 0.5f;
                gPlayerY = (float)y + 0.5f;
                gPlayerA = 0.0f;
                syncPlayerHeightFromCurrentCell(true);
                LOGI("level walk v64 column-height-anchor spawn asset=%s cell=%d,%d bounds=%d,%d-%d,%d", ai->name.c_str(), x, y, ai->lgldMinX, ai->lgldMinY, ai->lgldMaxX, ai->lgldMaxY);
                return;
            }
        }
        // Fallback: any positive cell.
        for (int y = 0; y < 128; ++y) for (int x = 0; x < 128; ++x) if (currentLgldOpenCell(x, y)) {
            gPlayerX = (float)x + 0.5f;
            gPlayerY = (float)y + 0.5f;
            gPlayerA = 0.0f;
            LOGI("level walk v64 fallback spawn asset=%s cell=%d,%d", ai->name.c_str(), x, y);
            return;
        }
        gPlayerX = 1.5f; gPlayerY = 1.5f; gPlayerA = 0.0f;
        syncPlayerHeightFromCurrentCell(true);
        return;
    }

    if (gPlayerStartChecked) return;
    gPlayerStartChecked = true;
    if (!isWallCell((int)floorf(gPlayerX), (int)floorf(gPlayerY))) return;

    static const float starts[][2] = {
        {1.5f, 1.5f}, {2.5f, 1.5f}, {3.5f, 1.5f}, {1.5f, 3.5f}, {4.5f, 3.5f}, {8.5f, 3.5f}
    };
    for (size_t i = 0; i < sizeof(starts) / sizeof(starts[0]); ++i) {
        if (!isWallCell((int)floorf(starts[i][0]), (int)floorf(starts[i][1]))) {
            gPlayerX = starts[i][0];
            gPlayerY = starts[i][1];
            LOGI("player start corrected to open cell %.2f %.2f", gPlayerX, gPlayerY);
            return;
        }
    }
}

static void updatePlayerMotion() {
    ensurePlayerInOpenCell();
    const double t = nowSeconds();
    if (gMoveLastTime <= 0.0) { gMoveLastTime = t; return; }
    double dt = t - gMoveLastTime; gMoveLastTime = t;
    if (dt < 0.0) dt = 0.0;
    if (dt > 0.05) dt = 0.05;
    if (gFireReleaseDeadline > 0.0 && t >= gFireReleaseDeadline) {
        gFireHeld = false;
        gFireLatch = false;
        stopLoopingSoundGroup(1);
        gNextAutoFireTime = 0.0;
        gFireReleaseDeadline = 0.0;
    }
    updateRuntimeEffects((float)dt);
    if (gFrontendState != FRONTEND_GAME || gLevelExitActive || gTeleportActive) {
        gPlayerBobPhase = 0.0f;
        gPlayerBobOffset = 0.0f;
        return;
    }
    if (!gPlayerDead && gFireHeld && t >= gNextAutoFireTime) {
        playerFireWeapon();
        gNextAutoFireTime = t + AUTO_FIRE_INTERVAL;
    }
    const int oldCellX = (int)floorf(gPlayerX), oldCellY = (int)floorf(gPlayerY);
    const int oldFloor = currentPlayerFloorHeight();
    float turn = gPlayerDead ? 0.0f : gAnalogRX; if (fabsf(turn) < 0.10f) turn = 0.0f;
    gPlayerA += turn * 2.35f * (float)dt;
    float fwd = gPlayerDead ? 0.0f : -gAnalogLY;
    float str = gPlayerDead ? 0.0f : gAnalogLX;
    if (fabsf(fwd) < 0.12f) fwd = 0.0f;
    if (fabsf(str) < 0.12f) str = 0.0f;
    const float movementAmount = std::min(1.0f, sqrtf(fwd * fwd + str * str));
    if (!gPlayerDead && !gPlayerFalling && movementAmount > 0.0f) {
        static const signed char bobWave[64] = {
             0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 6, 7,
             7, 7, 7, 6, 6, 6, 5, 5, 4, 4, 3, 3, 2, 2, 1, 1,
             0, 0,-1,-1,-2,-2,-3,-3,-4,-4,-5,-5,-6,-6,-6,-7,
            -7,-7,-7,-6,-6,-6,-5,-5,-4,-4,-3,-3,-2,-2,-1,-1
        };
        // OscSpeedTrans[64] is 40 in movement.asm; OscCont is sampled after
        // shifting four bits, so normal walking advances 125 wave samples/s.
        const float phaseStepsPerSecond = 125.0f * movementAmount;
        gPlayerBobPhase = fmodf(gPlayerBobPhase + phaseStepsPerSecond * (float)dt, 64.0f);
        // OscillationAmp's speed-64 row maps the seven-pixel source wave to
        // five vertical pixels at normal walking speed.
        gPlayerBobOffset = (float)bobWave[(int)gPlayerBobPhase & 63] * (5.0f / 7.0f) * movementAmount;
    } else {
        gPlayerBobPhase = 0.0f;
        gPlayerBobOffset = 0.0f;
    }
    const float ca = cosf(gPlayerA), sa = sinf(gPlayerA);
    const float runScale = gRunHeld ? 1.5f : 1.0f;
    float nx = gPlayerX + (ca * fwd * 1.95f + -sa * str * 1.35f) * runScale * (float)dt;
    float ny = gPlayerY + (sa * fwd * 1.95f +  ca * str * 1.35f) * runScale * (float)dt;

    // v63: height-aware movement plus visible drop diagnostics. Positive LGLD cells are no
    // longer enough; the target floor must be climbable, very deep blind
    // drops are blocked until the renderer shows them, and the resulting
    // floor/ceiling gap must fit the player.
    if (canOccupyPositionV57(nx, gPlayerY, "X")) gPlayerX = nx;
    if (canOccupyPositionV57(gPlayerX, ny, "Y")) gPlayerY = ny;
    syncPlayerHeightFromCurrentCell(false);
    const int newCellX = (int)floorf(gPlayerX), newCellY = (int)floorf(gPlayerY);
    const bool changedCell = newCellX != oldCellX || newCellY != oldCellY;
    const int newFloor = currentPlayerFloorHeight();
    if (changedCell && oldFloor - newFloor >= 24 && !gPlayerFalling) {
        gPlayerFalling = true;
        gPlayerFallStartZ = oldFloor;
        gPlayerBaseZF = (float)oldFloor;
        gPlayerVerticalSpeed = 0.0f;
    }
    if (gPlayerFalling) {
        gPlayerVerticalSpeed = std::min(500.0f, gPlayerVerticalSpeed + 1250.0f * (float)dt);
        gPlayerBaseZF -= gPlayerVerticalSpeed * (float)dt;
        if (gPlayerBaseZF <= (float)newFloor) {
            gPlayerBaseZF = (float)newFloor;
            gPlayerFalling = false;
            const int drop = gPlayerFallStartZ - newFloor;
            if (drop > 256) damagePlayer(4 * (drop >> 7));
        }
    } else {
        gPlayerBaseZF = (float)newFloor;
        gPlayerVerticalSpeed = 0.0f;
    }
    gPlayerBaseZ = (int)floorf(gPlayerBaseZF + 0.5f);
    if (gPlayerDead) {
        gPlayerDeathTickAccumulator += (float)dt * 50.0f;
        int deathTicks = (int)gPlayerDeathTickAccumulator;
        gPlayerDeathTickAccumulator -= (float)deathTicks;
        while (deathTicks-- > 0 && !gPlayerFalling) {
            if (gPlayerDeathEyeHeight > 12) gPlayerDeathEyeHeight = std::max(12, gPlayerDeathEyeHeight - 2);
            else if (gPlayerDeathWaitTicks > 0) --gPlayerDeathWaitTicks;
        }
        gPlayerEyeZ = gPlayerBaseZ + gPlayerDeathEyeHeight;
        if (!gPlayerFalling && gPlayerDeathEyeHeight <= 12 && gPlayerDeathWaitTicks <= 0) {
            --gPlayerRetries;
            gRestoreLevelCheckpoint = true;
            gRuntimeAssetIndex = -99999;
            gPlayerStartChecked = false;
            gPlayerDead = false;
            gAnalogLX = gAnalogLY = gAnalogRX = gAnalogRY = 0.0f;
            if (gPlayerRetries > 0) setFrontendState(FRONTEND_LOADING);
            else {
                gPlayerRetries = 3;
                gPlayerHealth = gCheckpointHealth;
                gPlayerShields = gCheckpointShields;
                gPlayerEnergy = gCheckpointEnergy;
                gPlayerCredits = gCheckpointCredits;
                gPlayerScore = gCheckpointScore;
                gPlayerWeapon = gCheckpointWeapon;
                memcpy(gPlayerWeapons, gCheckpointWeapons, sizeof(gPlayerWeapons));
                memcpy(gPlayerKeys, gCheckpointKeys, sizeof(gPlayerKeys));
                gRestoreLevelCheckpoint = false;
                markGameProgressDirty();
                saveGameProgress();
                setFrontendState(FRONTEND_TITLE);
            }
            return;
        }
    } else {
        gPlayerEyeZ = gPlayerBaseZ + ORIG_PLAYER_EYES_HEIGHT +
            (int)floorf(gPlayerBobOffset + (gPlayerBobOffset >= 0.0f ? 0.5f : -0.5f));
    }

    if (!gPlayerDead && changedCell) {
        const LgldBlockInfo* entered = currentLgldBlockForCell(newCellX, newCellY);
        if (entered && entered->effect != 0u && (entered->attributes & 0xf0u) == 0u) activateEffectList(entered->effect);
    }
    const LgldBlockInfo* standing = currentLgldBlockForCell(newCellX, newCellY);
    if (!gPlayerDead && standing && !gPlayerFalling && (standing->attributes & 3u) != 0u) {
        gHazardClock += dt;
        if (gHazardClock >= 1.0) {
            gHazardClock -= 1.0;
            static const int damage[4] = {0, 2, 5, 10};
            damagePlayer(damage[standing->attributes & 3u]);
        }
    } else gHazardClock = 0.0;
}

static const TextureBitmap* textureForWallHit(int mapX, int mapY, int side, int stepX, int stepY, int& outTexIndex) {
    outTexIndex = 0;
    const LgldBlockInfo* block = currentLgldBlockForCell(mapX, mapY);
    if (block) {
        int face = 0;
        if (side == 0) face = (stepX > 0) ? 0 : 1;     // west/east
        else face = (stepY > 0) ? 2 : 3;               // north/south
        const LgldEdgeInfo* edge = currentLgldEdgeByIndex(block->edge[face]);
        if (edge) {
            int tex = edge->normTex;
            if (tex <= 0 && edge->upTex > 0) tex = edge->upTex;
            if (tex <= 0 && edge->lowTex > 0) tex = edge->lowTex;
            outTexIndex = tex;
            const TextureBitmap* t = levelTextureByIndex(tex);
            if (t) return t;
        }
    }

    // Fallback for non-LGLD test maps or not-yet-decoded edge cases.
    if (gWallTexCount <= 0) return nullptr;
    int wallType = mapCell(mapX, mapY);
    int idx = (wallType - 1) % gWallTexCount;
    if (idx < 0) idx = 0;
    return &gWallTex[idx];
}

static const TextureBitmap* floorTextureForCell(int mx, int my, bool ceiling, bool& skyCeil, int& outTexIndex) {
    skyCeil = false;
    outTexIndex = 0;
    const LgldBlockInfo* block = currentLgldBlockForCell(mx, my);
    if (block) {
        int tex = ceiling ? block->ceilTex : block->floorTex;
        if (ceiling && tex < 0) { skyCeil = true; tex = -tex; }
        outTexIndex = tex;
        const TextureBitmap* t = levelTextureByIndex(tex);
        if (t) return t;
    }

    if (gFloorTexCount <= 0) return nullptr;
    const short v = currentLgldCellRaw(mx, my);
    int idx = 0;
    if (v > 0) idx = v % gFloorTexCount;
    else idx = ((mx / 2) + (my / 2)) % gFloorTexCount;
    if (idx < 0) idx = 0;
    return &gFloorTex[idx];
}


static int lgldBlockIndexForCellRaw(int mx, int my) {
    int v = currentLgldCellRaw(mx, my);
    if (v < 0) v = -v;
    return v;
}

static int originalEdgeFaceForStep(int side, int stepX, int stepY) {
    // TMap.i: Edge1=east/right, Edge2=south/bottom, Edge3=west/left, Edge4=north/top.
    // 3d.asm RayCastX/RayCastZ stores the edge of the block being entered:
    // X+ => Edge3, X- => Edge1, Y/Z+ => Edge4, Y/Z- => Edge2.
    if (side == 0) return (stepX > 0) ? 2 : 0;
    return (stepY > 0) ? 3 : 1;
}

static int edgeIndexFromBlockFace(int blockIndex, int edgeFace) {
    const AssetInfo* ai = currentLgldAsset();
    if (!ai || blockIndex <= 0 || blockIndex >= (int)ai->lgldBlockData.size() || edgeFace < 0 || edgeFace > 3) return 0;
    const LgldBlockInfo* block = currentLgldBlockByIndex(blockIndex);
    if (!block) return 0;
    int e = block->edge[edgeFace];
    return e < 0 ? -e : e;
}

static void buildOriginalStyleVTableForRay(float rayDirX, float rayDirY, std::vector<OrigVtHit>& outHits) {
    outHits.clear();
    const AssetInfo* ai = currentLgldAsset();
    if (!ai) return;

    int mapX = (int)floorf(gPlayerX);
    int mapY = (int)floorf(gPlayerY);
    int lastBlock = lgldBlockIndexForCellRaw(mapX, mapY);
    const float deltaDistX = (fabsf(rayDirX) < 0.0001f) ? 1.0e30f : fabsf(1.0f / rayDirX);
    const float deltaDistY = (fabsf(rayDirY) < 0.0001f) ? 1.0e30f : fabsf(1.0f / rayDirY);
    float sideDistX = 0.0f, sideDistY = 0.0f;
    int stepX = 0, stepY = 0;
    if (rayDirX < 0.0f) { stepX = -1; sideDistX = (gPlayerX - (float)mapX) * deltaDistX; }
    else { stepX = 1; sideDistX = ((float)mapX + 1.0f - gPlayerX) * deltaDistX; }
    if (rayDirY < 0.0f) { stepY = -1; sideDistY = (gPlayerY - (float)mapY) * deltaDistY; }
    else { stepY = 1; sideDistY = ((float)mapY + 1.0f - gPlayerY) * deltaDistY; }

    for (int i = 0; i < 64 && (int)outHits.size() < 32; ++i) {
        int side = 0;
        float dist = 0.0f;
        if (sideDistX < sideDistY) { dist = sideDistX; sideDistX += deltaDistX; mapX += stepX; side = 0; }
        else { dist = sideDistY; sideDistY += deltaDistY; mapY += stepY; side = 1; }

        short raw = currentLgldCellRaw(mapX, mapY);
        int blockIndex = raw < 0 ? -raw : raw;
        if (blockIndex <= 0 || blockIndex >= (int)ai->lgldBlockData.size()) {
            OrigVtHit h;
            h.distance = dist;
            h.mapX = mapX;
            h.mapY = mapY;
            h.blockIndex = 0;
            h.side = side;
            h.stepX = stepX;
            h.stepY = stepY;
            h.edgeFace = originalEdgeFaceForStep(side, stepX, stepY);
            h.stopWall = true;
            outHits.push_back(h);
            break;
        }

        if (raw > 0 && blockIndex == lastBlock) continue;
        lastBlock = blockIndex;

        OrigVtHit h;
        h.distance = dist;
        h.mapX = mapX;
        h.mapY = mapY;
        h.blockIndex = blockIndex;
        h.side = side;
        h.stepX = stepX;
        h.stepY = stepY;
        h.edgeFace = originalEdgeFaceForStep(side, stepX, stepY);
        h.edgeIndex = edgeIndexFromBlockFace(blockIndex, h.edgeFace);
        const float hitX = gPlayerX + rayDirX * dist;
        const float hitY = gPlayerY + rayDirY * dist;
        float frac = (side == 0) ? (hitY - floorf(hitY)) : (hitX - floorf(hitX));
        if (frac < 0.0f) frac += 1.0f;
        h.brushOffset = std::max(0, std::min(63, (int)floorf(frac * 64.0f)));
        // 3d.asm mirrors Edge2 and Edge3 so opposite faces retain the same
        // left-to-right texture orientation. Without this, 64px door motifs
        // wrap at their centre and appear as two swapped halves.
        if (h.edgeFace == 1 || h.edgeFace == 2) h.brushOffset = 63 - h.brushOffset;
        h.stopWall = (raw < 0);
        outHits.push_back(h);
        if (raw < 0) break;
    }
}


static int clampScreenY(int y) {
    if (y < 0) return 0;
    if (y >= FB_H) return FB_H - 1;
    return y;
}

static int originalPlayerEyeHeight() {
    return gPlayerEyeZ;
}

static int projectOriginalScreenY(int worldHeight, float dist) {
    if (dist < 0.08f) dist = 0.08f;
    const int eye = originalPlayerEyeHeight();
    // This is an intentionally conservative C++ approximation of the DrawScreen.asm
    // height projection. It is used only to build/log OTable-like spans and does not
    // alter the visible renderer yet.
    const float scale = 0.80f;
    const float y = (float)FB_H * 0.5f + ((float)(eye - worldHeight) * scale) / dist;
    return clampScreenY((int)floorf(y + 0.5f));
}

static void addOrigSpanPixels(int x, int y0, int y1, bool floorSpan, int blockIndex, bool visibleSurface) {
    if (x < 0 || x >= FB_W) return;
    if (y0 > y1) std::swap(y0, y1);
    if (y1 < 0 || y0 >= FB_H) return;
    y0 = clampScreenY(y0);
    y1 = clampScreenY(y1);
    const int n = y1 - y0 + 1;
    if (n <= 0) return;
    if (floorSpan) {
        gOrigSpanFloorSegments++;
        gOrigSpanFloorPixels += n;
    } else {
        gOrigSpanCeilSegments++;
        gOrigSpanCeilPixels += n;
    }
    OrigSpan sp;
    sp.x = x;
    sp.y0 = y0;
    sp.y1 = y1;
    sp.blockIndex = blockIndex;
    sp.floorSpan = floorSpan;
    sp.visibleSurface = visibleSurface;
    if (visibleSurface) gOrigSpans.push_back(sp);
}

static void updateOriginalSpanPrepProbe() {
    const AssetInfo* ai = currentLgldAsset();
    if (!ai) return;
    const int playerCellX = (int)floorf(gPlayerX);
    const int playerCellY = (int)floorf(gPlayerY);
    const int playerBlock = lgldBlockIndexForCellRaw(playerCellX, playerCellY);
    const double now = nowSeconds();

    // v48: OTable/row-span data is still rebuilt every frame, but only
    // transition spans are allowed to become visible. The stable base pass
    // keeps the full floor/ceiling background anchored.
    gOrigSpanLastAsset = gAssetIndex;
    gOrigSpanLastBlock = playerBlock;
    gOrigSpanLastBuild = now;

    gOrigSpans.clear();
    gOrigSpans.reserve(4096);
    gOrigSpanColumns = FB_W;
    gOrigSpanCeilSegments = 0;
    gOrigSpanFloorSegments = 0;
    gOrigSpanUpperChanges = 0;
    gOrigSpanLowerChanges = 0;
    gOrigSpanMaxHits = 0;
    gOrigSpanClosedColumns = 0;
    gOrigSpanCeilPixels = 0;
    gOrigSpanFloorPixels = 0;

    const float ca = cosf(gPlayerA), sa = sinf(gPlayerA);
    const float planeScale = cameraPlaneScale();
    const float planeX = -sa * planeScale, planeY = ca * planeScale;
    std::vector<OrigVtHit> hits;
    hits.reserve(32);

    for (int x = 0; x < FB_W; ++x) {
        const float cameraX = 2.0f * (float)x / (float)FB_W - 1.0f;
        const float rayDirX = ca + planeX * cameraX;
        const float rayDirY = sa + planeY * cameraX;
        buildOriginalStyleVTableForRay(rayDirX, rayDirY, hits);
        gOrigSpanMaxHits = std::max(gOrigSpanMaxHits, (int)hits.size());

        int prevBlock = playerBlock;
        const LgldBlockInfo* prev = currentLgldBlockByIndex(prevBlock);
        int clipTop = -1;
        int clipBottom = FB_H;

        for (size_t i = 0; i < hits.size() && clipTop + 1 < clipBottom; ++i) {
            const OrigVtHit& h = hits[i];
            const LgldBlockInfo* cur = currentLgldBlockByIndex(h.blockIndex);
            if (!prev || !cur) { gOrigSpanClosedColumns++; break; }

            const bool ceilChanged = (prev->ceilHeight != cur->ceilHeight) ||
                                     (prev->ceilTex != cur->ceilTex) ||
                                     (prev->illumination != cur->illumination);
            if (ceilChanged) {
                gOrigSpanUpperChanges++;
                int yPrev = projectOriginalScreenY(prev->ceilHeight, h.distance);
                int yCur  = projectOriginalScreenY(cur->ceilHeight,  h.distance);
                int yTop = std::min(yPrev, yCur);
                int yBottom = std::max(yPrev, yCur);
                if (yTop > clipTop + 1) addOrigSpanPixels(x, clipTop + 1, yTop, false, prevBlock, true);
                if (yBottom > clipTop) clipTop = std::min(yBottom, clipBottom - 1);
            }

            const bool floorChanged = (prev->floorHeight != cur->floorHeight) ||
                                      (prev->floorTex != cur->floorTex) ||
                                      (prev->illumination != cur->illumination);
            if (floorChanged && clipTop + 1 < clipBottom) {
                gOrigSpanLowerChanges++;
                int yPrev = projectOriginalScreenY(prev->floorHeight, h.distance);
                int yCur  = projectOriginalScreenY(cur->floorHeight,  h.distance);
                int yTop = std::min(yPrev, yCur);
                int yBottom = std::max(yPrev, yCur);
                if (yBottom < clipBottom - 1) addOrigSpanPixels(x, yBottom, clipBottom - 1, true, prevBlock, true);
                if (yTop < clipBottom) clipBottom = std::max(yTop, clipTop + 1);
            }

            prevBlock = h.blockIndex;
            prev = cur;
            if (h.stopWall) { gOrigSpanClosedColumns++; break; }
        }

        if (prev && clipTop + 1 < clipBottom) {
            addOrigSpanPixels(x, clipTop + 1, (FB_H / 2) - 1, false, prevBlock, false);
            addOrigSpanPixels(x, FB_H / 2, clipBottom - 1, true, prevBlock, false);
        }
    }

    if (now - gOrigSpanLastLog > 1.5 || gOrigSpanLastAsset != gAssetIndex) {
        gOrigSpanLastLog = now;
        LOGI("orig otable v64 asset=%s cell=%d,%d block=%d cols=%d maxhits=%d upper=%d lower=%d ceilSpans=%d floorSpans=%d ceilPx=%d floorPx=%d closed=%d",
             ai->name.c_str(), playerCellX, playerCellY, playerBlock, gOrigSpanColumns, gOrigSpanMaxHits,
             gOrigSpanUpperChanges, gOrigSpanLowerChanges, gOrigSpanCeilSegments, gOrigSpanFloorSegments,
             gOrigSpanCeilPixels, gOrigSpanFloorPixels, gOrigSpanClosedColumns);
    }
}

static void updateOriginalVTableProbe() {
    const AssetInfo* ai = currentLgldAsset();
    if (!ai) {
        gOrigCenterVTable.clear();
        gOrigCenterBlock = gOrigCenterFloor = gOrigCenterCeil = gOrigCenterIllum = gOrigCenterAttr = 0;
        return;
    }
    const int playerCellX = (int)floorf(gPlayerX);
    const int playerCellY = (int)floorf(gPlayerY);
    gOrigCenterBlock = lgldBlockIndexForCellRaw(playerCellX, playerCellY);
    const LgldBlockInfo* pb = currentLgldBlockForCell(playerCellX, playerCellY);
    if (pb) {
        gOrigCenterFloor = pb->floorHeight;
        gOrigCenterCeil = pb->ceilHeight;
        gOrigCenterIllum = pb->illumination;
        gOrigCenterAttr = (int)pb->attributes;
    }
    const float ca = cosf(gPlayerA), sa = sinf(gPlayerA);
    buildOriginalStyleVTableForRay(ca, sa, gOrigCenterVTable);

    const double now = nowSeconds();
    if (gOrigProbeLastAsset != gAssetIndex || gOrigProbeLastBlock != gOrigCenterBlock || now - gOrigProbeLastLog > 5.0) {
        gOrigProbeLastAsset = gAssetIndex;
        gOrigProbeLastBlock = gOrigCenterBlock;
        gOrigProbeLastLog = now;
        char chain[512];
        chain[0] = 0;
        size_t pos = 0;
        const int n = std::min(6, (int)gOrigCenterVTable.size());
        for (int i = 0; i < n; ++i) {
            const OrigVtHit& h = gOrigCenterVTable[(size_t)i];
            int wrote = snprintf(chain + pos, sizeof(chain) - pos, "%s%d%s@%.2f/e%d/o%d", i ? " " : "", h.blockIndex, h.stopWall ? "!" : "", h.distance, h.edgeIndex, h.brushOffset);
            if (wrote < 0) break;
            pos += (size_t)std::min(wrote, (int)(sizeof(chain) - pos - 1));
            if (pos >= sizeof(chain) - 1) break;
        }
        LOGI("orig vtable v64 asset=%s playerCell=%d,%d block=%d fh=%d ch=%d il=%d attr=%d hits=%u chain=%s",
             ai->name.c_str(), playerCellX, playerCellY, gOrigCenterBlock, gOrigCenterFloor, gOrigCenterCeil,
             gOrigCenterIllum, gOrigCenterAttr, (unsigned int)gOrigCenterVTable.size(), chain);
    }
}

static void drawSkyBackground() {
    // v33: Breathless outdoor levels use a wider panoramic sky feel.
    // v32 rotated the sky too slowly in open WLD1 areas, especially around BLES0008/map8.
    // Keep this isolated from wall/floor texture binding so only sky pan behaviour changes.
    for (int y = 0; y < VIEW_CENTER_Y; ++y) for (int x = 0; x < FB_W; ++x) {
        if (gSkyTex.ok) {
            const int sx = (int)((x * (int)gSkyTex.width) / FB_W + (int)(gPlayerA * SKY_SCROLL_SCALE_V33));
            int sy = (int)((y * (int)gSkyTex.height * SKY_VERTICAL_SCALE_V33) / VIEW_CENTER_Y);
            if (sy < 0) sy = 0;
            if (sy >= (int)gSkyTex.height) sy = (int)gSkyTex.height - 1;
            gFramebuffer[y * FB_W + x] = paletteColor(sampleTextureBitmap(gSkyTex, sx, sy));
        } else {
            int v = 72 + (y * 64) / VIEW_CENTER_Y;
            gFramebuffer[y * FB_W + x] = 0xff000000u | ((unsigned)v << 16) | ((unsigned)(v / 2) << 8) | (unsigned)(v / 3);
        }
    }
}

static void drawFloorAndCeiling() {
    // v54: wall-projection-matched world plane.
    // The floor/ceiling texture must be anchored in map/world space, but its
    // projection must match the wall renderer. v52 used absolute UVs but a
    // 0.42*screen-height floor constant, while wall height uses FB_H/dist. That
    // scale mismatch makes the surface glide subtly against player movement.
    // Use the canonical same-camera plane distance: (FB_H/2)/(screenY-FB_H/2).
    const float ca = cosf(gPlayerA), sa = sinf(gPlayerA);
    const float planeScale = cameraPlaneScale();
    const float planeX = -sa * planeScale, planeY = ca * planeScale;
    const float rayDirX0 = ca - planeX;
    const float rayDirY0 = sa - planeY;
    const float rayDirX1 = ca + planeX;
    const float rayDirY1 = sa + planeY;
    const double pulseTime = nowSeconds();

    for (int y = FB_H / 2; y < FB_H; ++y) {
        const float p = (float)y - (float)FB_H * 0.5f;
        const float rowDist = (p <= 0.0f) ? 1.0f : ((float)FB_H * 0.50f / p);
        const float stepX = rowDist * (rayDirX1 - rayDirX0) / (float)FB_W;
        const float stepY = rowDist * (rayDirY1 - rayDirY0) / (float)FB_W;
        float worldX = gPlayerX + rowDist * rayDirX0;
        float worldY = gPlayerY + rowDist * rayDirY0;

        int floorLight = 196 - (int)(rowDist * 18.0f);
        if (floorLight < 48) floorLight = 48;
        if (floorLight > 224) floorLight = 224;
        int ceilLight = floorLight - 60;
        if (ceilLight < 36) ceilLight = 36;
        if (ceilLight > 144) ceilLight = 144;

        const int cy = FB_H - 1 - y;
        for (int x = 0; x < FB_W; ++x) {
            const int cellX = (int)floorf(worldX);
            const int cellY = (int)floorf(worldY);
            const float texX = worldX;
            const float texY = worldY;

            bool floorSky = false;
            int floorTexIndex = 0;
            const TextureBitmap* ft = floorTextureForCell(cellX, cellY, false, floorSky, floorTexIndex);
            unsigned char idx = 32;
            if (ft && ft->ok) {
                idx = sampleTextureBitmap(*ft,
                                          (int)floorf(texX * (float)ft->width),
                                          (int)floorf(texY * (float)ft->height));
            } else {
                idx = (unsigned char)(32 + (((int)floorf(texX * 4.0f) ^ (int)floorf(texY * 4.0f)) & 15));
            }

            unsigned int floorCol = shadeColor(paletteColor(idx), floorLight);
            const LgldBlockInfo* floorBlock = currentLgldBlockForCell(cellX, cellY);
            if (floorBlock && ((floorBlock->attributes & 3u) != 0u)) {
                const int pulse = 32 + (int)(24.0f * sinf((float)pulseTime * 5.5f + (float)(cellX + cellY) * 0.37f));
                unsigned int r = floorCol & 0xffu;
                unsigned int g = (floorCol >> 8) & 0xffu;
                unsigned int b = (floorCol >> 16) & 0xffu;
                r = (unsigned int)std::min(255, (int)r + pulse);
                g = (unsigned int)std::max(0, (int)g - pulse / 3);
                b = (unsigned int)std::max(0, (int)b - pulse / 2);
                floorCol = 0xff000000u | (b << 16) | (g << 8) | r;
            }
            gFramebuffer[y * FB_W + x] = floorCol;

            if (cy >= 0 && cy < FB_H / 2) {
                bool skyCeil = false;
                int ceilTexIndex = 0;
                const TextureBitmap* ct = floorTextureForCell(cellX, cellY, true, skyCeil, ceilTexIndex);
                if (!skyCeil) {
                    unsigned char cidx = idx;
                    if (ct && ct->ok) {
                        cidx = sampleTextureBitmap(*ct,
                                                   (int)floorf(texX * (float)ct->width),
                                                   (int)floorf(texY * (float)ct->height));
                    }
                    unsigned int cc = shadeColor(paletteColor(cidx), ceilLight);
                    unsigned int r = (cc & 0xffu) * 3u / 4u;
                    unsigned int g = ((cc >> 8) & 0xffu) * 3u / 4u;
                    unsigned int b = ((cc >> 16) & 0xffu) * 5u / 8u;
                    gFramebuffer[cy * FB_W + x] = 0xff000000u | (b << 16) | (g << 8) | r;
                }
            }

            worldX += stepX;
            worldY += stepY;
        }
    }
}


static void __attribute__((unused)) drawOriginalSpanSurfaces() {
    // v48: disabled visible portal/height-delta spans for now. The final full floor/ceiling fill
    // remains handled by the stable v34/v39 surface path so the floor does not swim
    // and outdoor sky ceilings are not overwritten by generic ceiling textures.
    const AssetInfo* ai = currentLgldAsset();
    if (!ai || gOrigSpans.empty()) return;

    const float ca = cosf(gPlayerA), sa = sinf(gPlayerA);
    const float planeScale = cameraPlaneScale();
    const float planeX = -sa * planeScale, planeY = ca * planeScale;
    const int eye = originalPlayerEyeHeight();

    for (size_t si = 0; si < gOrigSpans.size(); ++si) {
        const OrigSpan& sp = gOrigSpans[si];
        if (!sp.visibleSurface) continue;
        const LgldBlockInfo* block = currentLgldBlockByIndex(sp.blockIndex);
        if (!block) continue;

        bool skyCeil = false;
        int texIndex = 0;
        int tex = sp.floorSpan ? block->floorTex : block->ceilTex;
        if (!sp.floorSpan && tex < 0) { skyCeil = true; tex = -tex; }
        if (skyCeil) continue;
        texIndex = tex;
        const TextureBitmap* tb = levelTextureByIndex(texIndex);
        if ((!tb || !tb->ok) && !sp.floorSpan) {
            if (skyCeil) continue;
        }
        if (!tb || !tb->ok) tb = (gFloorTexCount > 0) ? &gFloorTex[0] : nullptr;
        if (!tb || !tb->ok) continue;

        const float cameraX = 2.0f * (float)sp.x / (float)FB_W - 1.0f;
        const float rayDirX = ca + planeX * cameraX;
        const float rayDirY = sa + planeY * cameraX;
        const int worldHeight = sp.floorSpan ? block->floorHeight : block->ceilHeight;
        int baseLight = 196 - ((block->illumination >> 4) & 63);
        if (baseLight < 40) baseLight = 40;
        if (!sp.floorSpan) baseLight -= 45;

        for (int y = sp.y0; y <= sp.y1; ++y) {
            const float denom = (float)y - (float)FB_H * 0.5f;
            if (fabsf(denom) < 0.75f) continue;
            const float dist = ((float)(eye - worldHeight) * 0.80f) / denom;
            if (dist <= 0.03f || dist > 64.0f) continue;
            const float wx = gPlayerX + rayDirX * dist;
            const float wy = gPlayerY + rayDirY * dist;
            unsigned char idx = sampleTextureBitmap(*tb, (int)floorf(wx * (float)tb->width), (int)floorf(wy * (float)tb->height));
            int light = baseLight - (int)(dist * 5.0f);
            if (light < 36) light = 36;
            unsigned int col = shadeColor(paletteColor(idx), light);
            if (sp.floorSpan && ((block->attributes & 3u) != 0u)) {
                const int pulse = 26 + (int)(20.0f * sinf((float)nowSeconds() * 5.5f + wx * 0.12f + wy * 0.19f));
                unsigned int r = col & 0xffu;
                unsigned int g = (col >> 8) & 0xffu;
                unsigned int b = (col >> 16) & 0xffu;
                r = (unsigned int)std::min(255, (int)r + pulse);
                g = (unsigned int)std::max(0, (int)g - pulse / 3);
                b = (unsigned int)std::max(0, (int)b - pulse / 2);
                col = 0xff000000u | (b << 16) | (g << 8) | r;
            }
            gFramebuffer[y * FB_W + sp.x] = col;
        }
    }
}




static int projectColumnHeightYV64(int worldZ, float dist) {
    if (dist < 0.08f) dist = 0.08f;
    // v64: one original sector height span (normally 128 units) corresponds to the
    // same projected height as the old full wall. This anchors walls to sector
    // floor/ceil heights instead of screen-centering every wall slice.
    const float scale = (float)FB_H / (ORIGINAL_VERTICAL_REFERENCE * dist);
    const float y = (float)FB_H * 0.5f - ((float)(worldZ - gPlayerEyeZ) * scale);
    return clampScreenY((int)floorf(y + 0.5f));
}

static void drawTexturedColumnSegmentV64(int x, int yTop, int yBottom, const TextureBitmap* wt,
                                         int texX, float dist, int side, bool lavaTint) {
    if (!wt || !wt->ok || x < 0 || x >= FB_W) return;
    if (yTop > yBottom) std::swap(yTop, yBottom);
    if (yBottom < 0 || yTop >= FB_H) return;
    yTop = clampScreenY(yTop);
    yBottom = clampScreenY(yBottom);
    const int segH = yBottom - yTop + 1;
    if (segH <= 0) return;
    int baseLight = (side ? 166 : 208) - (int)(dist * 18.0f);
    if (baseLight < 38) baseLight = 38;
    if (baseLight > 236) baseLight = 236;
    texX %= (int)wt->width;
    if (texX < 0) texX += (int)wt->width;
    for (int y = yTop; y <= yBottom; ++y) {
        int texY = ((y - yTop) * (int)wt->height) / segH;
        unsigned char idx = sampleTextureBitmap(*wt, texX, texY);
        unsigned int col = shadeColor(paletteColor(idx), baseLight);
        if (lavaTint) {
            const int pulse = 18 + (int)(14.0f * sinf((float)nowSeconds() * 5.0f + (float)x * 0.07f));
            unsigned int r = col & 0xffu;
            unsigned int g = (col >> 8) & 0xffu;
            unsigned int b = (col >> 16) & 0xffu;
            r = (unsigned int)std::min(255, (int)r + pulse);
            g = (unsigned int)std::max(0, (int)g - pulse / 4);
            b = (unsigned int)std::max(0, (int)b - pulse / 3);
            col = 0xff000000u | (b << 16) | (g << 8) | r;
        }
        gFramebuffer[y * FB_W + x] = col;
    }
}

static bool currentLgldCellBlocksViewAsHeightWall(int mx, int my) {
    const AssetInfo* ai = currentLgldAsset();
    if (!ai) return isWallCell(mx, my);
    const short raw = currentLgldCellRaw(mx, my);
    if (raw <= 0) return true;
    const int bi = lgldBlockIndexForCellRaw(mx, my);
    const LgldBlockInfo* nb = currentLgldBlockByIndex(bi);
    if (!nb) return true;

    const int fromFloor = currentPlayerFloorHeight();
    const int fromCeil = currentPlayerCeilHeight();
    const int delta = nb->floorHeight - fromFloor;

    // v63: movement may still block a blind deep drop, but the renderer must
    // not turn lava pits / lower shafts into raised walls.  A downward height
    // break is visible geometry, not a solid wall face from the current level.
    if (delta < -ORIG_PLAYER_MAX_DROP) return false;

    // Upward ledges that the player cannot climb are visible blockers until
    // the proper upper/lower wall segment renderer replaces this stop rule.
    if (delta > ORIG_PLAYER_MAX_RISE) return true;

    int clearance = 0;
    if (delta >= 0) {
        const int lowCeil = std::min(fromCeil, nb->ceilHeight);
        clearance = lowCeil - nb->floorHeight;
    } else {
        clearance = nb->ceilHeight - nb->floorHeight;
    }
    return clearance <= ORIG_PLAYER_HEIGHT + 8;
}

static void __attribute__((unused)) drawRoomRendererLegacy() {
    updatePlayerMotion();
    updateOriginalVTableProbe();
    updateOriginalSpanPrepProbe();
    updateHeightRayProbe();
    drawSkyBackground();
    drawFloorAndCeiling();
    // v63: keep original span prep/probe logging, but do not draw those spans yet.
    // The visible OTable approximation from v49 was too inaccurate and made the
    // surface motion worse. Only the controlled plane pass above is visible here.

    const float ca = cosf(gPlayerA), sa = sinf(gPlayerA);
    const float planeScale = cameraPlaneScale();
    const float planeX = -sa * planeScale, planeY = ca * planeScale;
    for (int x = 0; x < FB_W; ++x) {
        const float cameraX = 2.0f * (float)x / (float)FB_W - 1.0f;
        const float rayDirX = ca + planeX * cameraX, rayDirY = sa + planeY * cameraX;
        int mapX = (int)floorf(gPlayerX), mapY = (int)floorf(gPlayerY);
        float sideDistX, sideDistY;
        const float deltaDistX = (fabsf(rayDirX) < 0.0001f) ? 1.0e30f : fabsf(1.0f / rayDirX);
        const float deltaDistY = (fabsf(rayDirY) < 0.0001f) ? 1.0e30f : fabsf(1.0f / rayDirY);
        int stepX, stepY;
        if (rayDirX < 0) { stepX = -1; sideDistX = (gPlayerX - (float)mapX) * deltaDistX; } else { stepX = 1; sideDistX = ((float)mapX + 1.0f - gPlayerX) * deltaDistX; }
        if (rayDirY < 0) { stepY = -1; sideDistY = (gPlayerY - (float)mapY) * deltaDistY; } else { stepY = 1; sideDistY = ((float)mapY + 1.0f - gPlayerY) * deltaDistY; }
        int side = 0;
        int prevBlockIndex = lgldBlockIndexForCellRaw(mapX, mapY);
        short hitRaw = 0;
        int hitBlockIndex = 0;
        bool hitFound = false;
        for (int i = 0; i < 64; ++i) {
            if (sideDistX < sideDistY) { sideDistX += deltaDistX; mapX += stepX; side = 0; }
            else { sideDistY += deltaDistY; mapY += stepY; side = 1; }
            hitRaw = currentLgldCellRaw(mapX, mapY);
            hitBlockIndex = lgldBlockIndexForCellRaw(mapX, mapY);
            if (currentLgldCellBlocksViewAsHeightWall(mapX, mapY)) { hitFound = true; break; }
            if (hitRaw > 0 && hitBlockIndex > 0) prevBlockIndex = hitBlockIndex;
        }
        if (!hitFound) continue;
        int wallTexIndex = 0;
        const TextureBitmap* wt = textureForWallHit(mapX, mapY, side, stepX, stepY, wallTexIndex);
        if (!wt || !wt->ok) continue;
        float perpWallDist = (side == 0) ? ((float)mapX - gPlayerX + (1.0f - (float)stepX) * 0.5f) / rayDirX : ((float)mapY - gPlayerY + (1.0f - (float)stepY) * 0.5f) / rayDirY;
        if (perpWallDist < 0.04f) perpWallDist = 0.04f;
        float wallX = (side == 0) ? gPlayerY + perpWallDist * rayDirY : gPlayerX + perpWallDist * rayDirX;
        wallX -= floorf(wallX);
        int texX = (int)(wallX * (float)wt->width);
        if (side == 0 && rayDirX > 0) texX = (int)wt->width - texX - 1;
        if (side == 1 && rayDirY < 0) texX = (int)wt->width - texX - 1;

        const LgldBlockInfo* prevBlock = currentLgldBlockByIndex(prevBlockIndex);
        const LgldBlockInfo* hitBlock = currentLgldBlockByIndex(hitBlockIndex);
        int z0 = prevBlock ? prevBlock->floorHeight : currentPlayerFloorHeight();
        int z1 = prevBlock ? prevBlock->ceilHeight : currentPlayerCeilHeight();
        bool lavaTint = false;

        // v64: draw the column slice at sector heights instead of always drawing a
        // screen-centered full wall. Positive cells that only block because their
        // floor is too high become a riser face; solid cells close the previous sector.
        if (hitRaw > 0 && prevBlock && hitBlock) {
            const int dFloor = hitBlock->floorHeight - prevBlock->floorHeight;
            const int dCeil = hitBlock->ceilHeight - prevBlock->ceilHeight;
            if (dFloor > ORIG_PLAYER_MAX_RISE) {
                z0 = prevBlock->floorHeight;
                z1 = hitBlock->floorHeight;
                lavaTint = ((hitBlock->attributes & 3u) != 0u);
            } else if (dCeil < 0) {
                z0 = hitBlock->ceilHeight;
                z1 = prevBlock->ceilHeight;
            }
        }
        if (z1 <= z0) { z0 = currentPlayerFloorHeight(); z1 = currentPlayerCeilHeight(); }
        int drawStart = projectColumnHeightYV64(z1, perpWallDist);
        int drawEnd   = projectColumnHeightYV64(z0, perpWallDist);
        drawTexturedColumnSegmentV64(x, drawStart, drawEnd, wt, texX, perpWallDist, side, lavaTint);
    }

    std::vector<unsigned char> raw; unsigned int tw = 0, th = 0;
    if (snapshotCurrentTexture(raw, tw, th)) {
        const int sw = 48, sh = 48, sx0 = FB_W - sw - 8, sy0 = FB_H - sh - 8;
        for (int y = -2; y < sh + 2; ++y) for (int x = -2; x < sw + 2; ++x) if (x < 0 || y < 0 || x >= sw || y >= sh) putPixelSafe(sx0 + x, sy0 + y, 0xff000000u);
        for (int y = 0; y < sh; ++y) for (int x = 0; x < sw; ++x) putPixelSafe(sx0 + x, sy0 + y, paletteColor(sampleColumnMajorTexture(raw, tw, th, (x * (int)tw) / sw, (y * (int)th) / sh)));
    }
}

static int projectWorldYPortal(int worldZ, float distance) {
    if (distance < 0.025f) distance = 0.025f;
    const float projection = (float)FB_H / ORIGINAL_VERTICAL_REFERENCE;
    const float y = (float)VIEW_CENTER_Y - ((float)(worldZ - gPlayerEyeZ) * projection / distance);
    return (int)floorf(y + 0.5f);
}

static int blockLightAt(const LgldBlockInfo& block, float distance, int side) {
    (void)side;
    // DrawScreen.asm adds the signed sector illumination directly to a
    // 32-step lighting-table index. One level is therefore roughly 8/256 of
    // the palette range; positive values deliberately make rooms much darker.
    int light = 256 - (int)(distance * (block.fog ? 14.0f : 6.0f)) - block.illumination * 8;
    return std::max(8, std::min(256, light));
}

static void drawPortalPlaneSegment(int x, int y0, int y1, float rayX, float rayY,
                                   float nearDistance, float farDistance, const LgldBlockInfo& block,
                                   bool floorPlane, bool filled[FB_H]) {
    if (y0 > y1) return;
    const int texIndex = floorPlane ? block.floorTex : block.ceilTex;
    if (!floorPlane && texIndex < 0) return; // panoramic sky was drawn as the background
    const TextureBitmap* texture = levelTextureByIndex(texIndex);
    if ((!texture || !texture->ok) && gFloorTexCount > 0) texture = &gFloorTex[0];
    if (!texture || !texture->ok) return;
    const int planeZ = floorPlane ? block.floorHeight : block.ceilHeight;
    const float projection = (float)FB_H / ORIGINAL_VERTICAL_REFERENCE;
    y0 = std::max(0, y0);
    y1 = std::min(VIEW_H - 1, y1);
    for (int y = y0; y <= y1; ++y) {
        if (filled[y]) continue;
        const float denom = floorPlane ? ((float)y - (float)VIEW_CENTER_Y) : ((float)VIEW_CENTER_Y - (float)y);
        if (denom <= 0.25f) continue;
        const float numerator = floorPlane ? (float)(gPlayerEyeZ - planeZ) : (float)(planeZ - gPlayerEyeZ);
        const float distance = numerator * projection / denom;
        const float edgeTolerance = std::max(0.003f, distance * 0.03f);
        if (distance < nearDistance - edgeTolerance || distance > farDistance + edgeTolerance || distance <= 0.01f) continue;
        const float wx = gPlayerX + rayX * distance;
        const float wy = gPlayerY + rayY * distance;
        const unsigned char paletteIndex = sampleTextureBitmap(*texture,
            (int)floorf(wx * (float)texture->width), (int)floorf(wy * (float)texture->height));
        gFramebuffer[y * FB_W + x] = shadeColor(paletteColor(paletteIndex), blockLightAt(block, distance, 0));
        gWallDepth[(size_t)y * FB_W + (size_t)x] = distance;
        filled[y] = true;
    }
}

static const TextureBitmap* portalWallTexture(const LgldEdgeInfo* edge, int edgeIndex, int part) {
    int textureIndex = 0;
    if (edge) textureIndex = part == 0 ? edge->normTex : (part == 1 ? edge->upTex : edge->lowTex);
    if (edgeIndex > 0 && gActivatedSwitchParts.count((unsigned int)edgeIndex * 3u + (unsigned int)part) != 0u) {
        const TextureBitmap* switched = levelSwitchTextureByIndex(textureIndex);
        if (switched) return switched;
    }
    const TextureBitmap* texture = levelTextureByIndex(textureIndex);
    if ((!texture || !texture->ok) && gWallTexCount > 0) texture = &gWallTex[0];
    return texture;
}

struct PortalWallVerticalMapping {
    int anchorWorld = 0;
    bool unpegged = false;
};

static PortalWallVerticalMapping portalWallVerticalMapping(int firstHeight, int secondHeight,
                                                            const LgldEdgeInfo* edge,
                                                            unsigned int attributeBit) {
    PortalWallVerticalMapping mapping;
    // DrawScreen.asm tests ed_Attribute bit 0 for the upper texture and bit 1
    // for the lower texture.  A moving sector must never implicitly change
    // that flag: doing so makes fixed jamb and lift-side textures travel with
    // a door/platform.  Pegged textures start at the upper world edge;
    // unpegged textures end at the lower world edge.
    mapping.unpegged = edge && (edge->attribute & attributeBit) != 0u;
    mapping.anchorWorld = mapping.unpegged ? std::min(firstHeight, secondHeight)
                                           : std::max(firstHeight, secondHeight);
    return mapping;
}

static int portalWallTextureY(float distance, int screenY, int anchorWorld, bool unpegged, int textureHeight) {
    const float projection = (float)FB_H / ORIGINAL_VERTICAL_REFERENCE;
    const float worldAtY = (float)gPlayerEyeZ + ((float)VIEW_CENTER_Y - (float)screenY) * distance / projection;
    int texY = (int)floorf((float)anchorWorld - worldAtY);
    if (unpegged) texY += textureHeight - 1;
    return texY;
}

static void drawPortalWallSegment(int x, int y0, int y1, float distance, int brushOffset,
                                  const TextureBitmap* texture, const LgldBlockInfo& lightBlock,
                                  int side, bool filled[FB_H], int anchorWorld, bool unpegged) {
    if (!texture || !texture->ok || y0 > y1) return;
    y0 = std::max(0, y0);
    y1 = std::min(VIEW_H - 1, y1);
    if (y0 > y1) return;
    int texX = brushOffset * (int)texture->width / 64;
    for (int y = y0; y <= y1; ++y) {
        if (filled[y]) continue;
        const int texY = portalWallTextureY(distance, y, anchorWorld, unpegged, (int)texture->height);
        const unsigned char paletteIndex = sampleTextureBitmap(*texture, texX, texY);
        gFramebuffer[y * FB_W + x] = shadeColor(paletteColor(paletteIndex), blockLightAt(lightBlock, distance, side));
        gWallDepth[(size_t)y * FB_W + (size_t)x] = distance;
        filled[y] = true;
    }
}

static void sealPortalWallGaps(int x, int y0, int y1, float distance, int brushOffset,
                              const TextureBitmap* texture, const LgldBlockInfo& lightBlock,
                              int side, bool filled[FB_H], int anchorWorld) {
    if (!texture || !texture->ok) return;
    y0 = std::max(0, y0);
    y1 = std::min(VIEW_H - 1, y1);
    const int texX = brushOffset * (int)texture->width / 64;
    for (int y = y0; y <= y1; ++y) {
        if (filled[y]) continue;
        const int texY = portalWallTextureY(distance, y, anchorWorld, false, (int)texture->height);
        const unsigned char paletteIndex = sampleTextureBitmap(*texture, texX, texY);
        gFramebuffer[(size_t)y * FB_W + (size_t)x] =
            shadeColor(paletteColor(paletteIndex), blockLightAt(lightBlock, distance, side));
        gWallDepth[(size_t)y * FB_W + (size_t)x] = distance;
        filled[y] = true;
    }
}

static void drawPlacedObjectBillboards() {
    const AssetInfo* ai = currentLgldAsset();
    if (!ai) return;
    const float dirX = cosf(gPlayerA), dirY = sinf(gPlayerA);
    const float planeScale = cameraPlaneScale();
    const float planeX = -dirY * planeScale, planeY = dirX * planeScale;
    const float invDet = 1.0f / (planeX * dirY - dirX * planeY);
    struct ProjectedObject { const RuntimeObject* runtime; float depth; float lateral; };
    std::vector<ProjectedObject> visible;
    for (size_t i = 0; i < gRuntimeObjects.size(); ++i) {
        const RuntimeObject& runtime = gRuntimeObjects[i];
        if (runtime.collected || runtime.dead || runtime.placedIndex >= ai->lgldPlacedObjects.size()) continue;
        const LgldPlacedObject& object = ai->lgldPlacedObjects[runtime.placedIndex];
        if (object.activationTrigger != 0u && gActiveEnemyTriggers.count(object.activationTrigger) == 0u) continue;
        const float relX = runtime.x - gPlayerX;
        const float relY = runtime.y - gPlayerY;
        const float lateral = invDet * (dirY * relX - dirX * relY);
        const float depth = invDet * (-planeY * relX + planeX * relY);
        if (depth > 0.05f && fabsf(lateral / depth) < 1.4f) visible.push_back(ProjectedObject{&runtime, depth, lateral});
    }
    std::sort(visible.begin(), visible.end(), [](const ProjectedObject& a, const ProjectedObject& b) { return a.depth > b.depth; });
    for (size_t i = 0; i < visible.size(); ++i) {
        const RuntimeObject& runtime = *visible[i].runtime;
        const LgldPlacedObject& object = ai->lgldPlacedObjects[runtime.placedIndex];
        const int cellX = (int)floorf(runtime.x), cellY = (int)floorf(runtime.y);
        const LgldBlockInfo* block = currentLgldBlockForCell(cellX, cellY);
        if (!block) continue;
        std::map<std::string, GlobalObjectInfo>::const_iterator objectInfo = gGlobalObjectInfo.find(object.name);
        const GlobalObjectInfo* definition = runtime.deathDefinition ? runtime.deathDefinition :
            (objectInfo != gGlobalObjectInfo.end() ? &objectInfo->second : nullptr);
        const ObjectSpriteFrame* selectedFrame = nullptr;
        if (definition && !definition->frames.empty()) {
            int frameIndex = runtime.animationFrame;
            if (!runtime.dying && !runtime.corpse && definition->animationType < 0 && definition->frames.size() >= 128u) {
                int objectSector = ((int)floorf(runtime.heading * (4.0f / 3.14159265f) + 0.5f)) & 7;
                int playerSector = ((int)floorf(gPlayerA * (4.0f / 3.14159265f) + 0.5f)) & 7;
                const int direction = (objectSector + 6 - playerSector) & 7;
                frameIndex = direction * 16 + ((int)(nowSeconds() * 8.0) & 3);
            }
            selectedFrame = &definition->frames[(size_t)(frameIndex % (int)definition->frames.size())];
        }
        const bool haveSprite = selectedFrame ? !selectedFrame->pixels.empty()
            : definition && definition->spriteWidth > 0 && definition->spriteHeight > 0 && !definition->spritePixels.empty();
        const int spriteWidth = selectedFrame ? selectedFrame->width : (definition ? definition->spriteWidth : 0);
        const int spriteHeight = selectedFrame ? selectedFrame->height : (definition ? definition->spriteHeight : 0);
        const int spriteYOffset = selectedFrame ? selectedFrame->yOffset : (definition ? definition->spriteYOffset : 0);
        const std::vector<unsigned char>* spritePixels = selectedFrame ? &selectedFrame->pixels : (definition ? &definition->spritePixels : nullptr);
        const std::vector<unsigned char>* spriteMask = selectedFrame ? &selectedFrame->mask : (definition ? &definition->spriteMask : nullptr);
        const int objectHeight = haveSprite ? spriteHeight : (definition ? std::max(1, definition->height) : 64);
        const int objectYOffset = (haveSprite ? spriteYOffset : 0) + runtime.deathYOffset;
        const int screenX = (int)((float)FB_W * 0.5f * (1.0f + visible[i].lateral / visible[i].depth));
        // Keep pickups clear of the floor throughout their complete bob cycle.
        const int bob = definition && definition->objectType == 3u
            ? PICKUP_FLOAT_HEIGHT + (int)(sinf(runtime.bobPhase) * (float)PICKUP_BOB_AMPLITUDE)
            : 0;
        int top = projectWorldYPortal(block->floorHeight + objectYOffset + bob + objectHeight, visible[i].depth);
        int bottom = projectWorldYPortal(block->floorHeight + objectYOffset + bob, visible[i].depth);
        if (top > bottom) std::swap(top, bottom);
        const int spriteWorldWidth = haveSprite ? spriteWidth : (definition ? definition->radius * 2 : 32);
        const int halfWidth = std::max(1, (int)((float)spriteWorldWidth * 0.5f * ((float)FB_H / ORIGINAL_VERTICAL_REFERENCE) / visible[i].depth));
        const unsigned int hash = fnv1aUpdate(2166136261u, (const unsigned char*)object.name.data(), object.name.size());
        const unsigned int color = 0xff000000u | (((hash >> 16) & 0x7fu) + 96u) << 16 |
                                   (((hash >> 8) & 0x7fu) + 64u) << 8 | ((hash & 0x7fu) + 96u);
        for (int y = std::max(0, top); y <= std::min(VIEW_H - 1, bottom); ++y) {
            for (int x = std::max(0, screenX - halfWidth); x <= std::min(FB_W - 1, screenX + halfWidth); ++x) {
                const size_t pixel = (size_t)y * FB_W + (size_t)x;
                if (visible[i].depth >= gWallDepth[pixel]) continue;
                const int dx = x - (screenX - halfWidth), dy = y - top;
                unsigned int sourceColor = color;
                if (haveSprite) {
                    const int sx = std::min(spriteWidth - 1, dx * spriteWidth / std::max(1, halfWidth * 2 + 1));
                    const int sy = std::min(spriteHeight - 1, dy * spriteHeight / std::max(1, bottom - top + 1));
                    const size_t source = (size_t)sy * (size_t)spriteWidth + (size_t)sx;
                    if (!spriteMask || !spritePixels || source >= spriteMask->size() || (*spriteMask)[source] == 0) continue;
                    sourceColor = paletteColor((*spritePixels)[source]);
                } else if (((dx + dy) & 3) == 0) continue;
                gFramebuffer[(size_t)y * FB_W + (size_t)x] = shadeColor(sourceColor, blockLightAt(*block, visible[i].depth, 0));
                gWallDepth[pixel] = visible[i].depth;
            }
        }
    }
}

static void drawRuntimeProjectiles() {
    if (gRuntimeProjectiles.empty()) return;
    const float dirX = cosf(gPlayerA), dirY = sinf(gPlayerA);
    const float planeScale = cameraPlaneScale();
    const float planeX = -dirY * planeScale, planeY = dirX * planeScale;
    const float invDet = 1.0f / (planeX * dirY - dirX * planeY);
    struct ProjectedShot { const RuntimeProjectile* projectile; float depth; float lateral; };
    std::vector<ProjectedShot> visible;
    for (size_t i = 0; i < gRuntimeProjectiles.size(); ++i) {
        const RuntimeProjectile& projectile = gRuntimeProjectiles[i];
        if (projectile.dead || !projectile.definition) continue;
        const float relX = projectile.x - gPlayerX, relY = projectile.y - gPlayerY;
        const float lateral = invDet * (dirY * relX - dirX * relY);
        const float depth = invDet * (-planeY * relX + planeX * relY);
        if (depth > 0.03f && fabsf(lateral / depth) < 1.4f) visible.push_back(ProjectedShot{&projectile, depth, lateral});
    }
    std::sort(visible.begin(), visible.end(), [](const ProjectedShot& a, const ProjectedShot& b) { return a.depth > b.depth; });
    for (size_t i = 0; i < visible.size(); ++i) {
        const RuntimeProjectile& projectile = *visible[i].projectile;
        const GlobalObjectInfo& definition = *projectile.definition;
        const ObjectSpriteFrame* frame = nullptr;
        if (!definition.frames.empty()) frame = &definition.frames[(size_t)((int)(nowSeconds() * 14.0) % (int)definition.frames.size())];
        const int spriteWidth = frame ? frame->width : definition.spriteWidth;
        const int spriteHeight = frame ? frame->height : definition.spriteHeight;
        const std::vector<unsigned char>* pixels = frame ? &frame->pixels : &definition.spritePixels;
        const std::vector<unsigned char>* mask = frame ? &frame->mask : &definition.spriteMask;
        if (spriteWidth <= 0 || spriteHeight <= 0 || !pixels || pixels->empty()) continue;
        const LgldBlockInfo* block = currentLgldBlockForCell((int)floorf(projectile.x), (int)floorf(projectile.y));
        if (!block) continue;
        const int screenX = (int)((float)FB_W * 0.5f * (1.0f + visible[i].lateral / visible[i].depth));
        int top = projectWorldYPortal((int)projectile.z + spriteHeight, visible[i].depth);
        int bottom = projectWorldYPortal((int)projectile.z, visible[i].depth);
        if (top > bottom) std::swap(top, bottom);
        const int halfWidth = std::max(1, (int)((float)spriteWidth * 0.5f * ((float)FB_H / ORIGINAL_VERTICAL_REFERENCE) / visible[i].depth));
        for (int y = std::max(0, top); y <= std::min(VIEW_H - 1, bottom); ++y) for (int x = std::max(0, screenX - halfWidth); x <= std::min(FB_W - 1, screenX + halfWidth); ++x) {
            const size_t destination = (size_t)y * FB_W + (size_t)x;
            if (visible[i].depth >= gWallDepth[destination]) continue;
            const int sx = std::min(spriteWidth - 1, (x - (screenX - halfWidth)) * spriteWidth / std::max(1, halfWidth * 2 + 1));
            const int sy = std::min(spriteHeight - 1, (y - top) * spriteHeight / std::max(1, bottom - top + 1));
            const size_t source = (size_t)sy * (size_t)spriteWidth + (size_t)sx;
            if (source >= pixels->size() || (mask && source < mask->size() && (*mask)[source] == 0u)) continue;
            gFramebuffer[destination] = shadeColor(paletteColor((*pixels)[source]), blockLightAt(*block, visible[i].depth, 0));
            gWallDepth[destination] = visible[i].depth;
        }
    }
}

static void drawRuntimeImpactSparks() {
    if (gRuntimeImpactSparks.empty()) return;
    const float dirX = cosf(gPlayerA), dirY = sinf(gPlayerA);
    const float planeScale = cameraPlaneScale();
    const float planeX = -dirY * planeScale, planeY = dirX * planeScale;
    const float invDet = 1.0f / (planeX * dirY - dirX * planeY);
    for (size_t i = 0; i < gRuntimeImpactSparks.size(); ++i) {
        const RuntimeImpactSpark& spark = gRuntimeImpactSparks[i];
        if (spark.dead || spark.lifetime <= 0.0f) continue;
        const float remaining = std::max(0.0f, 1.0f - spark.age / spark.lifetime);
        const unsigned int color = shadeColor(spark.color,
            std::max(56, std::min(256, (int)(remaining * 256.0f))));
        // Three samples make a one-to-three-pixel streak at 320-era
        // resolution, while retaining a discrete shower of tiny particles.
        for (int trail = 0; trail < 3; ++trail) {
            const float t = (float)trail * 0.5f;
            const float worldX = spark.previousX + (spark.x - spark.previousX) * t;
            const float worldY = spark.previousY + (spark.y - spark.previousY) * t;
            const float worldZ = spark.previousZ + (spark.z - spark.previousZ) * t;
            const float relX = worldX - gPlayerX, relY = worldY - gPlayerY;
            const float lateral = invDet * (dirY * relX - dirX * relY);
            const float depth = invDet * (-planeY * relX + planeX * relY);
            if (depth <= 0.03f || fabsf(lateral / depth) >= 1.35f) continue;
            const int screenX = (int)((float)FB_W * 0.5f * (1.0f + lateral / depth));
            const int screenY = projectWorldYPortal((int)worldZ, depth);
            const int radius = depth < 1.25f && trail == 2 ? 1 : 0;
            for (int py = screenY - radius; py <= screenY + radius; ++py) {
                if ((unsigned)py >= (unsigned)VIEW_H) continue;
                for (int px = screenX - radius; px <= screenX + radius; ++px) {
                    if ((unsigned)px >= (unsigned)FB_W) continue;
                    const size_t destination = (size_t)py * FB_W + (size_t)px;
                    if (depth < gWallDepth[destination]) gFramebuffer[destination] = color;
                }
            }
        }
    }
}

static void sealOnePixelDepthPinholes() {
    // Fixed-point edge projection in the original never leaves an uncovered
    // scanline between two owned spans. Close only one-row holes here; wider
    // portal openings remain transparent and reveal objects progressively.
    for (int x = 0; x < FB_W; ++x) for (int y = 1; y + 1 < VIEW_H; ++y) {
        const size_t pixel = (size_t)y * FB_W + (size_t)x;
        if (gWallDepth[pixel] < 1.0e29f) continue;
        const size_t above = pixel - FB_W, below = pixel + FB_W;
        if (gWallDepth[above] >= 1.0e29f || gWallDepth[below] >= 1.0e29f) continue;
        const size_t source = gWallDepth[above] <= gWallDepth[below] ? above : below;
        gFramebuffer[pixel] = gFramebuffer[source];
        gWallDepth[pixel] = gWallDepth[source];
    }
}

static void drawRoomRenderer() {
    updatePlayerMotion();
    updateRuntimeObjects();
    updateOriginalVTableProbe();
    updateOriginalSpanPrepProbe();
    updateHeightRayProbe();
    drawSkyBackground();
    for (int y = VIEW_CENTER_Y; y < VIEW_H; ++y)
        for (int x = 0; x < FB_W; ++x) gFramebuffer[(size_t)y * FB_W + (size_t)x] = 0xff080808u;
    std::fill(gWallDepth.begin(), gWallDepth.end(), 1.0e30f);

    const float dirX = cosf(gPlayerA), dirY = sinf(gPlayerA);
    const float planeScale = cameraPlaneScale();
    const float planeX = -dirY * planeScale, planeY = dirX * planeScale;
    const int startBlockIndex = currentLgldBlockIndexForCell((int)floorf(gPlayerX), (int)floorf(gPlayerY));
    for (int x = 0; x < FB_W; ++x) {
        bool filled[FB_H] = {false};
        const float cameraX = 2.0f * (float)x / (float)FB_W - 1.0f;
        const float rayX = dirX + planeX * cameraX;
        const float rayY = dirY + planeY * cameraX;
        std::vector<OrigVtHit> hits;
        buildOriginalStyleVTableForRay(rayX, rayY, hits);
        int previousBlockIndex = startBlockIndex;
        float nearDistance = 0.0f;
        int clipTop = 0, clipBottom = VIEW_H - 1;
        for (size_t hi = 0; hi < hits.size() && clipTop <= clipBottom; ++hi) {
            const OrigVtHit& hit = hits[hi];
            const LgldBlockInfo* previous = currentLgldBlockByIndex(previousBlockIndex);
            const LgldBlockInfo* entered = currentLgldBlockByIndex(hit.blockIndex);
            if (!previous) break;
            drawPortalPlaneSegment(x, clipTop, std::min(clipBottom, VIEW_CENTER_Y - 1), rayX, rayY,
                                   nearDistance, hit.distance, *previous, false, filled);
            drawPortalPlaneSegment(x, std::max(clipTop, VIEW_CENTER_Y), clipBottom, rayX, rayY,
                                   nearDistance, hit.distance, *previous, true, filled);
            if (!entered) {
                const TextureBitmap* texture = gWallTexCount > 0 ? &gWallTex[0] : nullptr;
                drawPortalWallSegment(x, clipTop, clipBottom, hit.distance, hit.brushOffset,
                                      texture, *previous, hit.side, filled, previous->ceilHeight, false);
                break;
            }
            const LgldEdgeInfo* edge = currentLgldEdgeByIndex(hit.edgeIndex);
            const int previousCeilY = projectWorldYPortal(previous->ceilHeight, hit.distance);
            const int enteredCeilY = projectWorldYPortal(entered->ceilHeight, hit.distance);
            const int previousFloorY = projectWorldYPortal(previous->floorHeight, hit.distance);
            const int enteredFloorY = projectWorldYPortal(entered->floorHeight, hit.distance);
            const int previousCeilDelta = runtimePlaneDelta(previousBlockIndex, true);
            const int enteredCeilDelta = runtimePlaneDelta(hit.blockIndex, true);
            const int previousFloorDelta = runtimePlaneDelta(previousBlockIndex, false);
            const int enteredFloorDelta = runtimePlaneDelta(hit.blockIndex, false);
            if (previous->ceilHeight != entered->ceilHeight) {
                const int y0 = std::max(clipTop, std::min(previousCeilY, enteredCeilY));
                const int y1 = std::min(clipBottom, std::max(previousCeilY, enteredCeilY));
                const PortalWallVerticalMapping mapping = portalWallVerticalMapping(
                    previous->ceilHeight, entered->ceilHeight, edge, 1u);
                drawPortalWallSegment(x, y0, y1, hit.distance, hit.brushOffset,
                    portalWallTexture(edge, hit.edgeIndex, 1), *previous, hit.side, filled,
                    mapping.anchorWorld, mapping.unpegged);
            }
            if (previous->floorHeight != entered->floorHeight) {
                const int y0 = std::max(clipTop, std::min(previousFloorY, enteredFloorY));
                const int y1 = std::min(clipBottom, std::max(previousFloorY, enteredFloorY));
                const PortalWallVerticalMapping mapping = portalWallVerticalMapping(
                    previous->floorHeight, entered->floorHeight, edge, 2u);
                drawPortalWallSegment(x, y0, y1, hit.distance, hit.brushOffset,
                    portalWallTexture(edge, hit.edgeIndex, 2), *previous, hit.side, filled,
                    mapping.anchorWorld, mapping.unpegged);
            }
            const int portalTop = std::max(previousCeilY, enteredCeilY);
            const int portalBottom = std::min(previousFloorY, enteredFloorY);
            if (hit.stopWall || (edge && edge->normTex != 0)) {
                // Offer the boundary rows to the wall as well. The per-column
                // filled mask preserves plane ownership, while any rounding
                // hole receives wall depth and can no longer expose a sprite.
                const int y0 = std::max(clipTop, portalTop);
                const int y1 = std::min(clipBottom, portalBottom);
                // The normal texture is the fixed jamb/side wall. Only upper and
                // lower door leaves follow their moving plane; keeping this anchor
                // at the level's initial height prevents sideways texture sliding.
                int normalAnchor = entered->ceilHeight;
                const int anchorBlock = (abs(previousCeilDelta) + abs(previousFloorDelta) >
                                         abs(enteredCeilDelta) + abs(enteredFloorDelta))
                    ? previousBlockIndex : hit.blockIndex;
                if (anchorBlock > 0 && anchorBlock < (int)gInitialRuntimeBlocks.size())
                    normalAnchor = gInitialRuntimeBlocks[(size_t)anchorBlock].ceilHeight;
                drawPortalWallSegment(x, y0, y1, hit.distance, hit.brushOffset,
                    portalWallTexture(edge, hit.edgeIndex, 0), *previous, hit.side, filled, normalAnchor, false);
                sealPortalWallGaps(x, y0, y1, hit.distance, hit.brushOffset,
                    portalWallTexture(edge, hit.edgeIndex, 0), *previous, hit.side, filled, normalAnchor);
                break;
            }
            clipTop = std::max(clipTop, portalTop);
            clipBottom = std::min(clipBottom, portalBottom);
            nearDistance = hit.distance;
            previousBlockIndex = hit.blockIndex;
        }
        const LgldBlockInfo* last = currentLgldBlockByIndex(previousBlockIndex);
        if (last && clipTop <= clipBottom) {
            drawPortalPlaneSegment(x, clipTop, std::min(clipBottom, VIEW_CENTER_Y - 1), rayX, rayY,
                                   nearDistance, 64.0f, *last, false, filled);
            drawPortalPlaneSegment(x, std::max(clipTop, VIEW_CENTER_Y), clipBottom, rayX, rayY,
                                   nearDistance, 64.0f, *last, true, filled);
        }
    }
    sealOnePixelDepthPinholes();
    drawPlacedObjectBillboards();
    drawRuntimeProjectiles();
    drawRuntimeImpactSparks();
}


static unsigned int mapColorForCell(int cell) {
    if (cell <= 0) return 0x66202020u;
    static const unsigned int colors[] = {
        0xff303030u, 0xff605040u, 0xff405060u, 0xff506040u,
        0xff604060u, 0xff606040u, 0xff406060u, 0xff704030u,
        0xff307040u, 0xff403070u
    };
    return colors[cell % 10];
}

static void fillRectSafe(int x0, int y0, int w, int h, unsigned int argb) {
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) putPixelSafe(x0 + x, y0 + y, argb);
    }
}

static void drawLineSafe(int x0, int y0, int x1, int y1, unsigned int argb) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        putPixelSafe(x0, y0, argb);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void __attribute__((unused)) drawMiniMapOverlay() {
    const AssetInfo* ai = currentLgldAsset();
    if (ai) {
        const int size = 64;
        const int ox = FB_W - size - 8;
        const int oy = FB_H - size - 8;
        fillRectSafe(ox - 2, oy - 2, size + 4, size + 4, 0xcc000000u);
        const int minX = std::max(0, ai->lgldMinX);
        const int minY = std::max(0, ai->lgldMinY);
        const int maxX = std::min(127, ai->lgldMaxX);
        const int maxY = std::min(127, ai->lgldMaxY);
        const int spanX = std::max(1, maxX - minX + 1);
        const int spanY = std::max(1, maxY - minY + 1);
        for (int yy = 0; yy < size; ++yy) {
            for (int xx = 0; xx < size; ++xx) {
                const int mx = minX + (xx * spanX) / size;
                const int my = minY + (yy * spanY) / size;
                const short v = currentLgldCellRaw(mx, my);
                unsigned int col = 0xff101010u;
                if (v > 0) {
                    const LgldBlockInfo* mb = currentLgldBlockForCell(mx, my);
                    if (mb && (mb->effect || mb->trigger || mb->trigger2 || (mb->attributes & 0xf0u))) {
                        col = 0xffa07020u;
                    } else if (mb && ((mb->attributes & 3u) != 0u)) {
                        const unsigned int pulse = (unsigned int)(96 + ((mb->attributes & 3u) * 32u));
                        col = 0xff000000u | (pulse << 16) | (0x3020u);
                    } else if (mb) {
                        int shade = 64 + (mb->floorHeight + 128) / 4;
                        if (shade < 28) shade = 28;
                        if (shade > 180) shade = 180;
                        col = 0xff000000u | ((unsigned int)(shade / 3) << 16) | ((unsigned int)shade << 8) | (unsigned int)(shade / 3);
                    } else {
                        col = 0xff304030u;
                    }
                }
                else if (v < 0) col = mapColorForCell(1 + ((-v) % 8));
                putPixelSafe(ox + xx, oy + yy, col);
            }
        }
        const int px = ox + (int)(((gPlayerX - (float)minX) / (float)spanX) * (float)size);
        const int py = oy + (int)(((gPlayerY - (float)minY) / (float)spanY) * (float)size);
        fillRectSafe(px - 1, py - 1, 3, 3, 0xffffffffu);
        drawLineSafe(px, py, px + (int)(cosf(gPlayerA) * 8.0f), py + (int)(sinf(gPlayerA) * 8.0f), 0xffffffffu);
        return;
    }

    const int scale = 4;
    const int ox = 8;
    const int oy = FB_H - MAP_H * scale - 8;
    fillRectSafe(ox - 2, oy - 2, MAP_W * scale + 4, MAP_H * scale + 4, 0xcc000000u);
    for (int my = 0; my < MAP_H; ++my) {
        for (int mx = 0; mx < MAP_W; ++mx) {
            int cell = mapCell(mx, my);
            unsigned int col = mapColorForCell(cell);
            if (cell == 0) col = 0xaa202020u;
            fillRectSafe(ox + mx * scale, oy + my * scale, scale - 1, scale - 1, col);
        }
    }
    const int px = ox + (int)(gPlayerX * (float)scale);
    const int py = oy + (int)(gPlayerY * (float)scale);
    fillRectSafe(px - 1, py - 1, 3, 3, 0xffffffffu);
    const int lx = px + (int)(cosf(gPlayerA) * 8.0f);
    const int ly = py + (int)(sinf(gPlayerA) * 8.0f);
    drawLineSafe(px, py, lx, ly, 0xffffffffu);
}

static void __attribute__((unused)) drawDiagnosticsOverlay() {
    char line[160];
    const AssetInfo* ai = currentLgldAsset();
    if (ai) {
        snprintf(line, sizeof(line), "%s  FPS:%02d", ai->name.c_str(), (int)(gFps + 0.5)); drawText(4, 4, line);
    } else {
        snprintf(line, sizeof(line), "FPS:%02d ABI:%s", (int)(gFps + 0.5), abiName()); drawText(4, 4, line);
        snprintf(line, sizeof(line), "TEX:%d/%d %s", gTextureIndex + 1, (int)gTextureEntries.size(), gGldEntry0Name.c_str()); drawText(4, 14, line);
        snprintf(line, sizeof(line), "WH:%ux%u WALLS:%d FLOORS:%d", gTex0Width, gTex0Height, gWallTexCount, gFloorTexCount); drawText(4, 24, line);
        snprintf(line, sizeof(line), "POS:%2.2f,%2.2f A:%2.2f", gPlayerX, gPlayerY, gPlayerA); drawText(4, 34, line);
        snprintf(line, sizeof(line), "OB:%d FH:%d CH:%d VT:%u", gOrigCenterBlock, gOrigCenterFloor, gOrigCenterCeil, (unsigned int)gOrigCenterVTable.size()); drawText(4, 44, line);
        if (!gOrigCenterVTable.empty()) { const OrigVtHit& h = gOrigCenterVTable[0]; snprintf(line, sizeof(line), "V0:B%d%s E%d O%d D:%1.2f", h.blockIndex, h.stopWall ? "!" : "", h.edgeIndex, h.brushOffset, h.distance); drawText(4, 54, line); }
        snprintf(line, sizeof(line), "OT:C%d F%d U%d L%d", gOrigSpanCeilSegments, gOrigSpanFloorSegments, gOrigSpanUpperChanges, gOrigSpanLowerChanges); drawText(4, 64, line);
        drawText(4, 74, "MODE:LGLD LAVA/DROP V61");
    }
}

static void drawLgldMapPreview(const AssetInfo& ai) {
    if (!ai.lgldParseOk || ai.lgldMapCells.size() != 128u * 128u) return;
    const int ox = 188;
    const int oy = 30;
    fillRectSafe(ox - 2, oy - 2, 132, 132, 0xff000000u);
    for (int y = 0; y < 128; ++y) {
        for (int x = 0; x < 128; ++x) {
            short v = ai.lgldMapCells[(size_t)y * 128u + (size_t)x];
            unsigned int col = 0xff101010u;
            if (v > 0) {
                unsigned int c = 48u + ((unsigned int)v * 17u) % 128u;
                col = 0xff000000u | ((c + 32u) << 16) | (c << 8) | (c + 64u);
            } else if (v < 0) {
                unsigned int c = 80u + ((unsigned int)(-v) * 13u) % 128u;
                col = 0xff000000u | (32u << 16) | (32u << 8) | (c + 64u);
            }
            putPixelSafe(ox + x, oy + y, col);
        }
    }
    if (ai.lgldUsedCells) {
        for (int x = ai.lgldMinX; x <= ai.lgldMaxX; ++x) {
            putPixelSafe(ox + x, oy + ai.lgldMinY, 0xffffffffu);
            putPixelSafe(ox + x, oy + ai.lgldMaxY, 0xffffffffu);
        }
        for (int y = ai.lgldMinY; y <= ai.lgldMaxY; ++y) {
            putPixelSafe(ox + ai.lgldMinX, oy + y, 0xffffffffu);
            putPixelSafe(ox + ai.lgldMaxX, oy + y, 0xffffffffu);
        }
    }
}

static void drawAssetExplorer() {
    for (int y = 0; y < FB_H; ++y) {
        for (int x = 0; x < FB_W; ++x) {
            unsigned int shade = (unsigned int)(10 + (y * 22) / FB_H);
            gFramebuffer[y * FB_W + x] = 0xff000000u | (shade << 16) | ((shade + 6u) << 8) | (shade + 12u);
        }
    }

    drawText(4, 4, "MODE:ASSET EXPLORER V61");
    char line[160];
    snprintf(line, sizeof(line), "GLD:%d  DPAD L/R file", (int)gAssetInfos.size());
    drawText(4, 16, line);

    if (!gAssetInfos.empty()) {
        const AssetInfo& ai = gAssetInfos[(size_t)gAssetIndex];
        snprintf(line, sizeof(line), "%02d/%02d %s", gAssetIndex + 1, (int)gAssetInfos.size(), ai.name.c_str());
        drawText(4, 32, line);
        snprintf(line, sizeof(line), "ID:%s  TYPE:%s", ai.id.c_str(), ai.guess.c_str());
        drawText(4, 44, line);
        snprintf(line, sizeof(line), "SIZE:%ld  CHK:%08X", ai.size, ai.checksum);
        drawText(4, 56, line);
        if (ai.id == "VDCO") {
            snprintf(line, sizeof(line), "PACK:%u UNPACK:%u T:%u", ai.packedSize, ai.unpackedSize, ai.compressionType);
            drawText(4, 68, line);
            snprintf(line, sizeof(line), "UID:%s UCHK:%08X", ai.unpackedId.c_str(), ai.unpackedHash);
            drawText(4, 80, line);
            snprintf(line, sizeof(line), "LGLD:%s B:%u E:%u", ai.lgldParseOk ? "OK" : "MISS", ai.lgldBlocks, ai.lgldEdges);
            drawText(4, 92, line);
            snprintf(line, sizeof(line), "MAP:%u SOL:%u", ai.lgldUsedCells, ai.lgldSolidCells);
            drawText(4, 104, line);
            snprintf(line, sizeof(line), "BND:%d,%d-%d,%d", ai.lgldMinX, ai.lgldMinY, ai.lgldMaxX, ai.lgldMaxY);
            drawText(4, 116, line);
            snprintf(line, sizeof(line), "T:%u O:%u S:%u LP:%s", ai.lgldTextureNames, ai.lgldObjects, ai.lgldSoundNames, ai.lgldLoadPic.c_str());
            drawText(4, 128, line);
            drawLgldMapPreview(ai);
        } else {
            snprintf(line, sizeof(line), "DIR:%u  COUNT:%u", ai.dirOffset, ai.count);
            drawText(4, 68, line);
            snprintf(line, sizeof(line), "HEAD:%s", ai.headHex.c_str());
            drawText(4, 80, line);
        }
    }

    int y = 142;
    const int startList = std::max(0, gAssetIndex - 4);
    const int endList = std::min((int)gAssetInfos.size(), startList + 9);
    for (int i = startList; i < endList; ++i) {
        const AssetInfo& ai = gAssetInfos[(size_t)i];
        snprintf(line, sizeof(line), "%c%02d %-12s %-10s %6ld", (i == gAssetIndex ? '>' : ' '), i + 1, ai.name.c_str(), ai.guess.c_str(), ai.size);
        drawText(4, y, line);
        y += 10;
    }
}

static unsigned int presentationPaletteColor(const GfxBitmap& picture, unsigned char index) {
    const size_t offset = (size_t)index * 3u;
    if (offset + 2u >= picture.palette.size()) return paletteColor(index);
    const unsigned int r = picture.palette[offset];
    const unsigned int g = picture.palette[offset + 1u];
    const unsigned int b = picture.palette[offset + 2u];
    return 0xff000000u | (b << 16) | (g << 8) | r;
}

static void putPresentationPixel(int x, int y, unsigned int color) {
    if ((unsigned)x >= (unsigned)ORIGINAL_W || (unsigned)y >= (unsigned)PRESENTATION_H) return;
    gPresentationFramebuffer[(size_t)y * ORIGINAL_W + (size_t)x] = color;
}

static void fillPresentationRect(int x, int y, int w, int h, unsigned int color) {
    for (int py = std::max(0, y); py < std::min(PRESENTATION_H, y + h); ++py)
        for (int px = std::max(0, x); px < std::min(ORIGINAL_W, x + w); ++px)
            putPresentationPixel(px, py, color);
}

static void drawPresentationText(int x, int y, const char* text) {
    int px = x;
    while (*text) {
        if (*text == ' ') px += 4;
        else {
            const unsigned char* glyph = glyph5x7(*text);
            for (int row = 0; row < 7; ++row) for (int col = 0; col < 5; ++col)
                if (glyph[row] & (1 << (4 - col))) putPresentationPixel(px + col, y + row, 0xffffffffu);
            px += 6;
        }
        ++text;
    }
}

static void drawPresentationTextCentered(int y, const char* text) {
    drawPresentationText((ORIGINAL_W - textWidth(text)) / 2, y, text);
}

static bool drawPresentationPicture(const std::string& name) {
    std::map<std::string, GfxBitmap>::const_iterator found = gPresentationGraphics.find(name);
    if (found == gPresentationGraphics.end() || !found->second.ok) return false;
    const GfxBitmap& picture = found->second;
    std::fill(gPresentationFramebuffer.begin(), gPresentationFramebuffer.end(), 0xff000000u);
    if (picture.width == ORIGINAL_W && picture.height >= PRESENTATION_H) {
        for (int y = 0; y < PRESENTATION_H; ++y) {
            const int sy = y;
            for (int x = 0; x < ORIGINAL_W; ++x) {
                const int sx = x;
                const unsigned char index = picture.pixels[(size_t)sy * (size_t)picture.width + (size_t)sx];
                gPresentationFramebuffer[(size_t)y * ORIGINAL_W + (size_t)x] = presentationPaletteColor(picture, index);
            }
        }
    } else {
        const int dstY = picture.y;
        for (int y = 0; y < picture.height; ++y) {
            const int sy = y;
            for (int x = 0; x < picture.width; ++x) {
                const int dx = picture.x + x;
                const int dy = dstY + y;
                if ((unsigned int)dx >= ORIGINAL_W || (unsigned int)dy >= PRESENTATION_H) continue;
                const unsigned char index = picture.pixels[(size_t)sy * (size_t)picture.width + (size_t)x];
                gPresentationFramebuffer[(size_t)dy * ORIGINAL_W + (size_t)dx] = presentationPaletteColor(picture, index);
            }
        }
    }
    return true;
}

static void setFrontendState(FrontendState state) {
    if (state == FRONTEND_PAUSE && gFrontendState != FRONTEND_PAUSE) {
        std::lock_guard<std::mutex> lock(gGameSnapshotMutex);
        gPauseBackground = gHaveGameSnapshot ? gLastGameFramebuffer : gFramebuffer;
    }
    gFrontendState = state;
    gFrontendStateSince = nowSeconds();
    if (state != FRONTEND_GAME) {
        stopSoundGroup(1);
        gFireHeld = false;
        gFireLatch = false;
        gRunHeld = false;
        gNextAutoFireTime = 0.0;
        gFireReleaseDeadline = 0.0;
    }
    if (state == FRONTEND_GAME) {
        gMoveLastTime = gFrontendStateSince;
        gObjectLastTime = gFrontendStateSince;
    }
}

static int runtimeTerminalChoiceCount() {
    static const int counts[4] = {4, 6, 7, 8};
    return counts[std::max(0, std::min(3, gRuntimeTerminalPage))];
}

static void closeRuntimeTerminal() {
    gRuntimeTerminalNumber = 0;
    gRuntimeTerminalPage = 0;
    gRuntimeTerminalSelection = 0;
    setFrontendState(FRONTEND_GAME);
}

static void selectRuntimeTerminalItem() {
    const int selection = gRuntimeTerminalSelection;
    if (gRuntimeTerminalPage == 0) {
        if (selection == 3) { playGlobalSoundCode(2); closeRuntimeTerminal(); }
        else {
            gRuntimeTerminalPage = selection + 1;
            gRuntimeTerminalSelection = 0;
            playGlobalSoundCode(2);
        }
        return;
    }

    bool accepted = false;
    if (gRuntimeTerminalPage == 1) {
        static const int costs[5] = {4000, 9000, 13000, 20000, 35000};
        if (selection == 5) {
            gRuntimeTerminalPage = 0; gRuntimeTerminalSelection = 0; playGlobalSoundCode(2); return;
        }
        const int weapon = selection + 1;
        if (gPlayerCredits >= costs[selection] && gPlayerWeapons[weapon] == 0) {
            gPlayerWeapons[weapon] = 1;
            gPlayerWeapon = weapon;
            gPlayerCredits -= costs[selection];
            accepted = true;
        }
    } else if (gRuntimeTerminalPage == 2) {
        static const int costs[6] = {1500, 2000, 5000, -1, 20000, 20000};
        if (selection == 6) {
            gRuntimeTerminalPage = 0; gRuntimeTerminalSelection = 0; playGlobalSoundCode(2); return;
        }
        if (costs[selection] >= 0 && gPlayerCredits >= costs[selection] &&
            gPlayerWeapons[selection] == 1) {
            gPlayerWeapons[selection] = 2;
            gPlayerCredits -= costs[selection];
            accepted = true;
        }
    } else {
        static const int costs[7] = {200, 150, 200, 5000, 5000, 5000, 5000};
        if (selection == 7) {
            gRuntimeTerminalPage = 0; gRuntimeTerminalSelection = 0; playGlobalSoundCode(2); return;
        }
        if (gPlayerCredits >= costs[selection]) {
            if (selection == 0 && gPlayerHealth < 100) { gPlayerHealth = std::min(100, gPlayerHealth + 10); accepted = true; }
            else if (selection == 1 && gPlayerShields < 100) { gPlayerShields = std::min(100, gPlayerShields + 10); accepted = true; }
            else if (selection == 2 && gPlayerEnergy < 9999) { gPlayerEnergy = std::min(9999, gPlayerEnergy + 100); accepted = true; }
            else if (selection >= 3 && selection <= 6 && !gPlayerKeys[selection - 3]) {
                gPlayerKeys[selection - 3] = true;
                accepted = true;
            }
            if (accepted) gPlayerCredits -= costs[selection];
        }
    }
    // Terminal.asm plays GlobalSound2 only for an accepted selection.
    if (accepted) {
        markGameProgressDirty();
        playGlobalSoundCode(2);
    }
}

static void drawFrontendMenu() {
    drawPresentationPicture("BTIT");
    drawPresentationTextCentered(74, "CONFIGURATION MENU");
    static const char* entries[] = {"START GAME", "SOUND", "CONTROLS", "GAME OPTIONS", "CREDITS", "QUIT GAME"};
    for (int i = 0; i < 6; ++i) {
        const int y = 98 + i * 14;
        if (i == gFrontendMenuSelection) fillPresentationRect(72, y - 4, 176, 16, 0xff805020u);
        drawPresentationTextCentered(y, entries[i]);
    }
    if (nowSeconds() < gGodModeMessageUntil) {
        fillPresentationRect(49, 203, 222, 15, 0xff204080u);
        drawPresentationTextCentered(207, "GODMODE ACTIVE");
    }
}

static void drawSoundMenu() {
    drawPresentationPicture("BTIT");
    drawPresentationTextCentered(74, "SOUND CONFIGURATION");
    char soundVolume[40], musicVolume[40];
    snprintf(soundVolume, sizeof(soundVolume), "SOUND VOLUME   %d", gSoundVolume);
    snprintf(musicVolume, sizeof(musicVolume), "MUSIC VOLUME   %d", gMusicVolume);
    const char* entries[] = {soundVolume, musicVolume,
        gSoundEnabled.load() ? "SOUND STATE    ON" : "SOUND STATE   OFF",
        gMusicEnabled ? "MUSIC STATE    ON" : "MUSIC STATE   OFF", "MAIN PAGE"};
    for (int i = 0; i < 5; ++i) {
        const int y = 98 + i * 14;
        if (i == gSoundMenuSelection) fillPresentationRect(67, y - 4, 186, 16, 0xff805020u);
        drawPresentationTextCentered(y, entries[i]);
    }
}

static const char* controllerButtonName(int key) {
    switch (key) {
        case 96: return "A"; case 97: return "B"; case 99: return "X"; case 100: return "Y";
        case 102: return "L1"; case 103: return "R1"; case 104: return "L2"; case 105: return "R2";
        case 108: return "START"; case 109: return "SELECT"; case 110: return "MODE";
        default: return "BUTTON";
    }
}

static void drawControlsMenu() {
    drawPresentationPicture("BTIT");
    drawPresentationTextCentered(74, "CONTROLS");
    const int bindings[] = {gFireKey, gActivateKey, gWeaponKey, gRunKey, gMenuKey};
    static const char* functions[] = {"FIRE", "ACTIVATE", "NEXT WEAPON", "RUN", "MENU"};
    for (int i = 0; i < 5; ++i) {
        const int y = 98 + i * 14;
        char line[48];
        snprintf(line, sizeof(line), "%s  %s", functions[i], controllerButtonName(bindings[i]));
        if (i == gControlsMenuSelection) fillPresentationRect(57, y - 4, 206, 16, 0xff805020u);
        drawPresentationTextCentered(y, line);
    }
    if (gControlsMenuSelection == 5) fillPresentationRect(57, 164, 206, 16, 0xff805020u);
    drawPresentationTextCentered(168, "MAIN PAGE");
    if (gControlCapture >= 0) {
        fillPresentationRect(49, 203, 222, 15, 0xff204080u);
        drawPresentationTextCentered(207, "PRESS CONTROLLER BUTTON");
    }
    if (nowSeconds() < gGodModeMessageUntil) {
        fillPresentationRect(49, 203, 222, 15, 0xff204080u);
        drawPresentationTextCentered(207, "GODMODE ACTIVE");
    }
}

static void drawGameOptionsMenu() {
    drawPresentationPicture("BTIT");
    drawPresentationTextCentered(74, "GAME OPTIONS");
    int totalLevels = 0;
    int level = playableLevelOrdinal(gAssetIndex, &totalLevels);
    if (level <= 0) level = 1;
    if (totalLevels <= 0) totalLevels = 1;
    char line[48];
    snprintf(line, sizeof(line), "LEVEL     %02d / %02d", level, totalLevels);
    drawPresentationTextCentered(98, line);
    snprintf(line, sizeof(line), "HEALTH    %03d", std::max(0, std::min(100, gPlayerHealth)));
    drawPresentationTextCentered(112, line);
    snprintf(line, sizeof(line), "ARMOR     %03d", std::max(0, std::min(100, gPlayerShields)));
    drawPresentationTextCentered(126, line);
    snprintf(line, sizeof(line), "CREDITS   %05d", std::max(0, std::min(99999, gPlayerCredits)));
    drawPresentationTextCentered(140, line);
    fillPresentationRect(72, 158, 176, 16, 0xff805020u);
    drawPresentationTextCentered(162, "RESET GAME");
    if (nowSeconds() < gGameResetMessageUntil) {
        fillPresentationRect(49, 203, 222, 15, 0xff204080u);
        drawPresentationTextCentered(207, "GAME RESET");
    }
}

static void drawPauseMenu() {
    if (gPauseBackground.size() == gFramebuffer.size()) gFramebuffer = gPauseBackground;
    drawTextCentered(58, "GAME MENU");
    static const char* entries[] = {"RESUME", "RESTART LEVEL", "QUIT TO TITLE"};
    for (int i = 0; i < 3; ++i) {
        const int y = 78 + i * 14;
        if (i == gPauseMenuSelection) fillRectSafe((FB_W - 136) / 2, y - 4, 136, 15, 0xff604020u);
        drawTextCentered(y, entries[i]);
    }
}

static void drawOriginalHud() {
    if (gHudPanelPixels.size() == 320u * 40u) {
        for (int y = 0; y < 40; ++y) {
            const unsigned int left = paletteColor(gHudPanelPixels[(size_t)y * 320u]);
            const unsigned int right = paletteColor(gHudPanelPixels[(size_t)y * 320u + 319u]);
            for (int x = 0; x < HUD_X; ++x) gFramebuffer[(size_t)(160 + y) * FB_W + (size_t)x] = left;
            for (int x = 0; x < 320; ++x)
                gFramebuffer[(size_t)(160 + y) * FB_W + (size_t)(HUD_X + x)] =
                    paletteColor(gHudPanelPixels[(size_t)y * 320u + (size_t)x]);
            for (int x = HUD_X + 320; x < FB_W; ++x) gFramebuffer[(size_t)(160 + y) * FB_W + (size_t)x] = right;
        }
        // Exact WeaponLightData/TurnLight masks from Scores.asm. The original
        // panel paints every owned weapon yellow and the currently selected
        // weapon red; unowned digits are cleared to palette colour 0.
        static const int weaponOffsets[PLAYER_WEAPON_COUNT] = {
            36 + 7 * 40, 38 + 7 * 40, 36 + 19 * 40,
            38 + 19 * 40, 36 + 31 * 40, 38 + 31 * 40
        };
        static const unsigned char weaponMasks[PLAYER_WEAPON_COUNT][6] = {
            {16,16,16,16,16,0}, {60,4,60,32,60,0}, {60,4,28,4,60,0},
            {36,36,60,4,4,0}, {60,32,60,4,60,0}, {60,32,60,36,60,0}
        };
        for (int weapon = 0; weapon < PLAYER_WEAPON_COUNT; ++weapon) {
            const int byteOffset = weaponOffsets[weapon];
            const int baseX = (byteOffset % 40) * 8;
            const int baseY = byteOffset / 40;
            const unsigned int color = paletteColor(!gPlayerWeapons[weapon] ? 0 :
                                                     (weapon == gPlayerWeapon ? 62 : 195));
            for (int row = 0; row < 6; ++row) for (int bit = 0; bit < 8; ++bit) {
                if (weaponMasks[weapon][row] & (1u << (7 - bit)))
                    putPixelSafe(HUD_X + baseX + bit, 160 + baseY + row, color);
            }
        }
        // Original Scores.asm/PanelRefresh coordinates and original digit fonts.
        drawOriginalHudNumber(46, 20, std::min(9999999, gPlayerScore), 7, true);
        drawOriginalHudNumber(123, 18, std::min(999, gPlayerHealth), 3, false);
        drawOriginalHudNumber(151, 18, std::min(999, gPlayerShields), 3, false);
        drawOriginalHudNumber(177, 18, std::min(9999, gPlayerEnergy), 4, false);
        drawOriginalHudNumber(250, 20, std::min(99999, gPlayerCredits), 5, true);
    }
    // Exact visible 11x11 mask from Graphic/Mirino01.raw. The original uses
    // the hardware-sprite palette; render its active pixels in pure red.
    static const char* sightMask[11] = {
        "##.......##", "#.........#", ".....#.....", ".....#.....", "...........",
        "..##.#.##..", "...........", ".....#.....", ".....#.....", "#.........#", "##.......##"
    };
    const int sightX = FB_W / 2 - 5, sightY = VIEW_CENTER_Y - 5;
    for (int y = 0; y < 11; ++y) for (int x = 0; x < 11; ++x)
        if (sightMask[y][x] == '#') putPixelSafe(sightX + x, sightY + y, 0xff0000ffu);
}

static void drawRuntimeTerminal() {
    if (gRuntimeTerminalBackground.size() == gFramebuffer.size()) gFramebuffer = gRuntimeTerminalBackground;
    const unsigned int hover = 0xff604020u;
    const int selection = gRuntimeTerminalSelection;
    char line[64];
    snprintf(line, sizeof(line), "CONNECTED TO TERMINAL %d", gRuntimeTerminalNumber);
    drawTextCentered(17, line);

    const char* title = "MAIN PAGE";
    const char* const* entries = nullptr;
    int count = 0;
    static const char* mainEntries[] = {"WEAPONS", "WEAPONS BOOST", "ACCESSORIES", "EXIT"};
    static const char* weaponEntries[] = {
        "FIREBALLS       4000", "PLASMA GUN      9000", "FLAME-THROWER  13000",
        "MAGNETIC GUN   20000", "DEATH MACHINE  35000", "EXIT"
    };
    static const char* boostEntries[] = {
        "SIMPLE SHOT     1500", "FIREBALLS       2000", "PLASMA GUN      5000",
        "FLAME-THROWER    N/A", "MAGNETIC GUN   20000", "DEATH MACHINE  20000", "EXIT"
    };
    static const char* accessoryEntries[] = {
        "HEALTH +10       200", "SHIELDS +10      150", "ENERGY +100      200",
        "GREEN KEY       5000", "YELLOW KEY      5000", "RED KEY         5000",
        "BLUE KEY        5000", "EXIT"
    };
    if (gRuntimeTerminalPage == 0) { entries = mainEntries; count = 4; }
    else if (gRuntimeTerminalPage == 1) { title = "WEAPONS"; entries = weaponEntries; count = 6; }
    else if (gRuntimeTerminalPage == 2) { title = "WEAPONS BOOST"; entries = boostEntries; count = 7; }
    else { title = "ACCESSORIES"; entries = accessoryEntries; count = 8; }

    drawTextCentered(30, title);
    // Fixed top-origin layout. Seven rows fit without touching the fixed
    // credits line; longer pages follow the selection and scroll vertically.
    const int visibleRows = 7;
    const int first = std::max(0, std::min(std::max(0, count - visibleRows),
                                          selection - visibleRows + 1));
    const int end = std::min(count, first + visibleRows);
    for (int i = first; i < end; ++i) {
        const int y = 43 + (i - first) * 12;
        if (selection == i) fillRectSafe((FB_W - 166) / 2, y - 3, 166, 11, hover);
        drawTextCentered(y, entries[i]);
    }
    snprintf(line, sizeof(line), "CREDITS  %05d", std::max(0, std::min(99999, gPlayerCredits)));
    drawTextCentered(143, line);
}

static void updateAndDrawFrontend() {
    const double elapsed = nowSeconds() - gFrontendStateSince;
    if (gFrontendState == FRONTEND_LOGO1 && elapsed >= 2.5) setFrontendState(FRONTEND_LOGO2);
    if (gFrontendState == FRONTEND_LOGO2 && elapsed >= 3.5) setFrontendState(FRONTEND_TITLE);
    switch (gFrontendState) {
        case FRONTEND_LOGO1: drawPresentationPicture("LOG1"); break;
        case FRONTEND_LOGO2: drawPresentationPicture("LOG2"); break;
        case FRONTEND_TITLE:
            drawPresentationPicture("BTIT");
            drawPresentationTextCentered(116, "PRESS START");
            break;
        case FRONTEND_MENU: drawFrontendMenu(); break;
        case FRONTEND_SOUND: drawSoundMenu(); break;
        case FRONTEND_CONTROLS: drawControlsMenu(); break;
        case FRONTEND_GAME_OPTIONS: drawGameOptionsMenu(); break;
        case FRONTEND_CREDITS:
            drawPresentationPicture("CRED");
            break;
        case FRONTEND_LOADING: {
            const AssetInfo* level = currentLgldAsset();
            if (!level || !drawPresentationPicture(level->lgldLoadPic)) {
                std::fill(gPresentationFramebuffer.begin(), gPresentationFramebuffer.end(), 0xff000000u);
            }
            char levelName[80];
            int levelNumber = 0;
            if (level && level->name.size() >= 8u && strncasecmp(level->name.c_str(), "BLES", 4) == 0)
                levelNumber = atoi(level->name.substr(4, 4).c_str());
            if (levelNumber >= 6 && levelNumber <= 25) {
                static const char* worlds[] = {"FIRST WORLD -", "SECOND WORLD -", "THIRD WORLD -", "LAST WORLD -"};
                static const char* arenas[] = {"FIRST ARENA", "SECOND ARENA", "THIRD ARENA", "FOURTH ARENA", "FIFTH ARENA"};
                const int world = (levelNumber - 6) / 5;
                const int arena = (levelNumber - 6) % 5;
                snprintf(levelName, sizeof(levelName), "%s %s", worlds[world],
                         world == 3 && arena == 4 ? "LAST ARENA" : arenas[arena]);
            } else snprintf(levelName, sizeof(levelName), "BREATHLESS  %s", level ? level->name.c_str() : "LEVEL");
            drawPresentationTextCentered(7, levelName);
            char retries[32];
            snprintf(retries, sizeof(retries), "RETRIES LEFT  %d", std::max(0, gPlayerRetries));
            drawPresentationTextCentered(17, retries);
            drawPresentationTextCentered(220, "PRESS ANY KEY TO START");
            break;
        }
        case FRONTEND_GAME: break;
        case FRONTEND_PAUSE: drawPauseMenu(); break;
        case FRONTEND_TERMINAL: drawRuntimeTerminal(); break;
    }
}

static void applyOriginalRedHitPalette() {
    if (nowSeconds() >= gRedFlashUntil) return;
    for (size_t i = 0; i < gFramebuffer.size(); ++i) {
        const unsigned int pixel = gFramebuffer[i];
        const unsigned int r = pixel & 0xffu;
        const unsigned int g = (pixel >> 8) & 0xffu;
        const unsigned int b = (pixel >> 16) & 0xffu;
        const unsigned int intensity = std::min(255u, (r * 77u + g * 150u + b * 29u) >> 8);
        gFramebuffer[i] = 0xff000000u | intensity;
    }
}

static void applyEndLevelFade() {
    if (!gLevelExitActive) return;
    const float elapsed = (float)(nowSeconds() - gLevelExitStarted);
    const float brightness = std::max(0.0f, 1.0f - elapsed / 0.90f);
    for (size_t i = 0; i < gFramebuffer.size(); ++i) {
        const unsigned int pixel = gFramebuffer[i];
        const unsigned int r = (unsigned int)((float)(pixel & 0xffu) * brightness);
        const unsigned int g = (unsigned int)((float)((pixel >> 8) & 0xffu) * brightness);
        const unsigned int b = (unsigned int)((float)((pixel >> 16) & 0xffu) * brightness);
        gFramebuffer[i] = 0xff000000u | (b << 16) | (g << 8) | r;
    }
}

static bool completeEndLevelFade() {
    if (!gLevelExitActive || nowSeconds() - gLevelExitStarted < gLevelExitCompleteAfter) return false;
    gLevelExitActive = false;
    gLevelExitStarted = 0.0;
    gLevelExitCompleteAfter = 1.15;
    selectLevelRelative(1, "end-level");
    gRuntimeAssetIndex = -99999;
    gPlayerStartChecked = false;
    markGameProgressDirty();
    saveGameProgress();
    setFrontendState(FRONTEND_LOADING);
    return true;
}

static void drawFrame() {
    if (completeEndLevelFade()) {
        updateAndDrawFrontend();
    } else if (gFrontendState != FRONTEND_GAME) {
        updateAndDrawFrontend();
    } else if (currentLgldAsset()) {
        drawRoomRenderer();
        drawOriginalHud();
        if (nowSeconds() < gPickupMessageUntil) drawTextCentered(146, gPickupMessage.c_str());
        applyOriginalRedHitPalette();
        applyEndLevelFade();
        if (gFrontendState == FRONTEND_GAME) {
            std::lock_guard<std::mutex> lock(gGameSnapshotMutex);
            gLastGameFramebuffer = gFramebuffer;
            gHaveGameSnapshot = true;
        }
    } else {
        drawAssetExplorer();
    }
    maybeAutosaveGameProgress();
    updateFps();
    ++gFrame;
}


static void selectAssetRelative(int delta, const char* source) {
    if (delta == 0 || gAssetInfos.empty()) return;
    const int count = (int)gAssetInfos.size();
    int next = (gAssetIndex + delta) % count;
    if (next < 0) next += count;
    if (next != gAssetIndex) {
        gAssetIndex = next;
        gPlayerStartChecked = false;
        const AssetInfo& ai = gAssetInfos[(size_t)gAssetIndex];
        LOGI("asset browser source=%s idx=%d name=%s id=%s guess=%s size=%ld dir=%u count=%u first=%s unpack=%u uid=%s ulen=%u payload=%u blocks=%u edges=%u map=%u solid=%u tex=%u objs=%u load=%s",
             source ? source : "unknown", gAssetIndex, ai.name.c_str(), ai.id.c_str(), ai.guess.c_str(), ai.size, ai.dirOffset, ai.count, ai.firstEntry.c_str(),
             ai.unpackedSize, ai.unpackedId.c_str(), ai.lgldLength, ai.lgldPayloadBytes, ai.lgldBlocks, ai.lgldEdges, ai.lgldUsedCells, ai.lgldSolidCells,
             ai.lgldTextureNames, ai.lgldObjects, ai.lgldLoadPic.c_str());
    }
}

static void __attribute__((unused)) selectLevelRelative(int direction, const char* source) {
    if (direction == 0 || gAssetInfos.empty()) return;
    const int count = (int)gAssetInfos.size();
    const int wantedSteps = std::max(1, std::abs(direction));
    int foundSteps = 0;
    int next = gAssetIndex;
    for (int tries = 0; tries < count; ++tries) {
        next = (next + (direction > 0 ? 1 : -1) + count) % count;
        if (gAssetInfos[(size_t)next].lgldParseOk) {
            ++foundSteps;
            if (foundSteps >= wantedSteps) {
                const int delta = next - gAssetIndex;
                selectAssetRelative(delta, source);
                return;
            }
        }
    }
}

static bool isConfirmKey(int keyCode) {
    return keyCode == 23 || keyCode == 62 || keyCode == 66 || keyCode == 96 || keyCode == 108;
}

static bool isBackKey(int keyCode) {
    return keyCode == 4 || keyCode == 97 || keyCode == 111;
}

static void __attribute__((unused)) selectTextureRelative(int delta, const char* source) {
    if (delta == 0 || gTextureEntries.empty()) return;
    const int count = (int)gTextureEntries.size();
    int next = (gTextureIndex + delta) % count;
    if (next < 0) next += count;
    if (next != gTextureIndex) { gTextureIndex = next; LOGI("texture browser source=%s delta=%d", source ? source : "unknown", delta); loadCurrentTexture(); }
}

extern "C" JNIEXPORT void JNICALL Java_com_ast_breathlessamiga_MainActivity_nativeMixAudio(JNIEnv* env, jclass, jshortArray output) {
    if (!output) return;
    const jsize sampleCount = env->GetArrayLength(output);
    if (sampleCount <= 0) return;
    std::vector<jshort> mixed((size_t)sampleCount, 0);
    std::lock_guard<std::mutex> lock(gAudioMutex);
    if (!gSoundEnabled.load()) {
        gSoundVoices.clear();
        env->SetShortArrayRegion(output, 0, sampleCount, &mixed[0]);
        return;
    }
    const int frames = sampleCount / 2;
    for (int frame = 0; frame < frames; ++frame) {
        float left = 0.0f, right = 0.0f;
        for (size_t voiceIndex = 0; voiceIndex < gSoundVoices.size();) {
            SoundVoice& voice = gSoundVoices[voiceIndex];
            if (!voice.resource || voice.resource->pcm.empty() || voice.position >= voice.resource->pcm.size()) {
                gSoundVoices.erase(gSoundVoices.begin() + (long)voiceIndex);
                continue;
            }
            const int source = voice.resource->pcm[(size_t)voice.position];
            left += (float)source * 256.0f * voice.left;
            right += (float)source * 256.0f * voice.right;
            voice.position += (double)voice.resource->sampleRate / 44100.0;
            if (voice.position >= voice.resource->pcm.size() && voice.allowLoop &&
                voice.resource->loop > 1 &&
                voice.resource->loop < (int)voice.resource->pcm.size())
                voice.position = (double)voice.resource->loop;
            ++voiceIndex;
        }
        const float effectsScale = (float)std::max(1, std::min(5, gSoundVolume)) / 5.0f;
        left *= effectsScale;
        right *= effectsScale;
        mixed[(size_t)frame * 2u] = (jshort)std::max(-32768.0f, std::min(32767.0f, left));
        if ((size_t)frame * 2u + 1u < mixed.size())
            mixed[(size_t)frame * 2u + 1u] = (jshort)std::max(-32768.0f, std::min(32767.0f, right));
    }
    env->SetShortArrayRegion(output, 0, sampleCount, &mixed[0]);
}

extern "C" JNIEXPORT jstring JNICALL Java_com_ast_breathlessamiga_MainActivity_nativeMusicCode(JNIEnv* env, jclass) {
    std::string code;
    if (!gMusicEnabled) return env->NewStringUTF("");
    if (gFrontendState == FRONTEND_LOGO1 || gFrontendState == FRONTEND_LOGO2 ||
        gFrontendState == FRONTEND_TITLE || gFrontendState == FRONTEND_MENU ||
        gFrontendState == FRONTEND_SOUND || gFrontendState == FRONTEND_CONTROLS ||
        gFrontendState == FRONTEND_GAME_OPTIONS || gFrontendState == FRONTEND_CREDITS) {
        code = "MUST";
    } else if (gFrontendState == FRONTEND_GAME || gFrontendState == FRONTEND_LOADING ||
               gFrontendState == FRONTEND_PAUSE || gFrontendState == FRONTEND_TERMINAL) {
        code = "MUS1";
        const AssetInfo* level = currentLgldAsset();
        if (level) for (size_t i = 0; i < level->lgldSoundList.size(); ++i) {
            std::map<std::string, SoundResource>::const_iterator sound = gSoundResources.find(level->lgldSoundList[i]);
            if (sound != gSoundResources.end() && sound->second.type == 0 &&
                sound->first.size() == 4u && sound->first.compare(0, 3, "MUS") == 0 &&
                sound->first[3] >= '1' && sound->first[3] <= '5') { code = sound->first; break; }
        }
    }
    return env->NewStringUTF(code.c_str());
}

extern "C" JNIEXPORT jint JNICALL Java_com_ast_breathlessamiga_MainActivity_nativeMusicVolume(JNIEnv*, jclass) {
    return gMusicVolume;
}

extern "C" JNIEXPORT jboolean JNICALL Java_com_ast_breathlessamiga_MainActivity_nativeConsumeQuitRequest(JNIEnv*, jclass) {
    const bool requested = gQuitRequested;
    gQuitRequested = false;
    return requested ? (jboolean)1 : (jboolean)0;
}

extern "C" JNIEXPORT void JNICALL Java_com_ast_breathlessamiga_MainActivity_nativeSetDataPath(JNIEnv* env, jclass, jstring path) {
    const char* p = env->GetStringUTFChars(path, 0);
    gDataPath = p ? p : "";
    env->ReleaseStringUTFChars(path, p);
    scanGameData();
}

extern "C" JNIEXPORT void JNICALL Java_com_ast_breathlessamiga_MainActivity_nativeSaveProgress(JNIEnv*, jclass) {
    if (gPlayerProgressValid && !gPlayerDead) saveGameProgress();
}

extern "C" JNIEXPORT void JNICALL Java_com_ast_breathlessamiga_BreathlessRenderer_nativeSurfaceCreated(JNIEnv*, jclass) {
    // OpenGL object names belong to the old EGL context after standby/context
    // loss. Force ensureGl() to rebuild every GPU-side resource for the new one.
    gProgram = 0;
    gTexture = 0;
    gTextureWidth = gTextureHeight = 0;
    gVbo = 0;
    gPosLoc = -1;
    gUvLoc = -1;
    gTexLoc = -1;
    gScaleLoc = -1;
    gUvScaleLoc = -1;
}

extern "C" JNIEXPORT void JNICALL Java_com_ast_breathlessamiga_BreathlessRenderer_nativeInit(JNIEnv*, jclass, jint width, jint height) {
    gViewW = width; gViewH = height; ensureGl();
}

extern "C" JNIEXPORT void JNICALL Java_com_ast_breathlessamiga_BreathlessRenderer_nativeResize(JNIEnv*, jclass, jint width, jint height) {
    gViewW = width; gViewH = height; glViewport(0, 0, gViewW, gViewH);
}

extern "C" JNIEXPORT void JNICALL Java_com_ast_breathlessamiga_BreathlessRenderer_nativeRender(JNIEnv*, jclass) {
    ensureGl();
    drawFrame();
    glViewport(0, 0, gViewW, gViewH);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(gProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gTexture);
    const bool presentation = gFrontendState != FRONTEND_GAME && gFrontendState != FRONTEND_PAUSE &&
                              gFrontendState != FRONTEND_TERMINAL;
    const int textureWidth = presentation ? ORIGINAL_W : FB_W;
    const int textureHeight = presentation ? PRESENTATION_H : FB_H;
    const unsigned int* pixels = presentation ? &gPresentationFramebuffer[0] : &gFramebuffer[0];
    if (gTextureWidth != textureWidth || gTextureHeight != textureHeight) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, textureWidth, textureHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        gTextureWidth = textureWidth;
        gTextureHeight = textureHeight;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, textureWidth, textureHeight, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    }
    glUniform1i(gTexLoc, 0);
    float scaleX = 1.0f, scaleY = 1.0f;
    const float contentAspect = (float)textureWidth / (float)textureHeight;
    const float screenAspect = gViewH > 0 ? (float)gViewW / (float)gViewH : contentAspect;
    if (screenAspect > contentAspect) scaleX = contentAspect / screenAspect;
    else scaleY = screenAspect / contentAspect;
    if (gFrontendState == FRONTEND_CREDITS) {
        // Credits deliberately keep plain black side bars.
        glUniform2f(gScaleLoc, scaleX, scaleY);
        glUniform2f(gUvScaleLoc, 1.0f, 1.0f);
    } else {
        // Keep the original aspect in the centre, but fill wider/taller displays
        // by repeating the outermost pixel row/column instead of black bars.
        glUniform2f(gScaleLoc, 1.0f, 1.0f);
        glUniform2f(gUvScaleLoc, scaleX, scaleY);
    }
    glBindBuffer(GL_ARRAY_BUFFER, gVbo);
    glEnableVertexAttribArray(gPosLoc); glEnableVertexAttribArray(gUvLoc);
    glVertexAttribPointer(gPosLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (const void*)0);
    glVertexAttribPointer(gUvLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (const void*)(2 * sizeof(GLfloat)));
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

extern "C" JNIEXPORT void JNICALL Java_com_ast_breathlessamiga_MainActivity_nativeKey(JNIEnv*, jclass, jint keyCode, jboolean pressed) {
    LOGI("key code=%d pressed=%d", (int)keyCode, (int)pressed);
    // Physical shoulder chord; this observation does not consume or remap any
    // normal controller event.
    int shoulder = -1;
    if (keyCode == 102) shoulder = 0;      // L1
    else if (keyCode == 104) shoulder = 1; // L2
    else if (keyCode == 103) shoulder = 2; // R1
    else if (keyCode == 105) shoulder = 3; // R2
    if (shoulder >= 0) {
        gCheatShoulders[shoulder] = pressed != 0;
        const bool chord = gCheatShoulders[0] && gCheatShoulders[1] &&
                           gCheatShoulders[2] && gCheatShoulders[3];
        if (chord && !gCheatChordLatch && gFrontendState == FRONTEND_MENU) {
            applyGodModeLoadout();
            gGodModeMessageUntil = nowSeconds() + 3.0;
            gCheatChordLatch = true;
        } else if (!chord) gCheatChordLatch = false;
    }
    if (!pressed) {
        if (keyCode == gFireKey) {
            if (gFrontendState == FRONTEND_GAME && gFireHeld)
                gFireReleaseDeadline = nowSeconds() + 0.18;
            else {
                gFireHeld = false;
                gFireLatch = false;
                stopLoopingSoundGroup(1);
                gNextAutoFireTime = 0.0;
                gFireReleaseDeadline = 0.0;
            }
        }
        if (keyCode == gRunKey) gRunHeld = false;
        return;
    }
    if (gFrontendState == FRONTEND_CONTROLS && gControlCapture >= 0) {
        if (keyCode != 4 && keyCode != 111) {
            if (gControlCapture == 0) gFireKey = keyCode;
            else if (gControlCapture == 1) gActivateKey = keyCode;
            else if (gControlCapture == 2) gWeaponKey = keyCode;
            else if (gControlCapture == 3) gRunKey = keyCode;
            else if (gControlCapture == 4) gMenuKey = keyCode;
        }
        gControlCapture = -1;
        return;
    }
    if (gFrontendState == FRONTEND_LOGO1 || gFrontendState == FRONTEND_LOGO2) {
        if (isConfirmKey(keyCode) || keyCode == 4 || keyCode == 111) setFrontendState(FRONTEND_TITLE);
        return;
    }
    if (gFrontendState == FRONTEND_TITLE) {
        if (isConfirmKey(keyCode)) setFrontendState(FRONTEND_MENU);
        return;
    }
    if (gFrontendState == FRONTEND_CREDITS) {
        if (isBackKey(keyCode)) setFrontendState(FRONTEND_MENU);
        return;
    }
    if (gFrontendState == FRONTEND_GAME_OPTIONS) {
        if (isBackKey(keyCode)) setFrontendState(FRONTEND_MENU);
        else if (isConfirmKey(keyCode)) resetSavedGame();
        return;
    }
    if (gFrontendState == FRONTEND_LOADING) {
        gPlayerStartChecked = gRuntimeAssetIndex == gAssetIndex &&
            gRuntimeBlocks.size() == gAssetInfos[(size_t)gAssetIndex].lgldBlockData.size();
        gMoveLastTime = nowSeconds();
        setFrontendState(FRONTEND_GAME);
        return;
    }
    if (gFrontendState == FRONTEND_TERMINAL) {
        const int count = runtimeTerminalChoiceCount();
        if (keyCode == 19) gRuntimeTerminalSelection = (gRuntimeTerminalSelection + count - 1) % count;
        else if (keyCode == 20) gRuntimeTerminalSelection = (gRuntimeTerminalSelection + 1) % count;
        else if (isBackKey(keyCode) || keyCode == 82 || keyCode == gMenuKey || keyCode == 110) {
            if (gRuntimeTerminalPage == 0) closeRuntimeTerminal();
            else { gRuntimeTerminalPage = 0; gRuntimeTerminalSelection = 0; }
        } else if (isConfirmKey(keyCode)) selectRuntimeTerminalItem();
        return;
    }
    if (gFrontendState == FRONTEND_PAUSE) {
        if (keyCode == 19) gPauseMenuSelection = (gPauseMenuSelection + 2) % 3;
        else if (keyCode == 20) gPauseMenuSelection = (gPauseMenuSelection + 1) % 3;
        else if (isBackKey(keyCode) || keyCode == 82 || keyCode == gMenuKey || keyCode == 110)
            setFrontendState(FRONTEND_GAME);
        else if (isConfirmKey(keyCode)) {
            if (gPauseMenuSelection == 0) setFrontendState(FRONTEND_GAME);
            else if (gPauseMenuSelection == 1) {
                gRestoreLevelCheckpoint = true;
                gRuntimeAssetIndex = -99999;
                gPlayerStartChecked = false;
                setFrontendState(FRONTEND_GAME);
            } else {
                saveGameProgress();
                setFrontendState(FRONTEND_TITLE);
            }
        }
        return;
    }
    if (gFrontendState == FRONTEND_MENU) {
        if (keyCode == 19) gFrontendMenuSelection = (gFrontendMenuSelection + 5) % 6;
        else if (keyCode == 20) gFrontendMenuSelection = (gFrontendMenuSelection + 1) % 6;
        else if (isBackKey(keyCode)) setFrontendState(FRONTEND_TITLE);
        else if (isConfirmKey(keyCode)) {
            if (gFrontendMenuSelection == 0) startOrContinueGame();
            else if (gFrontendMenuSelection == 1) { gSoundMenuSelection = 0; setFrontendState(FRONTEND_SOUND); }
            else if (gFrontendMenuSelection == 2) { gControlsMenuSelection = 0; setFrontendState(FRONTEND_CONTROLS); }
            else if (gFrontendMenuSelection == 3) setFrontendState(FRONTEND_GAME_OPTIONS);
            else if (gFrontendMenuSelection == 4) setFrontendState(FRONTEND_CREDITS);
            else gQuitRequested = true;
        }
        return;
    }
    if (gFrontendState == FRONTEND_SOUND) {
        if (keyCode == 19) gSoundMenuSelection = (gSoundMenuSelection + 4) % 5;
        else if (keyCode == 20) gSoundMenuSelection = (gSoundMenuSelection + 1) % 5;
        else if (keyCode == 21 || keyCode == 22 || isConfirmKey(keyCode)) {
            if (gSoundMenuSelection == 0) {
                const int delta = keyCode == 21 ? -1 : 1;
                gSoundVolume = std::max(1, std::min(5, gSoundVolume + delta));
            } else if (gSoundMenuSelection == 1) {
                const int delta = keyCode == 21 ? -1 : 1;
                gMusicVolume = std::max(1, std::min(5, gMusicVolume + delta));
            } else if (gSoundMenuSelection == 2) {
                gSoundEnabled.store(!gSoundEnabled.load());
                if (!gSoundEnabled.load()) {
                    std::lock_guard<std::mutex> lock(gAudioMutex);
                    gSoundVoices.clear();
                }
            } else if (gSoundMenuSelection == 3) gMusicEnabled = !gMusicEnabled;
            else setFrontendState(FRONTEND_MENU);
        } else if (isBackKey(keyCode)) setFrontendState(FRONTEND_MENU);
        return;
    }
    if (gFrontendState == FRONTEND_CONTROLS) {
        if (keyCode == 19) gControlsMenuSelection = (gControlsMenuSelection + 5) % 6;
        else if (keyCode == 20) gControlsMenuSelection = (gControlsMenuSelection + 1) % 6;
        else if (isBackKey(keyCode)) setFrontendState(FRONTEND_MENU);
        else if (isConfirmKey(keyCode)) {
            if (gControlsMenuSelection < 5) gControlCapture = gControlsMenuSelection;
            else setFrontendState(FRONTEND_MENU);
        }
        return;
    }
    if (gLevelExitActive) return;
    if (isBackKey(keyCode) || keyCode == 82 || keyCode == gMenuKey || keyCode == 110) {
        gPauseMenuSelection = 0;
        gAnalogLX = gAnalogLY = gAnalogRX = gAnalogRY = 0.0f;
        gRunHeld = false;
        setFrontendState(FRONTEND_PAUSE);
        return;
    }
    if (keyCode == gFireKey) {
        // Some Android gamepads emit short DOWN/UP pulses while a trigger is
        // held. Only the first DOWN starts the cadence; repeats neither fire an
        // extra shot nor postpone the next timed autofire shot.
        if (!gFireHeld) {
            playerFireWeapon();
            gNextAutoFireTime = nowSeconds() + AUTO_FIRE_INTERVAL;
        }
        gFireHeld = true;
        gFireLatch = true;
        gFireReleaseDeadline = 0.0;
    }
    else if (keyCode == gRunKey && !gPlayerDead) gRunHeld = true;
    else if (keyCode == gWeaponKey) {
        for (int step = 1; step <= PLAYER_WEAPON_COUNT; ++step) {
            const int candidate = (gPlayerWeapon + step) % PLAYER_WEAPON_COUNT;
            if (gPlayerWeapons[candidate]) {
                stopSoundGroup(1);
                gPlayerWeapon = candidate;
                gPickupMessage = "WEAPON SELECTED";
                gPickupMessageUntil = nowSeconds() + 1.0;
                markGameProgressDirty();
                break;
            }
        }
    }
    else if (keyCode == gActivateKey && !gPlayerDead) activateSwitchInFront();
}

extern "C" JNIEXPORT void JNICALL Java_com_ast_breathlessamiga_MainActivity_nativeAnalog(JNIEnv*, jclass, jfloat lx, jfloat ly, jfloat rx, jfloat ry, jfloat hatX, jfloat hatY) {
    gAnalogLX = fabsf(lx) < 0.12f && fabsf(hatX) > 0.5f ? hatX : lx;
    gAnalogLY = fabsf(ly) < 0.12f && fabsf(hatY) > 0.5f ? hatY : ly;
    gAnalogRX = rx;
    gAnalogRY = ry;

    static double lastAnalogLog = 0.0;
    const double t = nowSeconds();
    const bool analogActive = fabsf(lx) > 0.05f || fabsf(ly) > 0.05f || fabsf(rx) > 0.05f || fabsf(ry) > 0.05f || fabsf(hatX) > 0.05f || fabsf(hatY) > 0.05f;
    if (analogActive && (t - lastAnalogLog) > 0.50) {
        LOGI("analog v64 column-height-anchor lx=%0.3f ly=%0.3f rx=%0.3f ry=%0.3f hat=%0.3f,%0.3f", (double)lx, (double)ly, (double)rx, (double)ry, (double)hatX, (double)hatY);
        lastAnalogLog = t;
    }

    static int lastHatX = 0, lastHatY = 0;
    const int hx = (hatX > 0.5f) ? 1 : ((hatX < -0.5f) ? -1 : 0);
    const int hy = (hatY > 0.5f) ? 1 : ((hatY < -0.5f) ? -1 : 0);
    if (hx != lastHatX) {
        if (gFrontendState == FRONTEND_SOUND && hx != 0) {
            if (gSoundMenuSelection == 0) gSoundVolume = std::max(1, std::min(5, gSoundVolume + hx));
            else if (gSoundMenuSelection == 1) gMusicVolume = std::max(1, std::min(5, gMusicVolume + hx));
            else if (gSoundMenuSelection == 2) {
                gSoundEnabled.store(!gSoundEnabled.load());
                if (!gSoundEnabled.load()) {
                    std::lock_guard<std::mutex> lock(gAudioMutex);
                    gSoundVoices.clear();
                }
            } else if (gSoundMenuSelection == 3) gMusicEnabled = !gMusicEnabled;
        }
        lastHatX = hx;
    }
    if (hy != lastHatY) {
        if (gFrontendState == FRONTEND_MENU) {
            if (hy > 0) gFrontendMenuSelection = (gFrontendMenuSelection + 1) % 6;
            else if (hy < 0) gFrontendMenuSelection = (gFrontendMenuSelection + 5) % 6;
        } else if (gFrontendState == FRONTEND_SOUND) {
            if (hy > 0) gSoundMenuSelection = (gSoundMenuSelection + 1) % 5;
            else if (hy < 0) gSoundMenuSelection = (gSoundMenuSelection + 4) % 5;
        } else if (gFrontendState == FRONTEND_CONTROLS) {
            if (hy > 0) gControlsMenuSelection = (gControlsMenuSelection + 1) % 6;
            else if (hy < 0) gControlsMenuSelection = (gControlsMenuSelection + 5) % 6;
        } else if (gFrontendState == FRONTEND_PAUSE) {
            if (hy > 0) gPauseMenuSelection = (gPauseMenuSelection + 1) % 3;
            else if (hy < 0) gPauseMenuSelection = (gPauseMenuSelection + 2) % 3;
        } else if (gFrontendState == FRONTEND_TERMINAL) {
            const int count = runtimeTerminalChoiceCount();
            if (hy > 0) gRuntimeTerminalSelection = (gRuntimeTerminalSelection + 1) % count;
            else if (hy < 0) gRuntimeTerminalSelection = (gRuntimeTerminalSelection + count - 1) % count;
        }
        lastHatY = hy;
    }
}

extern "C" JNIEXPORT void JNICALL Java_com_ast_breathlessamiga_MainActivity_nativeTouch(JNIEnv*, jclass, jfloat x, jfloat y, jint action) {
    if (action != 1) return; // ACTION_UP
    if (gFrontendState == FRONTEND_LOGO1 || gFrontendState == FRONTEND_LOGO2) {
        setFrontendState(FRONTEND_TITLE);
    } else if (gFrontendState == FRONTEND_TITLE) {
        setFrontendState(FRONTEND_MENU);
    } else if (gFrontendState == FRONTEND_CREDITS) {
        setFrontendState(FRONTEND_MENU);
    } else if (gFrontendState == FRONTEND_GAME_OPTIONS) {
        const float virtualY = gViewH > 0 ? y * (float)PRESENTATION_H / (float)gViewH : y;
        if (virtualY >= 154.0f && virtualY < 180.0f) resetSavedGame();
        else setFrontendState(FRONTEND_MENU);
    } else if (gFrontendState == FRONTEND_LOADING) {
        gPlayerStartChecked = gRuntimeAssetIndex == gAssetIndex &&
            gRuntimeBlocks.size() == gAssetInfos[(size_t)gAssetIndex].lgldBlockData.size();
        gMoveLastTime = nowSeconds();
        setFrontendState(FRONTEND_GAME);
    } else if (gFrontendState == FRONTEND_MENU) {
        const float virtualY = gViewH > 0 ? y * (float)PRESENTATION_H / (float)gViewH : y;
        if (virtualY >= 91.0f && virtualY < 182.0f) {
            gFrontendMenuSelection = std::max(0, std::min(5, (int)((virtualY - 91.0f) / 14.0f)));
            if (gFrontendMenuSelection == 0) startOrContinueGame();
            else if (gFrontendMenuSelection == 1) setFrontendState(FRONTEND_SOUND);
            else if (gFrontendMenuSelection == 2) setFrontendState(FRONTEND_CONTROLS);
            else if (gFrontendMenuSelection == 3) setFrontendState(FRONTEND_GAME_OPTIONS);
            else if (gFrontendMenuSelection == 4) setFrontendState(FRONTEND_CREDITS);
            else gQuitRequested = true;
        }
    } else if (gFrontendState == FRONTEND_SOUND || gFrontendState == FRONTEND_CONTROLS) {
        setFrontendState(FRONTEND_MENU);
    } else if (gFrontendState == FRONTEND_PAUSE) {
        const float virtualY = gViewH > 0 ? y * (float)FB_H / (float)gViewH : y;
        if (virtualY >= 71.0f && virtualY < 113.0f) {
            gPauseMenuSelection = std::max(0, std::min(2, (int)((virtualY - 71.0f) / 14.0f)));
            if (gPauseMenuSelection == 0) setFrontendState(FRONTEND_GAME);
            else if (gPauseMenuSelection == 1) {
                gRestoreLevelCheckpoint = true;
                gRuntimeAssetIndex = -99999;
                gPlayerStartChecked = false;
                setFrontendState(FRONTEND_GAME);
            } else {
                saveGameProgress();
                setFrontendState(FRONTEND_TITLE);
            }
        }
    } else if (gFrontendState == FRONTEND_TERMINAL) {
        closeRuntimeTerminal();
    } else if (gFrontendState == FRONTEND_GAME) {
        if (gPlayerDead || gLevelExitActive) return;
        const float virtualX = gViewW > 0 ? x * (float)FB_W / (float)gViewW : x;
        if (virtualX >= FB_W * 0.5f) playerFireWeapon();
        else activateSwitchInFront();
    }
}
