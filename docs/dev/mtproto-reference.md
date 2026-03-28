# MTProto 2.0 — saját C implementáció terv

## Összegzés

A Telegram **Bot API** (HTTPS JSON) csak botként működik, **nem lehet chat előzményeket lekérdezni**.
A valódi user klienshez a **MTProto 2.0** protokollt kell implementálnunk nulláról, C-ben,
libcurl (transport) + libssl (crypto) használatával, **külső könyvtárak nélkül**.

---

## 1. Architektúra — mit kell megépíteni

```
┌─────────────────────────────────────────────┐
│  Application (main.c + domain)              │
│  list chats, read messages, download media  │
├─────────────────────────────────────────────┤
│  Telegram API layer (TL RPC)                │
│  auth.sendCode, messages.getHistory, etc.   │
├─────────────────────────────────────────────┤
│  MTProto session                            │
│  msg_id, seq_no, salt, session_id, updates  │
├─────────────────────────────────────────────┤
│  MTProto encryption                         │
│  AES-256-IGE, SHA-256, auth_key management  │
├─────────────────────────────────────────────┤
│  TL serialization / deserialization         │
│  bináris objektum kódolás                   │
├─────────────────────────────────────────────┤
│  Transport (libcurl vagy raw TCP)           │
│  Abridged/Intermediate encoding             │
└─────────────────────────────────────────────┘
```

---

## 2. Transport réteg

### MTProto transport opciók:

| Transport | Leírás | Megvalósíthatóság |
|-----------|--------|-------------------|
| **TCP (Abridged)** | Nyers TCP, bináris protokoll | Legjobb real-time-hoz, de saját socket kezelés |
| **TCP (Intermediate)** | Nyers TCP, 4 bájtos hossz prefix | Hasonló, kicsit egyszerűbb |
| **HTTPS** | MTProto payload HTTP POST-ban | **Libcurl-el megoldható!** |
| **WebSocket (WSS)** | MTProto WS felett | Libcurl WS support kell |

**Ajánlás:** Kezdésnek **HTTPS transport** libcurl-vel — átmegy tűzfalakon, proxy-kon.
Később optimalizálásra TCP (Abridged) a valós idejű frissítésekhez.

### HTTPS transport működése:
- `POST https://149.154.167.50/apiw1` (vagy más DC IP)
- Content-Type: `application/octet-stream`
- Body: bináris MTProto payload (encrypted vagy plain)
- Válasz: bináris MTProto payload

### DC-k (Data Centers):
| DC | IP | Régió |
|----|-----|-------|
| 1 | 149.154.175.50 | Miami |
| 2 | 149.154.167.51 | Amsterdam |
| 3 | 149.154.175.100 | Miami |
| 4 | 149.154.167.91 | Amsterdam |
| 5 | 91.108.56.190 | Singapore |

---

## 3. Kriptográfia

### Amire szükség van (mind elérhető libssl-ben):

| Művelet | OpenSSL függvény | Használat |
|---------|-------------------|-----------|
| SHA-256 | `SHA256()` | msg_key számítás, KDF |
| AES-256-ECB | `AES_encrypt()` / `AES_decrypt()` | IGE mode építőelem |
| RSA | `RSA_public_encrypt()` | Auth key generálás |
| DH | manuális `BN_mod_exp()` | Auth key generálás |
| PRNG | `RAND_bytes()` | Véletlenszámok |

### AES-256-IGE (Infinite Garble Extension):
**Nem része az OpenSSL-nek!** Saját implementáció kell:

```
IGE_encrypt(plaintext, key, iv):
    for each 16-byte block i:
        c[i] = AES_encrypt(p[i] XOR iv_prev_c) XOR iv_prev_p
        iv_prev_c = c[i]
        iv_prev_p = p[i]
```

Az iv 32 bájt (két 16 bájtos blokk). Az első fele a "prev ciphertext",
a második a "prev plaintext" inicializáció.

### AES key + IV levezetés (MTProto 2.0):

```
x = 0 (client→server) vagy 8 (server→client)

msg_key_large = SHA256(substr(auth_key, 88+x, 32) + plaintext + padding)
msg_key = substr(msg_key_large, 8, 16)

sha256_a = SHA256(msg_key + substr(auth_key, x, 36))
sha256_b = SHA256(substr(auth_key, 40+x, 36) + msg_key)

aes_key = substr(sha256_a, 0, 8) + substr(sha256_b, 8, 16) + substr(sha256_a, 24, 8)
aes_iv  = substr(sha256_b, 0, 8) + substr(sha256_a, 8, 16) + substr(sha256_b, 24, 8)
```

---

## 4. Auth Key generálás (DH kulcscsere)

Ez 9 lépésből áll, **titkosítatlan** üzenetekkel:

### Lépések:

**1) req_pq_multi**
```
Küld: nonce (random 128 bit)
Vár: ResPQ { nonce, server_nonce, pq, server_public_key_fingerprints }
```

**2) pq faktorizálás**
- `pq` egy 64 bites szám, két prím szorzata
- Pollard rho algoritmus (kb. 20 sor C-ben)
- `p < q`, mindkettő prím

**3-4) req_DH_params**
```
Kliens generál: new_nonce (random 256 bit)
TL serialize: P_Q_inner_data_dc { pq, p, q, nonce, server_nonce, new_nonce, dc }
RSA_PAD encrypt → encrypted_data
Küld: req_DH_params { nonce, server_nonce, p, q, fingerprint, encrypted_data }
Vár: Server_DH_Params { nonce, server_nonce, encrypted_answer }
```

RSA_PAD implementáció (OAEP+ variáns):
- data + padding = 192 bájt
- Byte reverse → data_pad_reversed
- temp_key = random 32 bájt
- data_with_hash = data_pad_reversed + SHA256(temp_key + data_with_padding)
- aes_encrypted = AES256_IGE(data_with_hash, temp_key, zero_iv)
- temp_key_xor = temp_key XOR SHA256(aes_encrypted)
- result = RSA(temp_key_xor + aes_encrypted, server_pubkey)

**5) encrypted_answer dekódolás**
```
tmp_aes_key = SHA1(new_nonce + server_nonce) + substr(SHA1(server_nonce + new_nonce), 0, 12)
tmp_aes_iv = substr(SHA1(server_nonce + new_nonce), 12, 8) + SHA1(new_nonce + new_nonce) + substr(new_nonce, 0, 4)
AES256_IGE_decrypt → Server_DH_inner_data { g, dh_prime, g_a, server_time }
```

**6-7) set_client_DH_params**
```
b = random 2048 bit
g_b = pow(g, b) mod dh_prime
auth_key = pow(g_a, b) mod dh_prime   ← ez lesz a 2048 bites auth_key!
TL serialize + encrypt → encrypted_data
Küld: set_client_DH_params { nonce, server_nonce, encrypted_data }
```

**8-9) Válasz**
```
dh_gen_ok → auth_key sikeresen generálva!
server_salt = substr(new_nonce, 0, 8) XOR substr(server_nonce, 0, 8)
```

### Beépített server public key:
A kliensben hardkódolva kell lennie a Telegram server RSA public key-nek.

---

## 5. TL (Type Language) szerializáció

A TL a Telegram bináris szerializációs formátuma. Minden objektum:
- 4 bájtos constructor ID (CRC32 hash a definícióból)
- Mezők sorban, típus szerint kódolva

### Alaptípusok kódolása:

| Típus | Méret | Leírás |
|-------|-------|--------|
| int32 | 4 bájt | little-endian |
| int64 | 8 bájt | little-endian |
| int128 | 16 bájt | little-endian |
| int256 | 32 bájt | little-endian |
| float64 | 8 bájt | IEEE 754 |
| string/bare bytes | 4 bájt hossz + adat | 4 bájtra igazítva |
| bool | 0x997275b5 (true) / 0xbc799737 (false) | |
| vector | 0x1cb5c415 + int32 count + elemek | |

### Implementáció:
Nem kell teljes TL parser. Elegendő:
- `tl_write_int32(buf, val)`
- `tl_write_int64(buf, val)`
- `tl_write_string(buf, data, len)` — hossz prefix + padding
- `tl_read_int32(buf)`
- `tl_read_string(buf)` — hossz + adat + skip padding
- Constructor ID-k header fájlként

---

## 6. MTProto üzenet formátum

### Titkosított üzenet:
```
[auth_key_id: 8 byte] [msg_key: 16 byte] [encrypted_data: N byte]
```

### encrypted_data (dekódolás után):
```
[salt: 8 byte] [session_id: 8 byte] [message_id: 8 byte]
[seq_no: 4 byte] [message_data_length: 4 byte]
[message_data: N byte] [padding: 12..1024 byte]
```

### Titkosítatlan üzenet (auth key generálásnál):
```
[auth_key_id = 0: 8 byte] [message_id: 8 byte]
[message_data_length: 4 byte] [message_data: N byte]
```

### msg_id szabályok:
- `unixtime * 2^32` közelítőleg
- Páros (kliens→szerver), páratlan (szerver→kliens)
- Monoton növekvő
- ±300 sec távolság szerver időtől

### seq_no:
- `seq_no = current_seq * 2 + 1` (content-related üzenet)
- `seq_no = current_seq * 2` (nem content-related)

---

## 7. Authentikáció (user login)

Az auth_key generálás UTÁN, már titkosított csatornán:

### API metódusok:
1. `auth.sendCode` — SMS/kód küldése a megadott telefonszámra
2. `auth.signIn` — bejelentkezés a telefonszámmal + kóddal
3. `auth.checkPassword` — ha 2FA be van állítva

### API layer:
Minden RPC hívást be kell csomagolni:
```
invokeWithLayer(LAYER_VERSION, initConnection(api_id, api_hash, device, app_version, ...))
```

LAYER_VERSION: jelenleg ~200+
api_id/api_hash: Telegram-tól kell kérni (ingyenes, regisztráció kell)

---

## 8. Minimális API metódusok a user klienshez

### Auth:
- `auth.sendCode { phone, api_id, api_hash, settings }`
- `auth.signIn { phone, phone_code_hash, phone_code }`
- `auth.checkPassword { password }`

### Config:
- `help.getConfig` — DC információk, konfiguráció
- `help.getNearestDc` — legközelebbi DC

### Chat lista:
- `messages.getDialogs { offset_date, offset_id, offset_peer, limit }`
- `messages.getPeerDialogs { peers }`

### Üzenetek:
- `messages.getHistory { peer, offset_id, offset_date, limit }` — **chat előzmények!**
- `messages.getMessages { id }`
- `messages.search { peer, q, filter, limit }`

### Fájlok:
- `upload.getFile { location, offset, limit }` — bináris fájl letöltés
- `photos.getUserPhotos { user_id, offset, limit }`

### Frissítések:
- `updates.getState` — állapot lekérdezés
- `updates.getDifference { pts, date, qts }` — új üzenetek lekérése
- `updates.getChannelDifference` — csatorna frissítések

### Kontaktok:
- `contacts.getContacts`
- `contacts.search { q }`

---

## 9. Mit kell megépíteni — modulonként

### `src/core/tl_serial.h/c` — TL szerializáció
- write/read: int32, int64, int128, int256, string, bytes, vector, bool
- Builder + reader minta (cursor-alapú)

### `src/core/ige_aes.h/c` — AES-256-IGE
- `aes_ige_encrypt(plaintext, len, key, iv, output)`
- `aes_ige_decrypt(ciphertext, len, key, iv, output)`
- OpenSSL AES-ECB-re épül

### `src/core/mtproto_crypto.h/c` — MTProto titkosítás
- `mtproto_derive_keys(auth_key, msg_key, direction, aes_key, aes_iv)`
- `mtproto_encrypt(plain, auth_key, msg_key, output)`
- `mtproto_decrypt(cipher, auth_key, msg_key, output)`
- Padding generálás (12..1024 bájt)

### `src/core/mtproto_auth.h/c` — Auth key generálás
- `mtproto_gen_auth_key(transport, auth_key_out)`
- req_pq_multi → resPQ → faktorizáció → req_DH_params → DH → set_client_DH → dh_gen_ok
- Server public key tárolás

### `src/core/mtproto_session.h/c` — Session menedzsment
- session_id generálás
- msg_id generálás (monoton, időalapú)
- seq_no tracking
- server_salt management (30 percenként változik)
- auth_key perzisztens tárolás (fájlba mentés, jelszavas védelem)

### `src/infrastructure/tl_transport.h/c` — Transport réteg
- HTTPS mode: libcurl POST bináris payload-dal
- TCP mode (jövő): socket + Abridged/Intermediate encoding
- DC választás és váltás

### `src/infrastructure/mtproto_rpc.h/c` — RPC keretrendszer
- RPC query küldés (wrapper msg)
- Válasz fogadás és dispatch
- Container támogatás (több RPC egy üzenetben)
- gzip_packed dekompresszió
- msgs_ack nyugtázás
- Újraküldés (bad_msg_notification)

### `src/infrastructure/telegram_api.h/c` — API metódusok
- TL constructor ID-k definíciói (header)
- Auth: sendCode, signIn, checkPassword
- Messages: getDialogs, getHistory, getMessages
- Upload: getFile
- Updates: getState, getDifference

### `src/domain/telegram_service.h/c` — Domain logika
- telegram_connect() — auth_key betöltés vagy generálás
- telegram_login(phone) — SMS auth
- telegram_list_chats(limit, offset)
- telegram_get_history(chat_id, limit, offset_id)
- telegram_download_media(file_id, path)
- telegram_poll_updates() — valós idejű frissítések

---

## 10. Implementációs sorrend

### Fázis 1: Alapok
1. TL szerializáció (tl_serial.h/c) + tesztek
2. AES-256-IGE (ige_aes.h/c) + tesztek
3. MTProto crypto (mtproto_crypto.h/c) + tesztek
4. JSON parser (json_util.h/c) — a Bot API-hoz is kell

### Fázis 2: Auth key + Login
5. Transport réteg HTTPS mode (tl_transport.h/c) + tesztek
6. Auth key generálás (mtproto_auth.h/c) + tesztek
7. Session management (mtproto_session.h/c) + tesztek
8. RPC keretrendszer (mtproto_rpc.h/c) + tesztek

### Fázis 3: Telegram API
9. API constructor ID-k + szerializáció (telegram_api.h/c)
10. Auth flow: sendCode + signIn (telegram_service auth rész)
11. Config + DC kezelés

### Fázis 4: Funkciók
12. Chat lista (getDialogs)
13. Üzenet előzmények (getHistory)
14. Fájl letöltés (upload.getFile)
15. Valós idejű frissítések (updates.getDifference)

### Fázis 5: TUI + Integráció
16. Interactive TUI a chat listához és üzenetekhez
17. Batch mode CLI parancsok
18. Setup wizard (telefonszám + auth)

---

## 11. Teljesítmény és korlátok

| Paraméter | Érték |
|-----------|-------|
| auth_key méret | 256 bájt (2048 bit) |
| Üzenet overhead | ~40 bájt (header + padding minimum) |
| Fájl letöltés | upload.getFile, max 1 MB/chunk, több chunk-ban |
| API Layer | ~200 (gyakran változik, de visszafelé kompatibilis) |
| api_id/api_hash | Ingyenes, my.telegram.org-on kérhető |

---

## 12. Key takeaways

- **MTProto nem HTTPS JSON** — bináris protokoll, saját szerializáció (TL)
- **AES-IGE nincs OpenSSL-ben** — saját implementáció kell (~30 sor)
- **Auth key generálás** a legbonyolultabb rész (DH + RSA + több körös kommunikáció)
- **Transport lehet HTTPS** — libcurl-Rel POST-oljuk a bináris payload-ot
- **Chat előzmények elérhetők!** — `messages.getHistory` (ezt a Bot API nem tudja)
- **Telefonszámos login** — SMS kód + opcionális 2FA
- **Beépített server public key** kell a kliensbe (hardkódolva)
- **api_id + api_hash** kell a my.telegram.org-ról
