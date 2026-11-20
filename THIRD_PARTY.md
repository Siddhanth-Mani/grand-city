# Third-party code

Everything in this repository is original work under the MIT licence
(see `LICENSE`), except:

| Library | Use | Licence | How it is included |
|---|---|---|---|
| [miniaudio](https://miniaud.io/) | Audio playback | Public domain or MIT-0 | Vendored as `src/audio/miniaudio.h` |
| [Lua 5.4](https://www.lua.org/) | Mission scripting | MIT | Downloaded by CMake at build time (`FetchContent`) |

No third-party game content, art, models, textures, sounds or branding is
used. The city, cars, people, sky and textures are all generated in code.
