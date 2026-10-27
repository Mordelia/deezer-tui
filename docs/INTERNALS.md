# Internals

Notes on how `deezer-tui` works, and on the traps that shaped it. The README
covers usage; this document covers the machinery.

## Layout

```
src/
├── main.cpp                  command dispatch
├── api/
│   ├── http.{hpp,cpp}        libcurl wrapper (GET/POST, headers, cookies, streaming)
│   ├── types.{hpp,cpp}       Item (track, album, artist, playlist) + formatting
│   ├── deezer.{hpp,cpp}      public API: search, charts, albums, artists
│   ├── auth.{hpp,cpp}        ARL -> session, gw-light calls
│   ├── account.{hpp,cpp}     account library through gw-light
│   └── stream.{hpp,cpp}      track token -> CDN URL -> decrypted stream
├── crypto/
│   └── blowfish.{hpp,cpp}    "BF_CBC_STRIPE" decryption
├── audio/
│   ├── byte_buffer.{hpp,cpp} bounded producer/consumer buffer
│   └── player.{hpp,cpp}      persistent libmpv player
├── mpris/
│   └── server.{hpp,cpp}      publish the player on the session bus
├── app/
│   ├── config.{hpp,cpp}      configuration file + environment overrides
│   ├── session.{hpp,cpp}     open a session from the configuration
│   ├── state.{hpp,cpp}       state, view stack, queue
│   ├── browser.{hpp,cpp}     worker loading the views
│   ├── playback.{hpp,cpp}    playback worker + player control
│   └── tui.{hpp,cpp}         interface composition
└── cli/
    ├── play.{hpp,cpp}        the --play command
    ├── selftest.{hpp,cpp}    the --self-test command
    └── show_config.{hpp,cpp} the --show-config command
```

## Threading

Three threads, with one rule: **no blocking work on the interface thread**.

- `BrowserService` and `PlaybackService` each own a worker consuming the latest
  pending request. The worker writes its state under a mutex, then wakes the
  FTXUI loop with `App::PostEvent(Event::Custom)`. The interface applies results
  in a `CatchEvent` handler — FTXUI components are never touched from a worker.
- `notify` (and therefore `PostEvent`) is **never** called while holding the
  service mutex. The UI thread takes that mutex in `snapshot()`, possibly while
  holding an internal lock of the event loop, which would expose us to a
  deadlock.
- The services are declared after `ScreenInteractive` and join their workers in
  their destructor, so no thread outlives the screen.

## Navigation

The interface is a **stack of views**. Its root is the search result (or the
library); opening an album, an artist or a playlist pushes a view, and `Escape`
pops it. A breadcrumb shows the path.

The FTXUI `Menu` is bound to a stable vector: the displayed view is copied into
it on every navigation change. Without that, pushing a view would invalidate the
reference the `Menu` holds.

Every loading request carries a **token**, stored on the relevant view. A result
is only applied to the view holding the matching token, so a load finishing
after a navigation is silently ignored.

## The player

`audio::Player` creates **one** mpv process and reuses it. Changing track is a
`loadfile`, not a new mpv.

Bytes go neither through the disk nor through a pipe: the custom `dzr://<id>`
protocol is registered with libmpv (`mpv_stream_cb_add_ro`), and its callbacks
read from a `ByteBuffer` fed by the decryptor.

- `ByteBuffer` is bounded (4 MiB): the producer blocks when it is full, and the
  back-pressure travels all the way to TCP. A 40 MiB FLAC never sits entirely in
  memory.
- `cancel_fn` is called by mpv from another thread to interrupt an ongoing read:
  that is the track-change mechanism.
- Stream callbacks must **never** call libmpv (documented deadlock).
- `size_fn` announces the size given by the API, which lets mpv know the
  duration from the start instead of estimating it as data arrives.
- Only a **natural** end of track (`MPV_END_FILE_REASON_EOF`) triggers chaining.
  `MPV_EVENT_END_FILE` also fires on a stop or a track change, and chaining in
  that case would restart the next track after an `s`.

### Seeking

The stream is seekable. `seek_fn` rounds the requested offset down to a
2048-byte block boundary — decryption works on blocks, so it cannot resume just
anywhere — then restarts the download with a `Range` header. Deezer's CDN
answers `206 Partial Content`; mpv adapts to the announced offset.

`size_fn` is what makes the stream seekable in mpv's eyes. mpv takes advantage of
it to read the end of the track in order to determine the exact duration, then
comes back to 0: two seeks when loading each track, which is normal.

One trap cost a lot of time. `ByteBuffer::reset()` is not enough to invalidate an
in-flight producer: if the producer only called `push` **after** the reset, it
captured the new generation, found the buffer full and blocked forever — nobody
was consuming anymore, and mpv stayed stuck inside `seek_fn`. The generation is
therefore captured by the producer at **its own** startup and passed to `push`,
which rejects any obsolete producer. Two regression tests cover this case.

## Queue

`a` queues the selected track, `f` plays the queue, `x` clears it, `Q` hides its
panel.

Without `f`, a queue built before any playback would never start: it only drains
at the end of a track. That is the typical case — you open the application,
queue three tracks, and nothing is playing to trigger the rest.

At the end of a track the queue goes first; once it is empty, playback continues
with the current view. Starting a track with `Enter` does not touch the queue.

## The Deezer session

`deezer.getUserData` sets session cookies (`dzr_uniq_id`, `_abck`, `bm_sz`). The
CSRF token returned in `checkForm` is **tied to those cookies**: without them,
later calls fail with `VALID_TOKEN_REQUIRED: Invalid CSRF token`.

`http::CookieJar` therefore keeps them from one call to the next, and `Session`
carries them around.

Another trap: in the `media.deezer.com/v1/get_url` response, the `cipher` field
is an **object** (`{"type": "BF_CBC_STRIPE"}`), not a string. Deezer uses
`BF_CBC_STRIPE` for MP3 as well as FLAC.

## Account library

The **public** API refuses account data: reading a private playlist returns
`OAuthException: An active access token must be used to query information about
the current user`. The library therefore goes through gw-light:

- `deezer.pageProfile` with `tab` set to `playlists`, `albums`, `artists` or
  `loved`, plus `user_id`;
- `playlist.getSongs` for the tracks of a playlist, private ones included.

Opening a playlist first tries the public API — enough for public playlists, and
it needs no account — then retries through gw-light on failure.

## Family profiles

A Family subscription groups several profiles. The ARL belongs to only one of
them (usually the main account), and the others stay invisible until you target
them.

Switching is done with a plain **cookie**, `account_id=<user_id>`: the server
then treats the session as that profile's. No reconnection, and no side effect on
the existing session. `deezer.getChildAccounts` provides the list of profiles.

**Library → Switch profile** lets you choose. The choice is remembered in
`$XDG_STATE_HOME/deezer-tui/profile`, a file **separate** from the configuration:
the latter is never rewritten, so user comments are preserved.

Two things to know:

- only **library** requests depend on the profile. Search, charts and playback
  stay global — the licence is shared by the subscription, so a track from
  another profile plays without trouble;
- loved tracks are fetched differently depending on the case: `pageProfile` with
  the `loved` tab for **another** user's profile, but `song.getFavoriteIds`
  followed by `song.getListData` for the account actually in use. The latter is
  also the only one that sees **private** playlists, and it reports different
  counts (38 playlists instead of 31 for the same profile, for instance).

## Encryption

Tracks are encrypted with Blowfish-CBC "stripe":

- the key is derived from the track id: `key[i] = md5hex(id)[i] ^
  md5hex(id)[i+16] ^ "g4el58wc0zvf9na1"[i]`, where `md5hex(id)[i]` is the
  **ASCII** byte of the digest;
- the stream is split into 2048-byte blocks;
- only blocks whose index is a multiple of 3 are encrypted, and the IV
  (`00 01 02 03 04 05 06 07`) is reset for every block;
- a trailing partial block is never encrypted.

`--self-test` checks the key derivation against reference values and performs a
decryption round-trip, with no network and no account.

## MPRIS

The player is published on the session bus as
`org.mpris.MediaPlayer2.deezer-tui`, with identity `Deezer`.

- **metadata**: title, artist, album, cover art, and `mpris:length` with the
  real duration. The official Electron application publishes `0` there, so
  progress bars do not work with it.
- **position**: served on demand, which allows an accurate progress bar.
- **commands**: `Play`, `Pause`, `PlayPause`, `Next`, `Previous`, `Stop`, `Seek`,
  `SetPosition`.

The bus name is distinct from the official application's
(`org.mpris.MediaPlayer2.deezer`), so both can coexist. They do publish the
**same identity** though: if both run, a shell picking the first of the list may
display either. Close the Electron application, or pin the player in your shell
configuration.

MPRIS is an **optional** integration. If the bus cannot be reached, `start`
fails, a warning is written to stderr, and everything else keeps working.

The player only knows how to move **relatively**: `SetPosition`, which is
absolute in MPRIS, is converted from the player's real position — not from a
remembered value, which could have gone stale.

### The flag that swallowed every signal

`sd_bus_emit_properties_changed` emits **nothing** for a property whose vtable is
not flagged `SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE`.

The properties stayed perfectly readable — `Get`, `GetAll`, `playerctl` and a Qt
probe all returned the right values — but no signal was emitted. Shells read
once, when the player is registered, therefore **before** any playback, and never
learn the rest. The widget displayed an empty title and "Unknown artist /
Unknown album" while everything was correct on the bus.

To diagnose that kind of thing, a minimal Quickshell instance (`qs -p <dir>` with
a `ShellRoot` reading `Quickshell.Services.Mpris`) is worth more than any other
tool: it is the same code as the shell, and its logs
(`QT_LOGGING_RULES="quickshell.*=true"`) show the values received.

## Testing without audio

The playback path can be exercised headlessly:

```sh
DEEZER_MPV_OPTIONS='ao=null' ./build/deezer-tui
```

Adding `;ao-null-untimed=yes` makes mpv run faster than real time, which turns a
five-minute track into an instant one when checking automatic chaining.

One pitfall when driving the interface through a pty: the test script must
**drain the master side continuously**. Otherwise the TUI blocks on write, the
worker waiting in `PostEvent` never returns, and the D-Bus thread freezes — which
looks exactly like a deadlock in the application.
