// DSi Video Player - passo 1 (file unico: parser MP4 + browser + info)
#ifndef MP4_H
#define MP4_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    // Video
    bool     has_video;
    bool     video_ok;        // codec che il DSi riesce a decodificare
    char     vcodec[5];       // fourcc del sample entry (es. "jpeg", "avc1")
    char     vlabel[20];      // nome leggibile (es. "MJPEG")
    uint8_t  video_oti;       // objectTypeIndication da esds (0 se assente)
    uint32_t width, height;
    uint32_t frames;
    uint32_t fps_x100;        // fps * 100

    // Audio
    bool     has_audio;
    bool     audio_ok;
    char     acodec[5];
    char     alabel[20];
    uint8_t  audio_oti;
    uint32_t sample_rate;
    uint32_t channels;

    uint32_t duration_ms;
} Mp4Info;

// Legge solo le tabelle (moov), non carica il file in RAM.
// Ritorna 0 se ok, <0 in caso di errore.
int mp4_probe(const char *path, Mp4Info *out);

#endif
// Parser minimale MP4/MOV: legge solo i metadati (moov) in streaming dalla SD.
// Limite attuale: offset a 32 bit (file < 2 GB), piu' che sufficiente per il DSi.

#include <stdio.h>
#include <string.h>

#define FCC(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | \
                         ((uint32_t)(c) << 8) | (uint32_t)(d))

static uint32_t rd32(FILE *f)
{
    uint8_t b[4] = {0, 0, 0, 0};
    fread(b, 1, 4, f);
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
}

static uint16_t rd16(FILE *f)
{
    uint8_t b[2] = {0, 0};
    fread(b, 1, 2, f);
    return (uint16_t)((b[0] << 8) | b[1]);
}

static void fcc_str(uint32_t v, char *out)
{
    out[0] = (char)(v >> 24);
    out[1] = (char)(v >> 16);
    out[2] = (char)(v >> 8);
    out[3] = (char)v;
    out[4] = 0;
}

typedef struct {
    uint32_t handler;
    uint32_t timescale;
    uint64_t duration;
    uint32_t samples;
    char     codec[5];
    uint32_t w, h, rate, ch;
    uint8_t  oti;
} Trk;

// Cerca il box 'esds' tra i figli del sample entry e ritorna objectTypeIndication.
static uint8_t read_esds_oti(FILE *f, uint32_t pos, uint32_t end)
{
    while (pos + 8 <= end) {
        fseek(f, pos, SEEK_SET);
        uint32_t size = rd32(f);
        uint32_t type = rd32(f);
        if (size < 8) break;
        if (type == FCC('e', 's', 'd', 's')) {
            uint8_t b[32] = {0};
            fseek(f, pos + 8 + 4, SEEK_SET);   // salta version/flags
            fread(b, 1, sizeof(b), f);
            int i = 0;
            if (b[i] == 0x03) {                // ES_Descriptor
                i++;
                while (i < 28 && (b[i] & 0x80)) i++;
                i++;
                i += 3;                        // ES_ID (2) + flags (1)
            }
            if (i < 28 && b[i] == 0x04) {      // DecoderConfigDescriptor
                i++;
                while (i < 28 && (b[i] & 0x80)) i++;
                i++;
                if (i < 32) return b[i];
            }
            return 0;
        }
        pos += size;
    }
    return 0;
}

static void commit_track(Mp4Info *info, const Trk *t)
{
    uint32_t ms = 0;
    if (t->timescale)
        ms = (uint32_t)((t->duration * 1000ULL) / t->timescale);

    if (t->handler == FCC('v', 'i', 'd', 'e') && !info->has_video) {
        info->has_video = true;
        strcpy(info->vcodec, t->codec);
        info->video_oti = t->oti;
        info->width = t->w;
        info->height = t->h;
        info->frames = t->samples;
        if (ms) info->fps_x100 = (uint32_t)(((uint64_t)t->samples * 100000ULL) / ms);
        if (ms > info->duration_ms) info->duration_ms = ms;
    } else if (t->handler == FCC('s', 'o', 'u', 'n') && !info->has_audio) {
        info->has_audio = true;
        strcpy(info->acodec, t->codec);
        info->audio_oti = t->oti;
        info->sample_rate = t->rate;
        info->channels = t->ch;
        if (ms > info->duration_ms) info->duration_ms = ms;
    }
}

static void walk(FILE *f, uint32_t pos, uint32_t end, Mp4Info *info, Trk *t)
{
    while (pos + 8 <= end) {
        fseek(f, pos, SEEK_SET);
        uint32_t size = rd32(f);
        uint32_t type = rd32(f);
        uint32_t hdr = 8;

        if (size == 1) {            // largesize a 64 bit: usiamo solo i 32 bit bassi
            rd32(f);
            size = rd32(f);
            hdr = 16;
        } else if (size == 0) {     // il box arriva fino a fine contenitore
            size = end - pos;
        }
        if (size < hdr || pos + size > end) break;

        uint32_t body = pos + hdr;
        uint32_t bend = pos + size;

        switch (type) {
        case FCC('m', 'o', 'o', 'v'):
        case FCC('m', 'd', 'i', 'a'):
        case FCC('m', 'i', 'n', 'f'):
        case FCC('s', 't', 'b', 'l'):
            walk(f, body, bend, info, t);
            break;

        case FCC('t', 'r', 'a', 'k'): {
            Trk nt;
            memset(&nt, 0, sizeof(nt));
            walk(f, body, bend, info, &nt);
            commit_track(info, &nt);
            break;
        }

        case FCC('m', 'd', 'h', 'd'): {
            fseek(f, body, SEEK_SET);
            uint32_t vf = rd32(f);
            if ((vf >> 24) == 1) {
                rd32(f); rd32(f); rd32(f); rd32(f);   // creation + modification (64+64)
                t->timescale = rd32(f);
                uint32_t hi = rd32(f);
                uint32_t lo = rd32(f);
                t->duration = ((uint64_t)hi << 32) | lo;
            } else {
                rd32(f); rd32(f);
                t->timescale = rd32(f);
                t->duration = rd32(f);
            }
            break;
        }

        case FCC('h', 'd', 'l', 'r'):
            // Il primo hdlr (in mdia) e' quello della traccia; i file QuickTime
            // ne hanno un secondo in minf (data handler) che va ignorato.
            if (t->handler == 0) {
                fseek(f, body + 8, SEEK_SET);
                t->handler = rd32(f);
            }
            break;

        case FCC('s', 't', 's', 'z'):
            fseek(f, body + 8, SEEK_SET);
            t->samples = rd32(f);
            break;

        case FCC('s', 't', 's', 'd'): {
            uint32_t e = body + 8;                 // primo sample entry
            fseek(f, e, SEEK_SET);
            uint32_t esz = rd32(f);
            uint32_t fmt = rd32(f);
            fcc_str(fmt, t->codec);
            if (esz < 16) break;
            uint32_t eend = e + esz;
            if (eend > bend) eend = bend;

            if (t->handler == FCC('v', 'i', 'd', 'e')) {
                fseek(f, e + 16 + 16, SEEK_SET);
                t->w = rd16(f);
                t->h = rd16(f);
                t->oti = read_esds_oti(f, e + 8 + 78, eend);
            } else if (t->handler == FCC('s', 'o', 'u', 'n')) {
                fseek(f, e + 16 + 8, SEEK_SET);
                t->ch = rd16(f);
                rd16(f); rd16(f); rd16(f);
                t->rate = rd32(f) >> 16;
                t->oti = read_esds_oti(f, e + 8 + 28, eend);
            }
            break;
        }

        default:
            break;
        }

        pos += size;
    }
}

static void label_codecs(Mp4Info *info)
{
    // --- Video ---
    const char *v = info->vcodec;
    if (!strcmp(v, "jpeg") || !strcmp(v, "mjpa") || !strcmp(v, "mjpb") ||
        (!strcmp(v, "mp4v") && info->video_oti == 0x6C)) {
        strcpy(info->vlabel, "MJPEG");
        info->video_ok = true;
    } else if (!strcmp(v, "avc1") || !strcmp(v, "avc3")) {
        strcpy(info->vlabel, "H.264");
    } else if (!strcmp(v, "hvc1") || !strcmp(v, "hev1")) {
        strcpy(info->vlabel, "H.265");
    } else if (!strcmp(v, "mp4v")) {
        strcpy(info->vlabel, "MPEG-4 Part 2");
    } else {
        strcpy(info->vlabel, v);
    }

    // --- Audio ---
    const char *a = info->acodec;
    if (!strcmp(a, "ima4")) {
        strcpy(info->alabel, "IMA ADPCM");
        info->audio_ok = true;
    } else if (!strcmp(a, "sowt") || !strcmp(a, "twos") || !strcmp(a, "ipcm") ||
               !strcmp(a, "lpcm") || !strcmp(a, "raw ")) {
        strcpy(info->alabel, "PCM");
        info->audio_ok = true;
    } else if (!strcmp(a, "mp4a")) {
        if (info->audio_oti == 0x6B || info->audio_oti == 0x69)
            strcpy(info->alabel, "MP3");
        else
            strcpy(info->alabel, "AAC");
    } else {
        strcpy(info->alabel, a);
    }
}

int mp4_probe(const char *path, Mp4Info *out)
{
    memset(out, 0, sizeof(*out));

    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    if (len < 16) { fclose(f); return -2; }

    Trk dummy;
    memset(&dummy, 0, sizeof(dummy));
    walk(f, 0, (uint32_t)len, out, &dummy);
    fclose(f);

    if (!out->has_video && !out->has_audio) return -3;   // moov non trovato

    label_codecs(out);
    return 0;
}
// DSi Video Player - passo 1: browser file + info video (parser MP4/MOV)

#include <nds.h>
#include <fat.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>


#define MAX_FILES  128
#define VISIBLE    18

static char names[MAX_FILES][64];
static int  count;

static bool is_video_file(const char *n)
{
    const char *e = strrchr(n, '.');
    return e && (!strcasecmp(e, ".mp4") || !strcasecmp(e, ".m4v") || !strcasecmp(e, ".mov"));
}

static void scan_dir(void)
{
    count = 0;
    DIR *d = opendir(".");
    if (!d) return;

    struct dirent *ent;
    while ((ent = readdir(d)) && count < MAX_FILES) {
        if (ent->d_type == DT_DIR) continue;
        if (!is_video_file(ent->d_name)) continue;
        strncpy(names[count], ent->d_name, 63);
        names[count][63] = 0;
        count++;
    }
    closedir(d);
}

static void draw_list(int sel, int top)
{
    consoleClear();
    printf("DSi Video Player\n");
    printf("--------------------------------\n");
    if (count == 0) {
        printf("\nNessun file .mp4/.mov nella\ncartella corrente.\n");
        return;
    }
    for (int i = top; i < count && i < top + VISIBLE; i++)
        printf("%c %.29s\n", i == sel ? '>' : ' ', names[i]);
    printf("\nSU/GIU: scegli   A: info\n");
}

static void draw_info(const char *name)
{
    Mp4Info info;
    int rc = mp4_probe(name, &info);

    consoleClear();
    printf("%.32s\n", name);
    printf("--------------------------------\n");

    if (rc < 0) {
        printf("\nErrore lettura file (%d)\n", rc);
    } else {
        if (info.has_video) {
            printf("Video: %s (%s)\n", info.vlabel, info.vcodec);
            printf("Risoluz.: %lux%lu\n", info.width, info.height);
            printf("FPS: %lu.%02lu  Frame: %lu\n",
                   info.fps_x100 / 100, info.fps_x100 % 100, info.frames);
        } else {
            printf("Nessuna traccia video\n");
        }
        printf("\n");
        if (info.has_audio) {
            printf("Audio: %s (%s)\n", info.alabel, info.acodec);
            printf("%lu Hz, %lu canali\n", info.sample_rate, info.channels);
        } else {
            printf("Nessuna traccia audio\n");
        }

        unsigned s = info.duration_ms / 1000;
        printf("\nDurata: %u:%02u\n", s / 60, s % 60);

        printf("\n--------------------------------\n");
        if (info.video_ok && (info.audio_ok || !info.has_audio))
            printf("COMPATIBILE col lettore\n");
        else {
            if (!info.video_ok) printf("Video NON supportato\n");
            if (info.has_audio && !info.audio_ok) printf("Audio NON supportato\n");
            printf("Converti con convert.py\n");
        }
    }
    printf("\nB: indietro\n");
}

int main(void)
{
    consoleDemoInit();

    if (!fatInitDefault()) {
        printf("Impossibile montare la SD.\n");
        while (1) swiWaitForVBlank();
    }

    scan_dir();

    int sel = 0, top = 0;
    bool in_info = false;
    draw_list(sel, top);

    while (1) {
        swiWaitForVBlank();
        scanKeys();
        u32 down = keysDown();

        if (!in_info) {
            if ((down & KEY_DOWN) && sel + 1 < count) sel++;
            if ((down & KEY_UP) && sel > 0) sel--;
            if (sel < top) top = sel;
            if (sel >= top + VISIBLE) top = sel - VISIBLE + 1;

            if (down & (KEY_UP | KEY_DOWN)) draw_list(sel, top);

            if ((down & KEY_A) && count > 0) {
                in_info = true;
                draw_info(names[sel]);
            }
        } else if (down & KEY_B) {
            in_info = false;
            draw_list(sel, top);
        }
    }
    return 0;
}
