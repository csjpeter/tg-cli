---
name: Architecture Decisions
description: Meghozott tervezési döntések indokaik és alkalmazási területük
type: project
---

## HTTPS + libcurl, nincs kész API lib

**Döntés:** A Telegram Bot API-t közvetlenül libcurl HTTPS hívásokkal érjük el, saját JSON parserrel.
**Why:** A minimalista függőség elv (csak libcurl + libssl) érvényesül. Kész API library-k (TDLib, telegram-bot-api C++ wrapper) nagy, felesleges függőségek lennének.
**How to apply:** Minden API hívás libcurl-n keresztül megy. JSON parse-ra saját minimális implementáció (`src/core/json_util.h/c`).

## Valós idejű figyelés (long polling)

**Döntés:** A Telegram `getUpdates` long polling módszerét használjuk valós idejű frissítésekhez.
**Why:** A felhasználó valós idejű figyelést kért. Webhook szükségtelen egy CLI kliensnél (kellene egy HTTP szerver). Long polling egyszerű, egyetlen libcurl hívás.
**How to apply:** Az alkalmazás indításkor elindít egy long polling loop-ot, ami a `getUpdates` endpointet hívja `timeout` paraméterrel.

## Saját JSON parser

**Döntés:** Minimális JSON parsert írunk from scratch, tesztekkel.
**Why:** Nincs JSON függőség a C stdlib-ben. cJSON és társai külső függőségek. A Telegram API válaszainak részhalmazára elég egy egyszerű parser.
**How to apply:** `src/core/json_util.h/c` — cJSON-szerű, de minimális: csak objektum, tömb, string, szám, bool, null. Getter-alapú API (`json_get_string`, `json_get_int`, `json_get_array`).

## Read-only első verzió

**Döntés:** Az első verzió csak olvasni tud: üzenetek, chat-ek listázása, média letöltés.
**Why:** A felhasználó explicit kérése: először read-only, írási képesség később.
**How to apply:** Csak GET-típusú API hívások (`getUpdates`, `getChat`, `getChatHistory`, `getFile`). Nincs `sendMessage` és társai.

## Média: letöltés + local path megjelenítés

**Döntés:** A média (kép, dokumentum, stb.) letöltődik a cache-be, az üzenetben a helyi fájl elérési út jelenik meg.
**Why:** Parancssori kliens — képet nem lehet inline megjeleníteni. A local URL megnyitható böngészőben.
**How to apply:** `getFile` API → `file_path` → letöltés `~/.cache/tg-cli/media/` alá. Üzenetben: `[photo: /path/to/file.jpg]`.

## Két kliens: Bot + User

**Döntés:** Később két klienst kell építeni: egy Bot API-t használót és egy TDLib/Client API-t használót.
**Why:** A felhasználó jelzi, hogy mind bot, mind valós felhasználói fiókkal kell tudni csatlakozni. A Bot API csak botként működik.
**How to apply:** Első körben Bot API. Az architektúra (domain layer) úgy van tervezve, hogy a `telegram_service` mögötti implementáció cserélhető legyen (Bot API vs TDLib).

## manage.sh Makefile helyett

**Döntés:** `manage.sh` közvetlenül hívja a CMake-et, nincs Makefile.
**Why:** Rugalmasabb bővítésre; a Makefile csak wrapper volt, duplikáció.
**How to apply:** Minden build/test parancs `./manage.sh <cmd>` formában fut.

## Minimális függőségek

**Döntés:** Csak C stdlib, POSIX, libcurl és libssl engedélyezett runtime függőségként.
**Why:** A felhasználó explicit elvként rögzítette: a függőségeket minimálisan kell tartani.
**How to apply:** Új könyvtár hozzáadása előtt mindig ki kell meríteni a stdlib/POSIX lehetőségeket. Új runtime függőség csak indoklással és felhasználói jóváhagyással kerülhet be.

## Cross-platform portabilitás

**Döntés:** A projekt Linux mellett macOS, Windows és Android platformokra is céloz.
**Why:** A felhasználó explicit igénye: jövőbeli multi-platform support.
**How to apply:**
- Platform különbségeket a **build rendszer (CMake) oldja fel, nem `#ifdef` makrók**
- `src/platform/` réteg: thin interfaces, `posix/` és `windows/` implementációk
- Toolchain: Linux=GCC, macOS=GCC/Apple Clang, Windows=MinGW-w64, Android=NDK Clang. MSVC nem célplatform.
