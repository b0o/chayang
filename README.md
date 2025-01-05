# chayang

Forked from [emersion/chayang](https://git.sr.ht/~emersion/chayang) with the following changes:

- Supports reverse dimming
- Supports disabling exit on key/mouse events

These changes are useful for fading in after unlocking the screen:

    chayang -d 10 && swaylock && chayang -Xrd 5

Gradually dim the screen.

Can be used to implement a grace period before locking the session.

## Usage

    chayang && swaylock

## Building

    meson setup build/
    ninja -C build/

## Contributing

Send patches on the [mailing list]. Discuss in [#emersion on Libera Chat].

## License

MIT

[mailing list]: https://lists.sr.ht/~emersion/public-inbox
[#emersion on Libera Chat]: ircs://irc.libera.chat/#emersion
