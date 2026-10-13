# Notification artwork lifetime

## Verified executable boundary

The latest analyzed executable is identified by the SHA-256 and GNU Build ID in
README. Neither value controls runtime behavior. Analysis used objdump and a
separate, temporary Ghidra headless import; the existing interactive Ghidra
project was not changed. Ghidra used image base `0x100000`; addresses below are
executable RVAs. No client binaries, decoded resources, or decompiler output
are included.

| RVA | Independently observed behavior in the updated executable |
| --- | --- |
| `0x1180220` | Notification callback reuses the notification object at owner offset `0x70`. |
| `0x11809a9` | Calls the notification text update, returning at `0x11809ac`. |
| `0x1180fd4` | Skips the artwork path if the image result is absent. |
| `0x1181112` | Writes the complete image file buffer to the loader. |
| `0x118111f` | Pixbuf lookup return address; NULL skips image assignment. |
| `0x11811f9` | Assigns a decoded pixbuf to the reused notification. |
| `0x11810be` | Reads the image file with a `0x20000` (128 KiB) size limit. |
| `0x2ff7b00` | File reader opens, stats, and reads the file; sizes greater than or equal to the supplied limit are rejected. |
| `0x11813c2` | Loader cleanup return address; the optional filename result remains at caller stack offset `0x78`. |
| `0x1181484` | Shows the notification after optional image assignment. |

The instruction pattern around `0x118111f` matches the previously analyzed
`1.2.96.518` path at `0x1107b6f`. A scan of executable segments found exactly one
match in the updated binary. These addresses document evidence; the shim does
not compare them at runtime.

## Runtime recognition

The image lookup must originate from a readable executable segment of the main
program and match the surrounding instructions: full-image write with the loader
in RBX, lookup of that same loader, save of the returned pixbuf in R14 and a stack
slot, then a NULL test. Relative call displacements are ignored. Synthetic tests
exercise this pattern at different addresses and with different Build IDs,
reject a changed save instruction, and leave shared-library callers untouched.

The notification text update is recognized by its API name during explicit-handle
symbol lookup from the main program. Successful main-program updates that change
summary or body clear the old image; identical-text updates retain it. Calls
through the same pointer originating from shared libraries do not clear it. The source hash used for the compiled-library cache remains independent of
the client's release identity.

## Library contract and regression

[libnotify's implementation](https://github.com/GNOME/libnotify/blob/0.8.8/libnotify/notification.c)
retains `image-data` when notification text changes. Calling
`notify_notification_set_image_from_pixbuf(notification, NULL)` removes that hint.
The native test confirmed this with libnotify 0.8.8 by observing the lifetime of
a synthetic pixbuf held by a reused notification, without showing a notification.

The old shim leaves that pixbuf retained after the text update. The fixed shim
releases it when notification text changes, allowing a later successful image
assignment to supply the current artwork. Preserving it for identical text
avoids losing artwork during text-only refreshes. Failed updates and unrelated calls
keep their original behavior. If the image setter cannot be resolved from the
same library handle, the update function is returned unchanged.

This confirms a path that can show old artwork with a new title. It does not
establish that every observed mismatch follows this path. A valid but stale
image delivered by the client is outside this fix: the shim does not compare
track metadata or cancel asynchronous image requests. Live desktop verification
requires a newly started client using the updated shim; tests never restart it.

## File-reader failure and recovery

Fresh Ghidra analysis of `0x2ff7b00` and disassembly of its open/stat/read helpers
confirmed that the notification callback reads a local image file, not a base64
string. The reader rejects sizes >= `0x20000`; the original loader remains empty.

At the identified cleanup boundary, the shim can recover the filename from the
callback's still-live optional result. Its libc++ string occupies 24 bytes followed
by a presence byte. Inline strings use a size byte and at most 22 characters;
heap strings carry their length and data pointer. The matching cleanup
instructions and stack slot are checked before accessing this ABI-dependent data.
No release version or fixed address is used as a gate.

When the loader has no pixbuf, the shim decodes the complete file with GdkPixbuf
and assigns it to the notification being updated on that thread. The notification
show wrapper ends this scope. This also preserves the loader close return value
and error. Missing or corrupt files remain unresolved rather than reusing artwork
from a different notification text.

A private D-Bus regression reproduces both failures with the previous shim:
identical-text updates discard the correct image, and a synthetic 527 KiB JPEG
leaves the notification without image-data when the client reader is skipped.
The fixed path sends image-data whose pixel hash matches the decoded fixture.
This is protocol verification through real libnotify, not a live desktop drawing
check. The supplied screenshot's specific missing-image event was not captured.
After installing this fix, the user restarted Spotify and reported correct image
display; a read-only process check confirmed that the updated shim was loaded.
A pixel-by-pixel comparison of the live desktop notification was not performed.
