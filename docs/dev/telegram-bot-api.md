---
name: Telegram Bot API Reference
description: HTTPS Bot API endpoint-ök, objektumok, limitációk — C kliens implementációhoz
type: reference
---

# Telegram Bot API Reference (HTTPS)

## Alapok

- **Base URL:** `https://api.telegram.org/bot<token>/METHOD_NAME`
- **Fájl letöltés:** `https://api.telegram.org/file/bot<token>/<file_path>`
- **Token formátum:** `123456:ABC-DEF1234ghIkl-zyx57W2v1u123ew11`
- **Auth:** token az URL-ben, nincs header/OAuth
- **HTTPS kötelező**, UTF-8
- **GET és POST is** támogatott
- **Paraméterezés:** query string, form-urlencoded, JSON body, multipart (fájl feltöltés)

## Válasz formátum

**Sikeres:**
```json
{ "ok": true, "result": { ... } }
```

**Hiba:**
```json
{ "ok": false, "error_code": 400, "description": "Bad request: chat not found" }
```

Opcionális `parameters` mező (ResponseParameters):
- `migrate_to_chat_id` (int64) — csoport → szupercsoport migráció
- `retry_after` (int) — flood control, ennyi másodpercet várj

### Hibakódok

| Kód | Jelentés |
|-----|----------|
| 400 | Bad Request — érvénytelen paraméter |
| 401 | Unauthorized — rossz token |
| 403 | Forbidden — nincs jogosultság |
| 404 | Not Found — chat/módszer nem található |
| 429 | Too Many Requests — flood control, lásd retry_after |

## Endpoint-ök

### getMe
`GET /bot<token>/getMe` — nincs paraméter. Visszaadja a bot User objektumát.
**Első hívás:** token validációra ajánlott.

### getUpdates (long polling)
`GET /bot<token>/getUpdates`

| Paraméter | Típus | Alapért. | Leírás |
|-----------|-------|----------|--------|
| offset | int | — | Első update azonosító (update_id + 1 az utolsó確認-oltól) |
| limit | int | 100 | 1–100 update egyszerre |
| timeout | int | 0 | Long polling timeout másodpercben (0 = rövid polling) |
| allowed_updates | string[] | mind | Szűrő: ["message", "edited_channel_post", …] |

**Fontos:**
- Nem működik webhook beállítása mellett
- Update-k max 24 óráig tárolódnak a szerveren
- Az `offset` alapján történik a megerősítés: `offset = highest_update_id + 1`
- Long polling: a szerver nyitva tartja a kapcsolatot `timeout` másodpercig vagy amíg update érkezik

### getChat
`GET /bot<token>/getChat`

| Paraméter | Típus | Kötelező |
|-----------|-------|----------|
| chat_id | int/string | Igen |

Visszaad: `ChatFullInfo` objektum (teljes chat adatok).

### sendMessage (jövőbeli használatra)
`POST /bot<token>/sendMessage`

| Paraméter | Típus | Kötelező | Leírás |
|-----------|-------|----------|--------|
| chat_id | int/string | Igen | Cél chat |
| text | string | Igen | Üzenet szöveg (1–4096 karakter) |
| parse_mode | string | Nem | "MarkdownV2", "HTML" |
| disable_notification | bool | Nem | Csendes küldés |

### getFile
`GET /bot<token>/getFile`

| Paraméter | Típus | Kötelező |
|-----------|-------|----------|
| file_id | string | Igen |

Visszaad: `File` objektum (`file_id`, `file_unique_id`, `file_size`, `file_path`).

Letöltés: `https://api.telegram.org/file/bot<token>/<file_path>`
- Max 20 MB (cloud szerver)
- Link min. 1 óráig érvényes
- `file_id` bot-specifikus, `file_unique_id` stabil de nem letölthető

## Objektumok

### Update
| Mező | Típus | Leírás |
|------|-------|--------|
| update_id | int | Egyedi azonosító (szekvenciális) |
| message | Message? | Új bejövő üzenet |
| edited_message | Message? | Szerkesztett üzenet |
| channel_post | Message? | Új csatorna poszt |
| edited_channel_post | Message? | Szerkesztett csatorna poszt |
| callback_query | CallbackQuery? | Inline gomb visszahívás |
| message_reaction | MessageReactionUpdated? | Reakció változás |
| my_chat_member | ChatMemberUpdated? | Bot tagság változás |

**Egyszerre legfeljebb EGY opcionális mező lehet jelen.**

### Message
| Mező | Típus | Leírás |
|------|-------|--------|
| message_id | int | Egyedi a chat-en belül |
| from | User? | Feladó |
| date | int | Unix timestamp |
| chat | Chat | Chat amihez tartozik |
| text | string? | Szöveg |
| entities | MessageEntity[]? | Formázás |
| photo | PhotoSize[]? | Kép (több méret) |
| document | Document? | Dokumentum |
| video | Video? | Videó |
| audio | Audio? | Audio |
| voice | Voice? | Hangüzenet |
| animation | Animation? | GIF/animáció |
| sticker | Sticker? | Matrica |
| video_note | VideoNote? | Kör videó |
| caption | string? | Média leírás |
| reply_to_message | Message? | Válasz eredeti üzenet |
| forward_origin | MessageOrigin? | Továbbítás info |
| new_chat_members | User[]? | Új tagok |
| left_chat_member | User? | Kilépett tag |
| pinned_message | Message? | Rögzített üzenet |

### Chat
| Mező | Típus | Leírás |
|------|-------|--------|
| id | int64 | Egyedi azonosító |
| type | string | "private", "group", "supergroup", "channel" |
| title | string? | Cím (csoport/csatorna) |
| username | string? | Felhasználónév |
| first_name | string? | Keresztnév (private) |
| last_name | string? | Vezetéknév (private) |
| is_forum | bool? | Fórum-e (supergroup) |

### User
| Mező | Típus | Leírás |
|------|-------|--------|
| id | int64 | Egyedi azonosító (52 bit szignifikáns) |
| is_bot | bool | Bot-e |
| first_name | string | Keresztnév |
| last_name | string? | Vezetéknév |
| username | string? | Felhasználónév |

### Média típusok

**PhotoSize:** `file_id`, `file_unique_id`, `width`, `height`, `file_size?`
- A Message.photo egy **tömb** — az utolsó a legnagyobb felbontás

**Document:** `file_id`, `file_unique_id`, `thumbnail?`, `file_name?`, `mime_type?`, `file_size?`

**Video:** `file_id`, `file_unique_id`, `width`, `height`, `duration`, `thumbnail?`, `file_name?`, `mime_type?`, `file_size?`

**Audio:** `file_id`, `file_unique_id`, `duration`, `performer?`, `title?`, `file_name?`, `mime_type?`, `file_size?`

**Voice:** `file_id`, `file_unique_id`, `duration`, `mime_type?`, `file_size?`

**Animation:** `file_id`, `file_unique_id`, `width`, `height`, `duration`, `thumbnail?`, `file_name?`, `mime_type?`, `file_size?`

**Sticker:** `file_id`, `file_unique_id`, `type`, `width`, `height`, `is_animated`, `is_video`, `thumbnail?`, `emoji?`, `set_name?`, `file_size?`

**VideoNote:** `file_id`, `file_unique_id`, `length` (átmérő), `duration`, `thumbnail?`, `file_size?`

**File:** `file_id`, `file_unique_id`, `file_size?`, `file_path?`

## Rate Limit-ek

| Mire | Limit |
|------|-------|
| Üzenetküldés — különböző chat-ekre | 30 msg/sec |
| Üzenetküldés — egy chat-re | ~1 msg/sec |
| Csoport — egy csoportba | ~20 msg/min |
| getUpdates batch | 1–100 update/hívás |
| Fájl letöltés | max 20 MB (cloud) |
| Fájl feltöltés — kép | max 10 MB |
| Fájl feltöltés — egyéb | max 50 MB (cloud) |
| Update tárolás | max 24 óra |

Flood → HTTP 429 + `retry_after` paraméter.

## Implementációs megjegyzések (C kliens)

- `id` mezők (User.id, Chat.id): `int64_t` (52 bit szignifikáns)
- `file_size`: lehet > 2^31 → `int64_t`
- Minden válasz JSON → saját JSON parser kell (`src/core/json_util.h/c`)
- Long polling: libcurl + megfelelő timeout
- Nincs getChatHistory — Bot API nem támogatja üzenetelőzmények lekérését
- Bot csak real-time update-eket kap (getUpdates/webhook)
