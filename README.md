# Spotify notification image fix

A small Linux `LD_PRELOAD` shim that restores album artwork in Spotify track
notifications when GdkPixbuf defers image decoding until the loader is closed.
It does not modify the Spotify executable. The notification daemon must support
notification images; the original fix was confirmed with mako.

## Supported environment

This release supports exactly this x86_64 Linux Spotify executable:

| Property | Value |
| --- | --- |
| Spotify version | `1.2.96.518.g366879e1` |
| SHA-256 | `a2009fc004c57f7037c88f5055996344146d1c63b9db3b99a38c2a12bd3d341d` |
| GNU Build ID | `ab6bc2bb7cde82bf437c8d19c673ae1c044f7e25` |
| Notification call return RVA | `0x1107b6f` |

The reported failure and recovery were confirmed with GdkPixbuf 2.44.8 and
glycin 2.2.2. Other decoder configurations may not exhibit the same failure.
Different Spotify binaries are intentionally unsupported, even if their displayed
version is identical. Updates require fresh analysis rather than removing guards.

## Requirements

- Linux x86_64, a C compiler, GNU Make, and pkg-config.
- GdkPixbuf development headers.
- Python 3.11 or newer; tests additionally require Pillow.
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
and `spotify-flags.conf` handling. If the executable is unsupported or compilation
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

Spicetify customizations are compatible while the executable matches the SHA-256
above. However, Spicetify's automatic restart starts the client executable directly
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

The shim intercepts `gdk_pixbuf_loader_get_pixbuf`. It first calls the original
function. Only when that returns NULL, the main executable's GNU Build ID matches,
and the caller is the exact notification return address, it closes the loader
and retries the lookup. Other call sites and incremental loaders are left alone.
ASLR is handled by adding the return RVA to the main executable's load address.
The launcher additionally checks the whole executable's SHA-256 before loading
the shim. The runtime Build ID guard also covers an update performed by the
standard launcher after that hash check.

## Build and test

```sh
make
make test
```

The native tests build small synthetic PIE programs with controlled Build IDs
and call sites; no Spotify client is included or executed. They check the original
failure, PNG/JPEG recovery, ASLR, mismatched builds and callers, incremental reads,
already closed loaders, and invalid images. Launcher tests check argument and
preload preservation, fallback, and prepare-only behavior.

The native suite expects a decoder that defers decoding until close, as observed
with the GdkPixbuf/glycin versions above. If your decoder provides a pixbuf earlier,
the baseline assertion will fail; that does not prove the shim is broken.
Tests do not restart Spotify or send real desktop notifications. Build outputs
and Python bytecode are ignored and are not distributed.

## License

[MIT](LICENSE). This repository contains independently written shim and test code,
not Spotify binaries or extracted client resources. It is not affiliated with Spotify.
