# Spotify notification image fix

A small Linux `LD_PRELOAD` shim that restores album artwork in Spotify track
notifications when GdkPixbuf defers image decoding until the loader is closed.
It also clears the previous track's artwork when notification text changes,
so missing or undecodable artwork does not leave an older album cover visible.
It does not modify the Spotify executable. The notification daemon must support
notification images; the original fix was confirmed with mako.

## Compatibility

The shim targets x86_64 Linux Spotify's notification path. It does not reject
client updates based on a release version, executable hash, Build ID, or fixed
address. Image recovery requires the notification instruction pattern to match;
if a future update changes that pattern, investigate and repair it then.

The latest analyzed executable had SHA-256
`030a6ea5fb2aa791e8acd67703eb7b21f77e8531e70871ebf211b8f9c81a826b`
and GNU Build ID `e18cef49a8467a1551f4d354b4e0df8d6bf3e833`.
These identify the analysis evidence, not an allowlist. The client also rejects
notification image files of 128 KiB or more before handing them to GdkPixbuf; the shim recovers those files at the
notification cleanup boundary.

The reported failure and recovery were confirmed with GdkPixbuf 2.44.8 and
glycin 2.2.2. Other decoder configurations may not exhibit the same failure.

## Requirements

- Linux x86_64 with GNU libc, a C compiler, GNU Make, and pkg-config.
- GdkPixbuf development headers.
- Python 3.11 or newer; tests additionally require Pillow, `libnotify.so.4`, GIO headers,
  and `dbus-daemon` for the isolated notification service.
- For the launcher: `/usr/bin/spotify-launcher` and its standard client install at
  `~/.local/share/spotify-launcher/install/usr/share/spotify/spotify`.

On Arch Linux, the relevant packages are `base-devel`, `gdk-pixbuf2`,
`python-pillow`, and `spotify-launcher`. The launcher uses `cc` and pkg-config
on its first run. It stores the compiled shim under
`$XDG_CACHE_HOME/spotify-notification-fix` (default: `~/.cache/spotify-notification-fix`).

## Usage

Keep the checkout in a permanent location. Prepare the shim without starting
Spotify, then start Spotify through this repository's launcher:

```sh
./bin/spotify-launcher --prepare-notification-fix
./bin/spotify-launcher
```

Fully quit any existing Spotify process before starting the patched instance.
An already running client keeps its original environment. The launcher preserves
URI arguments, existing `LD_PRELOAD` entries, and the standard launcher's update
and `spotify-flags.conf` handling. If the client is missing or compilation
fails, it delegates normal startup without adding this shim. Prepare-only mode
returns an error in those cases.

To add the launcher to your user command path, from this checkout:

```sh
mkdir -p "$HOME/.local/bin"
ln -s "$PWD/bin/spotify-launcher" "$HOME/.local/bin/spotify-launcher"
```

`ln` will refuse an existing destination. Keep any existing launcher or dotfiles
integration intact until you choose how to migrate it. Your shell or desktop
session must include `~/.local/bin` before `/usr/bin` in PATH. For menus or window
manager commands whose PATH differs, set the launcher to the absolute path of
`bin/spotify-launcher` in this checkout. No desktop files are overwritten here.

To bypass the shim, fully quit Spotify and start `/usr/bin/spotify-launcher`.

## Spicetify

Spicetify's JavaScript and theme customizations can be used with the shim. However,
Spicetify's automatic restart starts the client executable directly
and bypasses this launcher. Use:

```sh
spicetify --no-restart apply
```

When a restart is needed, fully quit Spotify and start it through the patched
launcher. `spicetify restart` also bypasses this launcher.

## How it works

The affected notification path writes the complete image to a `GdkPixbufLoader`
and asks for its pixbuf before closing it. With the tested decoder, the pixbuf is
still NULL at that point, so Spotify sends a notification without image data.

The shim intercepts `gdk_pixbuf_loader_get_pixbuf` and first calls the original
function. If that returns NULL, it checks that the caller belongs to the main
executable and matches the notification's write/lookup/save/NULL-test instruction
pattern. Only then does it close the loader and retry. Relative call displacements
are ignored, so moving the same code during an update or through ASLR still works.
Other instruction patterns and calls from shared libraries are left alone.

Spotify reuses a libnotify notification object. Updating its text does not clear
the previous `image-data` hint, and Spotify skips setting a new image when artwork
is absent or decoding fails. The shim clears that hint when a successful update
from the main executable changes the summary or body; identical text updates
retain the existing artwork. Spotify can then set newly decoded
artwork as usual. If the loader still has no decoded image at notification cleanup,
the shim reads the image filename from that notification callback and decodes the
full file directly. This bypasses the client's 128 KiB file-reader limit. The filename
access requires the main executable's notification cleanup instruction pattern;
other loader closes retain their normal behavior. Missing or corrupt files still
use the notification's normal icon fallback.

Because Spotify resolves libnotify functions through a library handle, the shim
also intercepts `dlsym`. It wraps `notify_notification_update` and
`notify_notification_show` lookups from the main executable and resolves the image
setter from the same handle. The show wrapper ends the image-recovery scope. Other
lookups tail-call the original resolver, preserving caller-relative `RTLD_NEXT`
semantics. See [analysis notes](docs/notification-artwork.md) for the verified
behavior boundary.

## Build and test

```sh
make
make test
```

The native tests build small synthetic PIE programs with controlled Build IDs
and call sites; no Spotify client is included or executed. They check the original
failure, PNG/JPEG recovery, ASLR, changed Build IDs and code addresses, incremental reads,
already closed loaders, and invalid images. Launcher tests check argument and
preload preservation, acceptance of updated executables, fallback, and prepare-only behavior.
Additional tests check repeated notification text/image updates, differing
instruction patterns and main/shared-library callers, missing image setters, failed
updates, and `RTLD_NEXT` forwarding.
The libnotify regressions use the installed library and a private test D-Bus
service to check identical-text updates and large-image notification pixel data.
They never connect to the desktop notification service.

The native suite expects a decoder that defers decoding until close, as observed
with the GdkPixbuf/glycin versions above. If your decoder provides a pixbuf earlier,
the baseline assertion will fail; that does not prove the shim is broken.
Tests do not restart Spotify or send real desktop notifications. Build outputs
and Python bytecode are ignored and are not distributed.

## License

[MIT](LICENSE). This repository contains independently written shim and test code,
not Spotify binaries or extracted client resources. It is not affiliated with Spotify.
