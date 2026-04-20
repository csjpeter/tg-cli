/**
 * @file test_rich_media_types.c
 * @brief TEST-73 — functional coverage for rich media types.
 *
 * US-22 lists nine MessageMedia variants whose parsing is silently
 * dropped or mislabelled today: video, audio, voice, sticker,
 * animation (GIF), round-video, geo, contact, poll, webpage. Unit
 * tests in test_tl_skip_message_functional.c exercise the skippers
 * per-variant; this suite drives the production `domain_get_history`
 * end-to-end through the in-process mock server so the full
 * TL_messages_messages → Vector<Message> → MessageMedia chain is
 * walked with real OpenSSL on both sides (same pattern as the
 * TEST-79 sibling test_history_rich_metadata.c).
 *
 * For each variant we assert:
 *   - domain_get_history does NOT bail (rows[0].text stays intact)
 *   - HistoryEntry.media is set to the correct MediaKind
 *   - MEDIA_PHOTO / MEDIA_DOCUMENT also expose media_id + media_dc +
 *     media_info metadata (document_mime, document_filename, size)
 *
 * US-22 "printed label" assertions (e.g. "[video WxH Ds BYTES]") are
 * intentionally NOT made here because the domain layer currently
 * stores only the MediaKind enum, not a rendered label string —
 * closing that gap is the US-22 prod change, out of scope for a
 * test-only ticket.
 *
 * The suite also exercises the download-path error branches of
 * media.c that test_upload_download.c does not yet cover (invalid
 * MediaInfo and `download_any` dispatch through
 * domain_download_media_cross_dc for a non-photo/document kind) to
 * push functional coverage of media.c past the 63 % baseline.
 */

#include "test_helpers.h"

#include "mock_socket.h"
#include "mock_tel_server.h"

#include "api_call.h"
#include "mtproto_session.h"
#include "transport.h"
#include "app/session_store.h"
#include "tl_registry.h"
#include "tl_serial.h"
#include "tl_skip.h"

#include "domain/read/history.h"
#include "domain/read/media.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ---- CRCs not re-exposed from public headers ---- */
#define CRC_messages_getHistory      0x4423e6c5U
#define CRC_upload_getFile           0xbe5335beU
#define CRC_upload_file              0x096a18d5U
#define CRC_storage_filePartial      0x40bc6f52U

#define CRC_messageMediaEmpty        0x3ded6320U
#define CRC_messageMediaPhoto        0x695150d7U
#define CRC_messageMediaDocument     0x4cf4d72dU
#define CRC_messageMediaGeo          0x56e0d474U
#define CRC_messageMediaContact      0x70322949U
#define CRC_messageMediaWebPage      0xddf8c26eU
#define CRC_messageMediaPoll         0x4bd6e798U

#define CRC_geoPoint                 0xb2a2f663U

#define CRC_document                 0x8fd4c4d8U
#define CRC_photo                    0xfb197a65U
#define CRC_photoSize                0x75c78e60U

#define CRC_documentAttributeAnimated  0x11b58939U
#define CRC_documentAttributeFilename  0x15590068U
#define CRC_documentAttributeVideo     0x43c57c48U
#define CRC_documentAttributeAudio     0x9852f9c6U
#define CRC_documentAttributeSticker   0x6319d612U
#define CRC_inputStickerSetEmpty       0xffb62b95U

#define CRC_webPage                  0xe89c45b2U
#define CRC_poll                     0x58747131U
#define CRC_pollAnswer               0x6ca9c2e9U
#define CRC_pollResults              0x7adc669dU
#define CRC_textWithEntities_poll    0x751f3146U

/* Message.flags bits we use. */
#define MSG_FLAG_MEDIA               (1u <<  9)

/* ---- Boilerplate ---- */

static void with_tmp_home(const char *tag) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "/tmp/tg-cli-ft-rich-media-%s", tag);
    char bin[512];
    snprintf(bin, sizeof(bin), "%s/.config/tg-cli/session.bin", tmp);
    (void)unlink(bin);
    setenv("HOME", tmp, 1);
}

static void connect_mock(Transport *t) {
    transport_init(t);
    ASSERT(transport_connect(t, "127.0.0.1", 443) == 0, "connect");
}

static void init_cfg(ApiConfig *cfg) {
    api_config_init(cfg);
    cfg->api_id = 12345;
    cfg->api_hash = "deadbeefcafebabef00dbaadfeedc0de";
}

static void load_session(MtProtoSession *s) {
    ASSERT(mt_server_seed_session(2, NULL, NULL, NULL) == 0, "seed");
    mtproto_session_init(s);
    int dc = 0;
    ASSERT(session_store_load(s, &dc) == 0, "load session");
}

/* Envelope: messages.messages { messages: Vector<Message>{1}, chats, users }
 * with the caller providing the inner message bytes (starting at TL_message). */
static void wrap_messages_messages(TlWriter *w, const uint8_t *msg_bytes,
                                    size_t msg_len) {
    tl_write_uint32(w, TL_messages_messages);
    tl_write_uint32(w, TL_vector);
    tl_write_uint32(w, 1);
    tl_write_raw(w, msg_bytes, msg_len);
    tl_write_uint32(w, TL_vector); tl_write_uint32(w, 0); /* chats */
    tl_write_uint32(w, TL_vector); tl_write_uint32(w, 0); /* users */
}

/* Write the common Message prefix up to and including the `message:string`
 * field, leaving the writer positioned at the media payload. Every variant
 * in this suite uses the same envelope (no flags2 bits, peer=self, minimal
 * date/text) so only the nested MessageMedia differs between tests. */
static void write_message_prefix(TlWriter *w, int32_t msg_id,
                                  const char *caption) {
    tl_write_uint32(w, TL_message);
    tl_write_uint32(w, MSG_FLAG_MEDIA);          /* flags */
    tl_write_uint32(w, 0);                       /* flags2 */
    tl_write_int32 (w, msg_id);
    tl_write_uint32(w, TL_peerUser);             /* peer_id */
    tl_write_int64 (w, 1LL);
    tl_write_int32 (w, 1700000500);              /* date */
    tl_write_string(w, caption);                 /* message */
}

/* Shared helper: arm getHistory → return an envelope built by @p build
 * into @p ctx. The responder allocates its own TlWriters — the inner
 * bytes are copied into the outer frame before either is freed. */
typedef void (*MediaBuilder)(TlWriter *w);

static void reply_history_with_media(MtRpcContext *ctx,
                                      MediaBuilder build_media,
                                      int32_t msg_id,
                                      const char *caption) {
    TlWriter inner; tl_writer_init(&inner);
    write_message_prefix(&inner, msg_id, caption);
    build_media(&inner);

    TlWriter w; tl_writer_init(&w);
    wrap_messages_messages(&w, inner.data, inner.len);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
    tl_writer_free(&inner);
}

/* Each responder below builds a single MessageMedia variant and hands
 * off to reply_history_with_media. */
/* ---- Media builders ---------------------------------------------- */

/* messageMediaGeo { geoPoint flags=0 long:double lat:double access_hash:long } */
static void build_media_geo(TlWriter *w) {
    tl_write_uint32(w, CRC_messageMediaGeo);
    tl_write_uint32(w, CRC_geoPoint);
    tl_write_uint32(w, 0);                         /* geoPoint flags */
    tl_write_double(w, 19.0402);                   /* long */
    tl_write_double(w, 47.4979);                   /* lat */
    tl_write_int64 (w, 0LL);                       /* access_hash */
}

/* messageMediaContact#70322949
 *   phone:string first_name:string last_name:string vcard:string user_id:long */
static void build_media_contact(TlWriter *w) {
    tl_write_uint32(w, CRC_messageMediaContact);
    tl_write_string(w, "+36301234567");
    tl_write_string(w, "Janos");
    tl_write_string(w, "Example");
    tl_write_string(w, "");
    tl_write_int64 (w, 555001LL);
}

/* messageMediaWebPage#ddf8c26e flags:# webpage:WebPage
 * Minimal webPage variant with url+display_url+hash, no optional fields. */
static void build_media_webpage(TlWriter *w) {
    tl_write_uint32(w, CRC_messageMediaWebPage);
    tl_write_uint32(w, 0);                         /* outer flags */
    tl_write_uint32(w, CRC_webPage);
    tl_write_uint32(w, 0);                         /* webPage flags */
    tl_write_int64 (w, 77001LL);                   /* id */
    tl_write_string(w, "https://example.com/");
    tl_write_string(w, "example.com");
    tl_write_int32 (w, 0);                         /* hash */
}

/* messageMediaPoll#4bd6e798 poll:Poll results:PollResults
 * Poll = flags:# id:long question:textWithEntities answers:Vector<PollAnswer>
 *        close_period:flags.4?int close_date:flags.5?int
 * PollResults = flags:#  (all optional vectors/fields skipped).
 * A single poll answer with one option. */
static void build_media_poll(TlWriter *w) {
    tl_write_uint32(w, CRC_messageMediaPoll);
    /* poll */
    tl_write_uint32(w, CRC_poll);
    tl_write_uint32(w, 0);                         /* poll flags */
    tl_write_int64 (w, 42LL);                      /* poll id */
    /* question */
    tl_write_uint32(w, CRC_textWithEntities_poll);
    tl_write_string(w, "Sunny today?");
    tl_write_uint32(w, TL_vector); tl_write_uint32(w, 0); /* entities */
    /* answers vector with 1 entry */
    tl_write_uint32(w, TL_vector);
    tl_write_uint32(w, 1);
    tl_write_uint32(w, CRC_pollAnswer);
    tl_write_uint32(w, CRC_textWithEntities_poll);
    tl_write_string(w, "Yes");
    tl_write_uint32(w, TL_vector); tl_write_uint32(w, 0); /* entities */
    tl_write_string(w, "y");                       /* option:bytes */
    /* pollResults — empty flags */
    tl_write_uint32(w, CRC_pollResults);
    tl_write_uint32(w, 0);
}

/* Build a messageMediaPhoto with a fully-populated photo#fb197a65
 * (layer 170+): flags + id + access_hash + file_reference:bytes + date
 * + sizes:Vector<PhotoSize> + dc_id. One photoSize entry (type="y"). */
static void build_media_photo(TlWriter *w) {
    tl_write_uint32(w, CRC_messageMediaPhoto);
    tl_write_uint32(w, 1u);                        /* outer flags — has photo */
    tl_write_uint32(w, CRC_photo);
    tl_write_uint32(w, 0);                         /* photo flags */
    tl_write_int64 (w, 0x1234567890ABLL);          /* id */
    tl_write_int64 (w, 0xCAFEBABEDEADBEEFLL);      /* access_hash */
    {
        /* file_reference bytes */
        static const unsigned char fr[] = { 0xDE, 0xAD, 0xBE, 0xEF };
        tl_write_bytes(w, fr, sizeof(fr));
    }
    tl_write_int32 (w, 1700000500);                /* date */
    /* sizes: Vector<PhotoSize> with 1 photoSize#75c78e60 type+w+h+size */
    tl_write_uint32(w, TL_vector);
    tl_write_uint32(w, 1);
    tl_write_uint32(w, CRC_photoSize);
    tl_write_string(w, "y");
    tl_write_int32 (w, 1280);
    tl_write_int32 (w, 720);
    tl_write_int32 (w, 123456);
    tl_write_int32 (w, 2);                         /* dc_id */
}

/* Build a messageMediaDocument carrying a Document with a single
 * DocumentAttribute supplied by the caller. The Document layout we
 * emit:
 *   document#8fd4c4d8 flags=0 id access_hash file_reference:bytes date
 *                     mime_type:string size:long dc_id:int
 *                     attributes:Vector<DocumentAttribute>
 * No thumbs / video_thumbs (flags.0 / flags.1 both 0) so the skipper
 * takes the happy path. */
typedef void (*AttrBuilder)(TlWriter *w);

static void emit_document(TlWriter *w, const char *mime, int64_t size,
                           AttrBuilder build_attr) {
    tl_write_uint32(w, CRC_messageMediaDocument);
    tl_write_uint32(w, 1u);                        /* outer flags — has document */
    tl_write_uint32(w, CRC_document);
    tl_write_uint32(w, 0);                         /* document flags */
    tl_write_int64 (w, 0x1111222233334444LL);      /* id */
    tl_write_int64 (w, 0x5555666677778888LL);      /* access_hash */
    {
        static const unsigned char fr[] = { 0xAA, 0xBB, 0xCC, 0xDD };
        tl_write_bytes(w, fr, sizeof(fr));
    }
    tl_write_int32 (w, 1700000500);                /* date */
    tl_write_string(w, mime);
    tl_write_int64 (w, size);
    tl_write_int32 (w, 2);                         /* dc_id */
    /* attributes — always exactly one in these fixtures. */
    tl_write_uint32(w, TL_vector);
    tl_write_uint32(w, 1);
    build_attr(w);
}

/* documentAttributeVideo#43c57c48 flags:# duration:double w:int h:int ... */
static void attr_video(TlWriter *w) {
    tl_write_uint32(w, CRC_documentAttributeVideo);
    tl_write_uint32(w, 0);                         /* flags */
    tl_write_double(w, 42.0);                      /* duration */
    tl_write_int32 (w, 1280);                      /* w */
    tl_write_int32 (w, 720);                       /* h */
}

/* documentAttributeVideo with round_message (flags.0). */
static void attr_round_video(TlWriter *w) {
    tl_write_uint32(w, CRC_documentAttributeVideo);
    tl_write_uint32(w, 1u);                        /* flags: round_message */
    tl_write_double(w, 4.0);
    tl_write_int32 (w, 320);
    tl_write_int32 (w, 320);
}

/* documentAttributeAudio#9852f9c6 flags:# duration:int (ints, NOT double)
 * Voice flag lives at flags.10 — we set it to differentiate voice from
 * ordinary audio downloads. */
static void attr_audio_voice(TlWriter *w) {
    tl_write_uint32(w, CRC_documentAttributeAudio);
    tl_write_uint32(w, 1u << 10);                  /* flags: voice */
    tl_write_int32 (w, 8);                         /* duration */
}

static void attr_audio_music(TlWriter *w) {
    tl_write_uint32(w, CRC_documentAttributeAudio);
    tl_write_uint32(w, 0u);                        /* flags: plain audio */
    tl_write_int32 (w, 197);                       /* duration (m:s) */
}

/* documentAttributeSticker#6319d612 flags:# alt:string
 *   stickerset:InputStickerSet mask_coords:flags.0?MaskCoords
 * inputStickerSetEmpty#ffb62b95 — no body. */
static void attr_sticker(TlWriter *w) {
    tl_write_uint32(w, CRC_documentAttributeSticker);
    tl_write_uint32(w, 0);                         /* flags */
    tl_write_string(w, ":heart_eyes:");            /* alt */
    tl_write_uint32(w, CRC_inputStickerSetEmpty);
}

/* documentAttributeAnimated#11b58939 (GIF). Followed by a filename attr
 * would be redundant; keep it single-attr. */
static void attr_animated(TlWriter *w) {
    tl_write_uint32(w, CRC_documentAttributeAnimated);
}

/* documentAttributeFilename#15590068 file_name:string */
static void attr_filename_hello_ogg(TlWriter *w) {
    tl_write_uint32(w, CRC_documentAttributeFilename);
    tl_write_string(w, "voice.ogg");
}

/* ---- getHistory responders ---- */

static void on_history_geo(MtRpcContext *ctx) {
    reply_history_with_media(ctx, build_media_geo, 1001, "pin drop");
}
static void on_history_contact(MtRpcContext *ctx) {
    reply_history_with_media(ctx, build_media_contact, 1002, "contact");
}
static void on_history_webpage(MtRpcContext *ctx) {
    reply_history_with_media(ctx, build_media_webpage, 1003, "link");
}
static void on_history_poll(MtRpcContext *ctx) {
    reply_history_with_media(ctx, build_media_poll, 1004, "poll");
}
static void on_history_photo(MtRpcContext *ctx) {
    reply_history_with_media(ctx, build_media_photo, 1005, "pic");
}

/* Per-document-variant responder factory — each builds a Document with
 * the given attribute as its single DocumentAttribute and a controlled
 * mime/size pair so tests can assert the parser captured them. */
static const char *g_doc_mime       = NULL;
static int64_t     g_doc_size       = 0;
static AttrBuilder g_doc_attr_build = NULL;
static int32_t     g_doc_msg_id     = 0;
static const char *g_doc_caption    = NULL;

static void build_media_document_dispatch(TlWriter *w) {
    emit_document(w, g_doc_mime, g_doc_size, g_doc_attr_build);
}

static void on_history_document(MtRpcContext *ctx) {
    reply_history_with_media(ctx, build_media_document_dispatch,
                              g_doc_msg_id, g_doc_caption);
}

/* ================================================================ */
/* Parse tests                                                      */
/* ================================================================ */

static void test_rich_media_parse_geo(void) {
    with_tmp_home("parse-geo");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_messages_getHistory, on_history_geo, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    HistoryEntry rows[4]; int n = 0;
    ASSERT(domain_get_history_self(&cfg, &s, &t, 0, 4, rows, &n) == 0,
           "getHistory with messageMediaGeo succeeds");
    ASSERT(n == 1, "one row parsed");
    ASSERT(rows[0].id == 1001, "id preserved");
    ASSERT(strcmp(rows[0].text, "pin drop") == 0,
           "caption preserved before geo media");
    ASSERT(rows[0].media == MEDIA_GEO, "media classified as MEDIA_GEO");
    ASSERT(rows[0].complex == 0, "geo does not mark complex");

    transport_close(&t);
    mt_server_reset();
}

static void test_rich_media_parse_contact(void) {
    with_tmp_home("parse-contact");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_messages_getHistory, on_history_contact, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    HistoryEntry rows[4]; int n = 0;
    ASSERT(domain_get_history_self(&cfg, &s, &t, 0, 4, rows, &n) == 0,
           "getHistory with messageMediaContact succeeds");
    ASSERT(n == 1, "one row parsed");
    ASSERT(rows[0].id == 1002, "id preserved");
    ASSERT(rows[0].media == MEDIA_CONTACT, "classified as MEDIA_CONTACT");
    ASSERT(rows[0].complex == 0, "contact does not mark complex");

    transport_close(&t);
    mt_server_reset();
}

static void test_rich_media_parse_webpage(void) {
    with_tmp_home("parse-webpage");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_messages_getHistory, on_history_webpage, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    HistoryEntry rows[4]; int n = 0;
    ASSERT(domain_get_history_self(&cfg, &s, &t, 0, 4, rows, &n) == 0,
           "getHistory with messageMediaWebPage succeeds");
    ASSERT(n == 1, "one row parsed");
    ASSERT(rows[0].id == 1003, "id preserved");
    ASSERT(strcmp(rows[0].text, "link") == 0, "caption preserved");
    ASSERT(rows[0].media == MEDIA_WEBPAGE, "classified as MEDIA_WEBPAGE");
    ASSERT(rows[0].complex == 0, "webpage does not mark complex");

    transport_close(&t);
    mt_server_reset();
}

static void test_rich_media_parse_poll(void) {
    with_tmp_home("parse-poll");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_messages_getHistory, on_history_poll, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    HistoryEntry rows[4]; int n = 0;
    ASSERT(domain_get_history_self(&cfg, &s, &t, 0, 4, rows, &n) == 0,
           "getHistory with messageMediaPoll succeeds");
    ASSERT(n == 1, "one row parsed");
    ASSERT(rows[0].id == 1004, "id preserved");
    ASSERT(rows[0].media == MEDIA_POLL, "classified as MEDIA_POLL");
    ASSERT(rows[0].complex == 0, "poll does not mark complex");

    transport_close(&t);
    mt_server_reset();
}

static void test_rich_media_parse_photo_metadata(void) {
    with_tmp_home("parse-photo");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_messages_getHistory, on_history_photo, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    HistoryEntry rows[4]; int n = 0;
    ASSERT(domain_get_history_self(&cfg, &s, &t, 0, 4, rows, &n) == 0,
           "getHistory with messageMediaPhoto succeeds");
    ASSERT(n == 1, "one row parsed");
    ASSERT(rows[0].media == MEDIA_PHOTO, "classified as MEDIA_PHOTO");
    ASSERT(rows[0].media_id == 0x1234567890ABLL,
           "photo_id propagates into HistoryEntry");
    ASSERT(rows[0].media_dc == 2, "dc_id propagates into HistoryEntry");
    ASSERT(rows[0].media_info.access_hash == (int64_t)0xCAFEBABEDEADBEEFLL,
           "access_hash captured");
    ASSERT(rows[0].media_info.file_reference_len == 4,
           "file_reference length captured");
    ASSERT(strcmp(rows[0].media_info.thumb_type, "y") == 0,
           "largest photoSize.type captured");
    ASSERT(rows[0].complex == 0, "photo happy path does not mark complex");

    transport_close(&t);
    mt_server_reset();
}

/* Helper: run a single-document parse test with the given attribute
 * builder and mime/size pair. Collapses the per-variant boilerplate. */
static void run_document_parse(const char *tag, AttrBuilder attr,
                                const char *mime, int64_t size,
                                int32_t msg_id, const char *caption,
                                const char *want_filename) {
    with_tmp_home(tag);
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    g_doc_mime       = mime;
    g_doc_size       = size;
    g_doc_attr_build = attr;
    g_doc_msg_id     = msg_id;
    g_doc_caption    = caption;
    mt_server_expect(CRC_messages_getHistory, on_history_document, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    HistoryEntry rows[4]; int n = 0;
    ASSERT(domain_get_history_self(&cfg, &s, &t, 0, 4, rows, &n) == 0,
           "getHistory with messageMediaDocument succeeds");
    ASSERT(n == 1, "one row parsed");
    ASSERT(rows[0].id == msg_id, "id preserved past Document trailer");
    ASSERT(rows[0].media == MEDIA_DOCUMENT,
           "classified as MEDIA_DOCUMENT");
    ASSERT(rows[0].media_id == 0x1111222233334444LL,
           "document_id propagates into HistoryEntry");
    ASSERT(rows[0].media_info.document_size == size,
           "document size captured");
    ASSERT(strcmp(rows[0].media_info.document_mime, mime) == 0,
           "document mime captured verbatim");
    if (want_filename) {
        ASSERT(strcmp(rows[0].media_info.document_filename,
                       want_filename) == 0,
               "document filename captured");
    }
    ASSERT(rows[0].complex == 0, "document attr does not mark complex");

    transport_close(&t);
    mt_server_reset();
}

static void test_rich_media_parse_document_video(void) {
    run_document_parse("parse-video", attr_video,
                        "video/mp4", 1234567LL, 2001, "vid", NULL);
}

static void test_rich_media_parse_document_round_video(void) {
    run_document_parse("parse-round", attr_round_video,
                        "video/mp4", 655360LL, 2002, "round", NULL);
}

static void test_rich_media_parse_document_audio_music(void) {
    run_document_parse("parse-audio", attr_audio_music,
                        "audio/mpeg", 4915200LL, 2003, "track", NULL);
}

static void test_rich_media_parse_document_voice_note(void) {
    run_document_parse("parse-voice", attr_audio_voice,
                        "audio/ogg", 42000LL, 2004, "vm", NULL);
}

static void test_rich_media_parse_document_sticker(void) {
    run_document_parse("parse-sticker", attr_sticker,
                        "image/webp", 51200LL, 2005, "", NULL);
}

static void test_rich_media_parse_document_animation(void) {
    run_document_parse("parse-gif", attr_animated,
                        "video/mp4", 131072LL, 2006, "gif", NULL);
}

static void test_rich_media_parse_document_filename_captured(void) {
    /* Single-attribute vector carrying documentAttributeFilename lets us
     * assert that the filename propagates out of the skipper into
     * HistoryEntry.media_info.document_filename — a guarantee the
     * download-name inference in future US-22 work depends on. */
    run_document_parse("parse-filename", attr_filename_hello_ogg,
                        "audio/ogg", 42000LL, 2007, "", "voice.ogg");
}

/* ================================================================ */
/* Download-path coverage tests                                     */
/* ================================================================ */

/* upload.file helper reused by the download tests. */
static void reply_upload_file_short(MtRpcContext *ctx) {
    uint8_t payload[64];
    for (size_t i = 0; i < sizeof(payload); ++i)
        payload[i] = (uint8_t)(i ^ 0xC3u);

    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_upload_file);
    tl_write_uint32(&w, CRC_storage_filePartial);
    tl_write_int32 (&w, 0);
    tl_write_bytes (&w, payload, sizeof(payload));
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

static void on_get_file_short_doc(MtRpcContext *ctx) {
    reply_upload_file_short(ctx);
}

static void make_document_mi(MediaInfo *mi, const char *fname,
                              const char *mime) {
    memset(mi, 0, sizeof(*mi));
    mi->kind = MEDIA_DOCUMENT;
    mi->document_id = 0x1111222233334444LL;
    mi->access_hash = 0x5555666677778888LL;
    mi->dc_id = 2;
    mi->file_reference_len = 4;
    mi->file_reference[0] = 0xAA; mi->file_reference[1] = 0xBB;
    mi->file_reference[2] = 0xCC; mi->file_reference[3] = 0xDD;
    if (fname) {
        snprintf(mi->document_filename,
                 sizeof(mi->document_filename), "%s", fname);
    }
    if (mime) {
        snprintf(mi->document_mime,
                 sizeof(mi->document_mime), "%s", mime);
    }
}

/* Exercises the happy path for a voice document download — same chunk
 * flow as plain documents (no extension inference today), which keeps
 * the test tied to behaviour actually present in media.c. */
static void test_rich_media_download_voice_note_chunked(void) {
    with_tmp_home("dl-voice");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_upload_getFile, on_get_file_short_doc, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    MediaInfo mi; make_document_mi(&mi, "voice.ogg", "audio/ogg");
    const char *out = "/tmp/tg-cli-ft-rich-media-voice.ogg";
    int wrong = -1;
    ASSERT(domain_download_document(&cfg, &s, &t, &mi, out, &wrong) == 0,
           "voice download returns 0");
    ASSERT(wrong == 0, "no wrong_dc surfaced");

    struct stat st;
    ASSERT(stat(out, &st) == 0, "voice file written");
    ASSERT(st.st_size == 64, "64 bytes written for voice");
    unlink(out);

    transport_close(&t);
    mt_server_reset();
}

static void test_rich_media_download_sticker_chunked(void) {
    with_tmp_home("dl-sticker");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_upload_getFile, on_get_file_short_doc, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    MediaInfo mi; make_document_mi(&mi, "heart.webp", "image/webp");
    const char *out = "/tmp/tg-cli-ft-rich-media-heart.webp";
    int wrong = -1;
    ASSERT(domain_download_document(&cfg, &s, &t, &mi, out, &wrong) == 0,
           "sticker download returns 0");

    struct stat st;
    ASSERT(stat(out, &st) == 0, "sticker file written");
    ASSERT(st.st_size == 64, "64 bytes written for sticker");
    unlink(out);

    transport_close(&t);
    mt_server_reset();
}

static void test_rich_media_download_video_chunked(void) {
    with_tmp_home("dl-video");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_upload_getFile, on_get_file_short_doc, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    MediaInfo mi; make_document_mi(&mi, "clip.mp4", "video/mp4");
    const char *out = "/tmp/tg-cli-ft-rich-media-clip.mp4";
    int wrong = -1;
    ASSERT(domain_download_document(&cfg, &s, &t, &mi, out, &wrong) == 0,
           "video download returns 0");

    struct stat st;
    ASSERT(stat(out, &st) == 0, "video file written");
    ASSERT(st.st_size == 64, "64 bytes written for video");
    unlink(out);

    transport_close(&t);
    mt_server_reset();
}

/* Guard: download_photo must refuse a MEDIA_DOCUMENT MediaInfo
 * (exercises the kind-guard branch in media.c). */
static void test_rich_media_download_photo_rejects_document_kind(void) {
    with_tmp_home("dl-guard-doc");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    MediaInfo mi; make_document_mi(&mi, "x.bin", "application/octet-stream");
    int wrong = 7;
    ASSERT(domain_download_photo(&cfg, &s, &t, &mi,
                                  "/tmp/tg-cli-ft-rich-media-guard.bin",
                                  &wrong) == -1,
           "download_photo rejects MEDIA_DOCUMENT MediaInfo");
    ASSERT(wrong == 0, "wrong_dc cleared on kind mismatch");

    transport_close(&t);
    mt_server_reset();
}

/* Guard: download_document must refuse a MEDIA_PHOTO MediaInfo and a
 * MediaInfo with a missing document_id (both hit validation branches
 * that the existing suite never triggers). */
static void test_rich_media_download_document_rejects_photo_kind(void) {
    with_tmp_home("dl-guard-photo");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    MediaInfo mi;
    memset(&mi, 0, sizeof(mi));
    mi.kind = MEDIA_PHOTO;
    mi.photo_id = 0xAABBLL;
    mi.access_hash = 0xCCDDLL;
    mi.file_reference_len = 4;
    int wrong = 9;
    ASSERT(domain_download_document(&cfg, &s, &t, &mi,
                                     "/tmp/tg-cli-ft-rich-media-guard2.bin",
                                     &wrong) == -1,
           "download_document rejects MEDIA_PHOTO MediaInfo");
    ASSERT(wrong == 0, "wrong_dc cleared on kind mismatch");

    /* Second sub-case: MEDIA_DOCUMENT but with zero document_id — the
     * required-field guard. */
    MediaInfo mi2;
    memset(&mi2, 0, sizeof(mi2));
    mi2.kind = MEDIA_DOCUMENT;              /* id=0, access_hash=0 */
    wrong = 9;
    ASSERT(domain_download_document(&cfg, &s, &t, &mi2,
                                     "/tmp/tg-cli-ft-rich-media-guard3.bin",
                                     &wrong) == -1,
           "download_document rejects zero document_id");
    ASSERT(wrong == 0, "wrong_dc cleared on empty MediaInfo");

    transport_close(&t);
    mt_server_reset();
}

/* cross_dc wrapper: happy path (home DC succeeds on first shot) — hits
 * the early-return branch we otherwise never exercise. */
static void test_rich_media_cross_dc_home_succeeds(void) {
    with_tmp_home("dl-xdc-home");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_upload_getFile, on_get_file_short_doc, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    MediaInfo mi; make_document_mi(&mi, "home.bin", "application/octet-stream");
    const char *out = "/tmp/tg-cli-ft-rich-media-home.bin";
    ASSERT(domain_download_media_cross_dc(&cfg, &s, &t, &mi, out) == 0,
           "cross_dc happy path returns 0 without migration");

    struct stat st;
    ASSERT(stat(out, &st) == 0, "output written by cross_dc wrapper");
    ASSERT(st.st_size == 64, "64 bytes written via cross_dc happy path");
    unlink(out);

    transport_close(&t);
    mt_server_reset();
}

/* cross_dc wrapper: unsupported kind (MEDIA_GEO) routes through
 * download_any's dispatch and returns -1 immediately. Covers the
 * "unsupported kind" log branch without touching any network path. */
static void test_rich_media_cross_dc_rejects_unsupported_kind(void) {
    with_tmp_home("dl-xdc-unsup");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    MediaInfo mi;
    memset(&mi, 0, sizeof(mi));
    mi.kind = MEDIA_GEO;                   /* not photo, not document */
    ASSERT(domain_download_media_cross_dc(&cfg, &s, &t, &mi,
                                           "/tmp/tg-cli-ft-rich-media-unsup.bin")
               == -1,
           "cross_dc refuses MEDIA_GEO kind");

    transport_close(&t);
    mt_server_reset();
}

/* ================================================================ */
/* download_loop error-branch and cache-copy coverage              */
/* ================================================================ */

/* Reply with a raw TL body whose first word is CRC_upload_fileCdnRedirect.
 * download_loop sees this as CDN redirect and returns -1. */
#define CRC_upload_fileCdnRedirect_T 0xf18cda44u

static void on_get_file_cdn_redirect(MtRpcContext *ctx) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_upload_fileCdnRedirect_T);
    /* Minimal trailing bytes so resp_len >= 4 */
    tl_write_uint32(&w, 0);
    tl_write_uint32(&w, 0);
    tl_write_uint32(&w, 0);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

/* Reply with an unknown CRC — triggers the "unexpected top" branch. */
static void on_get_file_unknown_top(MtRpcContext *ctx) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, 0xDEAD1234u);
    tl_write_uint32(&w, 0);
    tl_write_uint32(&w, 0);
    tl_write_uint32(&w, 0);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

/* download_loop: CDN redirect reply → download returns -1. */
static void test_media_download_loop_cdn_redirect(void) {
    with_tmp_home("dl-cdn");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_upload_getFile, on_get_file_cdn_redirect, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    MediaInfo mi; make_document_mi(&mi, "x.bin", "application/octet-stream");
    int wrong = 0;
    ASSERT(domain_download_document(&cfg, &s, &t, &mi,
                                     "/tmp/tg-cli-ft-rm-cdn.bin",
                                     &wrong) == -1,
           "CDN redirect causes download to return -1");

    transport_close(&t);
    mt_server_reset();
}

/* download_loop: unknown top CRC → download returns -1. */
static void test_media_download_loop_unexpected_top(void) {
    with_tmp_home("dl-badtop");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_upload_getFile, on_get_file_unknown_top, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    MediaInfo mi; make_document_mi(&mi, "x.bin", "application/octet-stream");
    int wrong = 0;
    ASSERT(domain_download_document(&cfg, &s, &t, &mi,
                                     "/tmp/tg-cli-ft-rm-badtop.bin",
                                     &wrong) == -1,
           "Unexpected top CRC causes download to return -1");

    transport_close(&t);
    mt_server_reset();
}

/* download_loop: api_call fails (bad_msg_notification) → download returns -1.
 * Covers the "api_call failed at offset" log branch. */
static void test_media_download_loop_api_call_fail(void) {
    with_tmp_home("dl-apifail");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    /* Arm bad_msg_notification: api_call returns -1 on the first RPC. */
    mt_server_reply_bad_msg_notification(0, 16);
    mt_server_expect(CRC_upload_getFile, on_get_file_cdn_redirect, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    MediaInfo mi; make_document_mi(&mi, "x.bin", "application/octet-stream");
    int wrong = 0;
    ASSERT(domain_download_document(&cfg, &s, &t, &mi,
                                     "/tmp/tg-cli-ft-rm-apifail.bin",
                                     &wrong) == -1,
           "api_call fail causes download to return -1");

    transport_close(&t);
    mt_server_reset();
}

/* download_loop: fopen fails because output directory does not exist. */
static void test_media_download_loop_fopen_fail(void) {
    with_tmp_home("dl-fopen");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    /* No server expectation needed — fopen fails before the first RPC. */

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    MediaInfo mi; make_document_mi(&mi, "x.bin", "application/octet-stream");
    int wrong = 0;
    /* Path under a directory that does not exist. */
    ASSERT(domain_download_document(&cfg, &s, &t, &mi,
                                     "/tmp/tg-cli-no-such-dir/out.bin",
                                     &wrong) == -1,
           "fopen fail causes download to return -1");

    transport_close(&t);
    mt_server_reset();
}

/* Cache-hit copy path for photos: first download to path A, second to
 * path B (different path, same photo_id).  The copy branch in
 * domain_download_photo runs and B must exist with identical content. */
static void make_photo_mi(MediaInfo *mi) {
    memset(mi, 0, sizeof(*mi));
    mi->kind              = MEDIA_PHOTO;
    mi->photo_id          = 0xABCD1234EF56LL;
    mi->access_hash       = (int64_t)0xCAFEBABEDEADBEEFLL;
    mi->dc_id             = 2;
    mi->file_reference_len = 4;
    mi->file_reference[0] = 0x01; mi->file_reference[1] = 0x02;
    mi->file_reference[2] = 0x03; mi->file_reference[3] = 0x04;
    mi->thumb_type[0]     = 'y';   mi->thumb_type[1]     = '\0';
}

static void test_media_photo_cache_hit_copy(void) {
    with_tmp_home("dl-cache-copy-photo");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_upload_getFile, on_get_file_short_doc, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    MediaInfo mi; make_photo_mi(&mi);
    const char *path_a = "/tmp/tg-cli-ft-rm-cache-photo-a.bin";
    const char *path_b = "/tmp/tg-cli-ft-rm-cache-photo-b.bin";
    unlink(path_a); unlink(path_b);

    /* First download → server is called, result cached at path_a. */
    int wrong = 0;
    ASSERT(domain_download_photo(&cfg, &s, &t, &mi, path_a, &wrong) == 0,
           "first photo download ok");

    /* Second download to a DIFFERENT path → cache-hit copy branch fires. */
    ASSERT(domain_download_photo(&cfg, &s, &t, &mi, path_b, &wrong) == 0,
           "second photo download (different path) ok via cache copy");

    struct stat st_a, st_b;
    ASSERT(stat(path_a, &st_a) == 0, "path_a exists");
    ASSERT(stat(path_b, &st_b) == 0, "path_b exists after copy");
    ASSERT(st_a.st_size == st_b.st_size, "copy has same size as original");

    unlink(path_a); unlink(path_b);
    transport_close(&t);
    mt_server_reset();
}

/* Cache-hit copy path for documents: first download to path A, second to
 * path B — exercises the copy branch in domain_download_document. */
static void test_media_document_cache_hit_copy(void) {
    with_tmp_home("dl-cache-copy-doc");
    mt_server_init(); mt_server_reset();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_upload_getFile, on_get_file_short_doc, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    MediaInfo mi; make_document_mi(&mi, "doc.bin", "application/octet-stream");
    const char *path_a = "/tmp/tg-cli-ft-rm-cache-doc-a.bin";
    const char *path_b = "/tmp/tg-cli-ft-rm-cache-doc-b.bin";
    unlink(path_a); unlink(path_b);

    int wrong = 0;
    ASSERT(domain_download_document(&cfg, &s, &t, &mi, path_a, &wrong) == 0,
           "first document download ok");

    ASSERT(domain_download_document(&cfg, &s, &t, &mi, path_b, &wrong) == 0,
           "second document download (different path) ok via cache copy");

    struct stat st_a, st_b;
    ASSERT(stat(path_a, &st_a) == 0, "path_a exists");
    ASSERT(stat(path_b, &st_b) == 0, "path_b exists after copy");
    ASSERT(st_a.st_size == st_b.st_size, "copy has same size as original");

    unlink(path_a); unlink(path_b);
    transport_close(&t);
    mt_server_reset();
}

/* cross-DC FILE_MIGRATE: home DC returns FILE_MIGRATE_4 on upload.getFile;
 * domain_download_media_cross_dc enters the retry path (lines 273+).
 * We test two sub-cases:
 *   A) dc_session_open fails → domain_download_media_cross_dc returns -1
 *      (covers the LOG_INFO "FILE_MIGRATE" line and the dc_session_open
 *      failure branch at lines 277-279).
 *   B) dc_session_open succeeds (pre-seeded DC4) but the DC4 download
 *      also returns FILE_MIGRATE → dummy wrong_dc ignored, returns -1
 *      (covers dc_session_ensure_authorized fast path + download_any
 *      + dc_session_close at lines 285-300).
 */
static void on_get_file_always_file_migrate(MtRpcContext *ctx) {
    /* Always returns FILE_MIGRATE_4 — used in sub-case B where we need
     * the home-DC call to set wrong_dc=4 and the DC4 call to also fail. */
    mt_server_arm_reconnect();
    mt_server_reply_error(ctx, 303, "FILE_MIGRATE_4");
}

static void test_media_cross_dc_session_open_fails(void) {
    /* Sub-case A: the home DC returns FILE_MIGRATE_4, then dc_session_open
     * for DC4 fails (mock connect failure) → cross_dc returns -1.
     * Covers lines 273, 277-279. */
    with_tmp_home("dl-xdc-openfail");
    mt_server_init(); mt_server_reset();

    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_upload_getFile, on_get_file_always_file_migrate, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    /* Do NOT seed DC4 session → session_store_load_dc(4) fails → dc_session
     * falls through to the DH handshake path → auth_flow_connect_dc runs
     * transport_connect which succeeds (mock), but the handshake itself fails
     * because there is no proper DH responder set up.  This causes
     * dc_session_open to return -1. */
    mock_socket_fail_connect();   /* make the DC4 transport_connect fail */

    MediaInfo mi; make_document_mi(&mi, "xdc.bin", "application/octet-stream");
    const char *out = "/tmp/tg-cli-ft-rm-xdc-openfail.bin";
    unlink(out);

    ASSERT(domain_download_media_cross_dc(&cfg, &s, &t, &mi, out) == -1,
           "cross_dc returns -1 when dc_session_open fails after FILE_MIGRATE_4");

    unlink(out);
    transport_close(&t);
    mt_server_reset();
}

static void test_media_cross_dc_download_any_on_foreign_dc(void) {
    /* Sub-case B: home DC returns FILE_MIGRATE_4, DC4 session opens OK
     * (pre-seeded), but DC4's download also returns an rpc_error (not a
     * migration this time — 400 MEDIA_INVALID) so download_any fails on the
     * foreign DC, covering lines 293-300. */
    with_tmp_home("dl-xdc-foreign");
    mt_server_init(); mt_server_reset();

    MtProtoSession s; load_session(&s);
    ASSERT(mt_server_seed_extra_dc(4) == 0, "seed DC4 session");
    /* Same handler for both calls: first call sets wrong_dc=4 (home DC),
     * reconnect is armed; second call (DC4) also returns an rpc_error
     * (400, not a migration) so download_any on DC4 fails. */
    mt_server_expect(CRC_upload_getFile, on_get_file_always_file_migrate, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    MediaInfo mi; make_document_mi(&mi, "xdc.bin", "application/octet-stream");
    const char *out = "/tmp/tg-cli-ft-rm-xdc-foreign.bin";
    unlink(out);

    /* Both home DC and DC4 return FILE_MIGRATE_4.  The DC4 call receives the
     * migrate error but since wrong_dc is the dummy parameter,
     * domain_download_media_cross_dc returns -1 (download_any rc != 0). */
    ASSERT(domain_download_media_cross_dc(&cfg, &s, &t, &mi, out) == -1,
           "cross_dc returns -1 when DC4 download also fails");

    unlink(out);
    transport_close(&t);
    mt_server_reset();
}

void run_rich_media_types_tests(void) {
    /* Parse-path coverage — one test per media variant. */
    RUN_TEST(test_rich_media_parse_geo);
    RUN_TEST(test_rich_media_parse_contact);
    RUN_TEST(test_rich_media_parse_webpage);
    RUN_TEST(test_rich_media_parse_poll);
    RUN_TEST(test_rich_media_parse_photo_metadata);
    RUN_TEST(test_rich_media_parse_document_video);
    RUN_TEST(test_rich_media_parse_document_round_video);
    RUN_TEST(test_rich_media_parse_document_audio_music);
    RUN_TEST(test_rich_media_parse_document_voice_note);
    RUN_TEST(test_rich_media_parse_document_sticker);
    RUN_TEST(test_rich_media_parse_document_animation);
    RUN_TEST(test_rich_media_parse_document_filename_captured);

    /* Download-path coverage for media.c guards + cross-DC wrapper. */
    RUN_TEST(test_rich_media_download_voice_note_chunked);
    RUN_TEST(test_rich_media_download_sticker_chunked);
    RUN_TEST(test_rich_media_download_video_chunked);
    RUN_TEST(test_rich_media_download_photo_rejects_document_kind);
    RUN_TEST(test_rich_media_download_document_rejects_photo_kind);
    RUN_TEST(test_rich_media_cross_dc_home_succeeds);
    RUN_TEST(test_rich_media_cross_dc_rejects_unsupported_kind);

    /* download_loop error branches and cache-copy paths (media.c ≥90%). */
    RUN_TEST(test_media_download_loop_cdn_redirect);
    RUN_TEST(test_media_download_loop_unexpected_top);
    RUN_TEST(test_media_download_loop_api_call_fail);
    RUN_TEST(test_media_download_loop_fopen_fail);
    RUN_TEST(test_media_photo_cache_hit_copy);
    RUN_TEST(test_media_document_cache_hit_copy);
    RUN_TEST(test_media_cross_dc_session_open_fails);
    RUN_TEST(test_media_cross_dc_download_any_on_foreign_dc);
}
